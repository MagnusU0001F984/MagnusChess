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

#include <cstdint>

#include "History.h"
#include "MoveGen.h"
#include "Position.h"

namespace valerain::search {

enum class MoveStage : std::uint8_t {
    TT_MOVE = 0,
    GEN_CAPTURES,
    GOOD_CAPTURES,
    GEN_QUIETS,
    KILLER_1,
    KILLER_2,
    QUIETS,
    BAD_CAPTURES,
    DONE
};

/*
MovePicker emits legal moves in staged order:
TT move -> good captures -> killers -> quiets -> bad captures.
The picker pre-builds legal candidates once per node and keeps stage logic in
one place so the search loop can stay focused on pruning and reductions.
*/
class MovePicker {
public:
    MovePicker(
        Position& pos,
        const memory::Memory& mem,
        const GenInfo& info,
        const HistoryTables& history,
        Move tt_move,
        int ply,
        Move prev_move,
        Move prev2_move,
        int depth
    ) noexcept;

    [[nodiscard]] Move next() noexcept;

    [[nodiscard]] int last_score() const noexcept { return last_score_; }
    [[nodiscard]] bool last_was_capture() const noexcept { return last_was_capture_; }
    [[nodiscard]] bool last_was_bad_capture() const noexcept { return last_was_bad_capture_; }
    [[nodiscard]] int last_see_value() const noexcept { return last_see_value_; }

private:
    struct ScoredEntry {
        Move move = 0;
        int score = 0;
        int see_value = 0;
    };

    Position& pos_;
    const memory::Memory& mem_;
    const GenInfo& info_;
    const HistoryTables& history_;

    Move tt_move_ = Move(0);
    Move killer1_ = Move(0);
    Move killer2_ = Move(0);
    Move prev_move_ = Move(0);
    Move prev2_move_ = Move(0);
    int depth_ = 0;

    MoveStage stage_ = MoveStage::TT_MOVE;

    ScoredEntry good_caps_[MAX_MOVES]{};
    ScoredEntry bad_caps_[MAX_MOVES]{};
    ScoredEntry quiets_[MAX_MOVES]{};
    int good_size_ = 0;
    int bad_size_ = 0;
    int quiet_size_ = 0;
    int good_idx_ = 0;
    int bad_idx_ = 0;
    int quiet_idx_ = 0;

    bool tt_ready_ = false;
    int tt_score_ = 0;
    int tt_see_value_ = 0;
    bool tt_bad_capture_ = false;

    bool killer1_ready_ = false;
    bool killer2_ready_ = false;
    Move killer1_move_ = Move(0);
    Move killer2_move_ = Move(0);
    int killer1_score_ = 0;
    int killer2_score_ = 0;

    int last_score_ = 0;
    bool last_was_capture_ = false;
    bool last_was_bad_capture_ = false;
    int last_see_value_ = 0;

private:
    void build_lists() noexcept;
    void add_capture(Move move) noexcept;
    void add_quiet(Move move) noexcept;
    void choose_killers() noexcept;
    [[nodiscard]] bool quiet_score_from_list(Move move, int& score) const noexcept;

    [[nodiscard]] int score_capture(Move move, int see_value) const noexcept;
    [[nodiscard]] int score_quiet(Move move) const noexcept;

    [[nodiscard]] ScoredEntry pick_best_entry(
        ScoredEntry* list,
        int size,
        int& index
    ) noexcept;

    inline void set_last(
        int score,
        bool capture,
        bool bad_capture,
        int see_value
    ) noexcept {
        last_score_ = score;
        last_was_capture_ = capture;
        last_was_bad_capture_ = bad_capture;
        last_see_value_ = see_value;
    }
};

} // namespace valerain::search
