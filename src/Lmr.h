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

#include "SearchStack.h"

namespace valerain::search {

struct LmrNodeContext {
    int depth = 0;
    int alpha = 0;
    int beta = 0;
    int ply = 0;
    bool pv_node = false;
    bool cut_node = false;
    bool all_node = false;
    bool checked = false;
    bool improving = false;
    bool tt_move_present = false;
    bool tt_move_is_capture = false;
    int next_ply_cutoff_count = 0;
    int parent_reduction_fp = 0;
};

struct LmrMoveContext {
    Move move = 0;
    int move_index = 0;
    int reduction_index = 0;
    bool is_tt_move = false;
    bool quiet = false;
    bool capture = false;
    bool simple_capture = false;
    bool gives_check = false;
    bool recapture = false;
    bool promotion = false;
    int ordering_score = 0;
    int quiet_history_score = 0;
    int continuation_score = 0;
    int countermove_bonus = 0;
    int capture_history_score = 0;
    int see_value = 0;
    int see_bias_term = 0;
};

struct LmrDecision {
    int stat_score = 0;
    int base_reduction_fp = 0;
    int final_reduction_fp = 0;
    int final_reduction = 0;
    bool eligible = false;
};

[[nodiscard]] LmrDecision decide_lmr(
    const LmrNodeContext& node,
    const LmrMoveContext& move
) noexcept;

[[nodiscard]] int lmr_research_depth(
    const LmrDecision& decision,
    int full_depth,
    int score,
    int alpha,
    int best_score
) noexcept;

} // namespace valerain::search
