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

#pragma once

#include <algorithm>
#include <cstdint>

#include "MoveGen.h"
#include "Position.h"
#include "Search.h"
#include "Types.h"

namespace valerain::search {

enum class SeeClass : std::uint8_t {
    LossBig = 0,
    LossSmall,
    Equal,
    GainSmall,
    GainBig,
    Promo,
    Check,
    Count
};

enum class DepthClass : std::uint8_t {
    Shallow = 0,
    Medium,
    Deep,
    Count
};

enum class SeeScalePreset : std::uint8_t {
    Weak = 0,
    Medium,
    Strong
};

struct KillerTable {
    Move table[MAX_PLY][2]{};
};

struct QuietHistoryTable {
    i16 value[COLOR_NB][SQ_NB][SQ_NB]{};
};

struct CaptureHistoryTable {
    i16 value[COLOR_NB][PIECE_TYPE_NB][SQ_NB][PIECE_TYPE_NB]{};
};

struct CounterMoveTable {
    Move value[COLOR_NB][SQ_NB][SQ_NB]{};
};

struct SeeBiasTable {
    i16 value[static_cast<int>(DepthClass::Count)][static_cast<int>(SeeClass::Count)]{};
};

[[nodiscard]] DepthClass depth_class(int depth) noexcept;
[[nodiscard]] SeeClass classify_see(int see_value, bool gives_check, bool is_promotion) noexcept;
[[nodiscard]] int history_bonus(int depth) noexcept;
[[nodiscard]] int history_penalty(int depth) noexcept;
[[nodiscard]] int see_immediate_term(int see_value, SeeScalePreset preset) noexcept;

/*
HistoryTables separates quiet and capture experience, while keeping killers and
countermove hints in one place for move ordering and light pruning signals.
*/
struct HistoryTables {
    KillerTable killers{};
    QuietHistoryTable quiet{};
    CaptureHistoryTable capture{};
    CounterMoveTable countermove{};
    SeeBiasTable see_bias{}; // Reserved for step-3 integration.

    void clear() noexcept;

    [[nodiscard]] inline Move killer_fast(int ply, int slot) const noexcept {
        return killers.table[ply][slot];
    }
    [[nodiscard]] inline i32 quiet_value_fast(const Position& pos, Move move) const noexcept {
        if (move_is_capture(move))
            return 0;

        const Color side = static_cast<Color>(pos.side_to_move);
        return quiet.value[side][from_sq(move)][to_sq(move)];
    }
    [[nodiscard]] inline i32 capture_value_fast(const Position& pos, Move move) const noexcept {
        if (!move_is_capture(move))
            return 0;

        const Color side = static_cast<Color>(pos.side_to_move);
        const PieceType mover = piece_type_on(pos, from_sq(move));
        const PieceType captured = move_is_ep(move) ? PAWN : piece_type_on(pos, to_sq(move));
        return capture.value[side][mover][to_sq(move)][captured];
    }

    inline void bonus_fast(const Position& pos, Move move, int depth) noexcept {
        if (move_is_capture(move))
            return;

        const Color side = static_cast<Color>(pos.side_to_move);
        i16& h = quiet.value[side][from_sq(move)][to_sq(move)];
        const i32 next = static_cast<i32>(h) + history_bonus(depth);
        h = static_cast<i16>(std::clamp(next, -32767, 32767));
    }
    inline void penalty_fast(const Position& pos, Move move, int depth) noexcept {
        if (move_is_capture(move))
            return;

        const Color side = static_cast<Color>(pos.side_to_move);
        i16& h = quiet.value[side][from_sq(move)][to_sq(move)];
        const i32 next = static_cast<i32>(h) - history_penalty(depth);
        h = static_cast<i16>(std::clamp(next, -32767, 32767));
    }
    inline void bonus_capture_fast(const Position& pos, Move move, int depth) noexcept {
        if (!move_is_capture(move))
            return;

        const Color side = static_cast<Color>(pos.side_to_move);
        const PieceType mover = piece_type_on(pos, from_sq(move));
        const PieceType captured = move_is_ep(move) ? PAWN : piece_type_on(pos, to_sq(move));
        i16& h = capture.value[side][mover][to_sq(move)][captured];
        const i32 next = static_cast<i32>(h) + history_bonus(depth);
        h = static_cast<i16>(std::clamp(next, -32767, 32767));
    }
    inline void penalty_capture_fast(const Position& pos, Move move, int depth) noexcept {
        if (!move_is_capture(move))
            return;

        const Color side = static_cast<Color>(pos.side_to_move);
        const PieceType mover = piece_type_on(pos, from_sq(move));
        const PieceType captured = move_is_ep(move) ? PAWN : piece_type_on(pos, to_sq(move));
        i16& h = capture.value[side][mover][to_sq(move)][captured];
        const i32 next = static_cast<i32>(h) - history_penalty(depth);
        h = static_cast<i16>(std::clamp(next, -32767, 32767));
    }

    inline void penalize_quiets_fast(
        const Position& pos,
        const Move* quiets,
        int count,
        Move excluded_move,
        int depth
    ) noexcept {
        for (int i = 0; i < count; ++i)
            if (quiets[i] != excluded_move)
                penalty_fast(pos, quiets[i], depth);
    }
    inline void penalize_captures_fast(
        const Position& pos,
        const Move* caps,
        int count,
        Move excluded_move,
        int depth
    ) noexcept {
        for (int i = 0; i < count; ++i)
            if (caps[i] != excluded_move)
                penalty_capture_fast(pos, caps[i], depth);
    }

    inline void reward_cutoff_fast(
        const Position& pos,
        Move move,
        int depth,
        int ply
    ) noexcept {
        if (move_is_capture(move)) {
            bonus_capture_fast(pos, move, depth);
            return;
        }

        if (killers.table[ply][0] != move) {
            killers.table[ply][1] = killers.table[ply][0];
            killers.table[ply][0] = move;
        }

        bonus_fast(pos, move, depth);
    }
};

} // namespace valerain::search
