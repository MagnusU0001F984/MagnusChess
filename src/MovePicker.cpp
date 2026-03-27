/*
MIT License

Copyright (c) 2026 Mazhaoze

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "MovePicker.h"

#include "See.h"

namespace valerain::search {

namespace {

constexpr int piece_order_value[PIECE_TYPE_NB] = {
    100, 320, 330, 500, 900, 0
};

#ifndef VALERAIN_SEE_TERM_PRESET
#define VALERAIN_SEE_TERM_PRESET 1
#endif

#if VALERAIN_SEE_TERM_PRESET == 0
constexpr SeeScalePreset MOVE_PICKER_SEE_TERM_PRESET = SeeScalePreset::Weak;
#elif VALERAIN_SEE_TERM_PRESET == 1
constexpr SeeScalePreset MOVE_PICKER_SEE_TERM_PRESET = SeeScalePreset::Medium;
#elif VALERAIN_SEE_TERM_PRESET == 2
constexpr SeeScalePreset MOVE_PICKER_SEE_TERM_PRESET = SeeScalePreset::Strong;
#else
#error "VALERAIN_SEE_TERM_PRESET must be 0 (Weak), 1 (Medium), or 2 (Strong)"
#endif

[[nodiscard]] inline int mvv_lva_capture_term(
    const Position& pos,
    Move move
) noexcept {
    const PieceType attacker = piece_type_on(pos, from_sq(move));
    const PieceType victim = move_is_ep(move) ? PAWN : piece_type_on(pos, to_sq(move));
    const int attacker_value = is_ok(attacker) ? piece_order_value[attacker] : 0;
    const int victim_value = is_ok(victim) ? piece_order_value[victim] : 0;
    return victim_value * 32 - attacker_value;
}

} // namespace

MovePicker::MovePicker(
    Position& pos,
    const memory::Memory& mem,
    const GenInfo& info,
    const HistoryTables& history,
    Move tt_move,
    int ply,
    Move prev_move,
    Move prev2_move,
    int depth
) noexcept
    : pos_(pos)
    , mem_(mem)
    , info_(info)
    , history_(history)
    , tt_move_(tt_move)
    , killer1_(history.killer_fast(ply, 0))
    , killer2_(history.killer_fast(ply, 1))
    , prev_move_(prev_move)
    , prev2_move_(prev2_move)
    , depth_(depth) {
    build_lists();
}

Move MovePicker::next() noexcept {
    while (true) {
        switch (stage_) {
            case MoveStage::TT_MOVE:
                stage_ = MoveStage::GEN_CAPTURES;
                if (tt_ready_) {
                    tt_ready_ = false;
                    set_last(
                        tt_score_,
                        move_is_capture(tt_move_),
                        tt_bad_capture_,
                        tt_see_value_
                    );
                    return tt_move_;
                }
                break;

            case MoveStage::GEN_CAPTURES:
                stage_ = MoveStage::GOOD_CAPTURES;
                break;

            case MoveStage::GOOD_CAPTURES:
                if (good_idx_ < good_size_) {
                    const ScoredEntry e = pick_best_entry(good_caps_, good_size_, good_idx_);
                    set_last(e.score, true, false, e.see_value);
                    return e.move;
                }
                stage_ = MoveStage::GEN_QUIETS;
                break;

            case MoveStage::GEN_QUIETS:
                stage_ = MoveStage::KILLER_1;
                break;

            case MoveStage::KILLER_1:
                stage_ = MoveStage::KILLER_2;
                if (killer1_ready_) {
                    killer1_ready_ = false;
                    set_last(score_quiet(killer1_move_), false, false, 0);
                    return killer1_move_;
                }
                break;

            case MoveStage::KILLER_2:
                stage_ = MoveStage::QUIETS;
                if (killer2_ready_) {
                    killer2_ready_ = false;
                    set_last(score_quiet(killer2_move_), false, false, 0);
                    return killer2_move_;
                }
                break;

            case MoveStage::QUIETS:
                while (quiet_idx_ < quiet_size_) {
                    const ScoredEntry e = pick_best_entry(quiets_, quiet_size_, quiet_idx_);
                    if (e.move == killer1_move_ || e.move == killer2_move_)
                        continue;
                    set_last(e.score, false, false, 0);
                    return e.move;
                }
                stage_ = MoveStage::BAD_CAPTURES;
                break;

            case MoveStage::BAD_CAPTURES:
                if (bad_idx_ < bad_size_) {
                    const ScoredEntry e = pick_best_entry(bad_caps_, bad_size_, bad_idx_);
                    set_last(e.score, true, true, e.see_value);
                    return e.move;
                }
                stage_ = MoveStage::DONE;
                break;

            case MoveStage::DONE:
                return Move(0);
        }
    }
}

void MovePicker::build_lists() noexcept {
    MoveList list;
    Move* end = generate_pseudo_legal(pos_, mem_, info_, list.moves);
    list.size = static_cast<int>(end - list.moves);

    for (int i = 0; i < list.size; ++i) {
        const Move move = list.moves[i];
        if (!legal_fast(pos_, mem_, info_, move))
            continue;

        if (move == tt_move_) {
            tt_ready_ = true;
            if (move_is_capture(move)) {
                tt_see_value_ = search::see_value_fast(pos_, mem_, move);
                tt_bad_capture_ = tt_see_value_ < 0;
                tt_score_ = score_capture(move, tt_see_value_);
            } else {
                tt_see_value_ = 0;
                tt_bad_capture_ = false;
                tt_score_ = score_quiet(move);
            }
            continue;
        }

        if (move_is_capture(move))
            add_capture(move);
        else
            add_quiet(move);
    }

    choose_killers();
}

void MovePicker::add_capture(Move move) noexcept {
    const int see_value = search::see_value_fast(pos_, mem_, move);
    const ScoredEntry entry{
        move,
        score_capture(move, see_value),
        see_value
    };

    if (see_value >= 0) {
        good_caps_[good_size_] = entry;
        ++good_size_;
    } else {
        bad_caps_[bad_size_] = entry;
        ++bad_size_;
    }
}

void MovePicker::add_quiet(Move move) noexcept {
    quiets_[quiet_size_] = ScoredEntry{
        move,
        score_quiet(move),
        0
    };
    ++quiet_size_;
}

void MovePicker::choose_killers() noexcept {
    killer1_ready_ = false;
    killer2_ready_ = false;
    killer1_move_ = Move(0);
    killer2_move_ = Move(0);

    if (!move_is_none(killer1_) &&
        killer1_ != tt_move_ &&
        !move_is_capture(killer1_) &&
        move_in_quiets(killer1_)) {
        killer1_ready_ = true;
        killer1_move_ = killer1_;
    }

    if (!move_is_none(killer2_) &&
        killer2_ != tt_move_ &&
        killer2_ != killer1_move_ &&
        !move_is_capture(killer2_) &&
        move_in_quiets(killer2_)) {
        killer2_ready_ = true;
        killer2_move_ = killer2_;
    }
}

bool MovePicker::move_in_quiets(Move move) const noexcept {
    for (int i = 0; i < quiet_size_; ++i)
        if (quiets_[i].move == move)
            return true;
    return false;
}

int MovePicker::score_capture(Move move, int see_value) const noexcept {
    return mvv_lva_capture_term(pos_, move)
        + history_.capture_value_fast(pos_, move)
        + see_immediate_term(see_value, MOVE_PICKER_SEE_TERM_PRESET)
        + history_.see_bias_value_fast(depth_, see_value);
}

int MovePicker::score_quiet(Move move) const noexcept {
    int score = history_.quiet_value_fast(pos_, move);
    score += history_.countermove_bonus_fast(pos_, move, prev_move_);
    score += history_.continuation_value_fast(pos_, move, prev_move_);
    score += history_.continuation_value_fast(pos_, move, prev2_move_) / 2;
    return score;
}

MovePicker::ScoredEntry MovePicker::pick_best_entry(
    ScoredEntry* list,
    int size,
    int& index
) noexcept {
    int best = index;
    for (int i = index + 1; i < size; ++i) {
        if (list[i].score > list[best].score)
            best = i;
    }

    if (best != index) {
        const ScoredEntry tmp = list[index];
        list[index] = list[best];
        list[best] = tmp;
    }

    return list[index++];
}

} // namespace valerain::search
