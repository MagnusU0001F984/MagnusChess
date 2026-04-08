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

#include "TT.h"

namespace valerain::search {

struct NmpNodeContext {
    int depth = 0;
    int ply = 0;
    int alpha = 0;
    int beta = 0;
    int static_eval = 0;
    int tt_score = 0;
    int nmp_min_ply = 0;
    bool allow_null = false;
    bool pv_node = false;
    bool cut_node = false;
    bool checked = false;
    bool improving = false;
    bool exclusion_search = false;
    bool tt_hit = false;
    bool tt_move_present = false;
    bool material_ok = false;
    memory::Bound tt_bound = memory::BOUND_NONE;
};

struct NmpDecision {
    bool eligible = false;
    bool requires_verification = false;
    int eval_gate = 0;
    int eval_margin = 0;
    int reduction = 0;
    int null_depth = 0;
    int verify_depth = 0;
    int verify_min_ply = 0;
};

[[nodiscard]] bool nmp_disabled_for_ply(int ply, int nmp_min_ply) noexcept;

[[nodiscard]] NmpDecision decide_null_move(const NmpNodeContext& node) noexcept;

} // namespace valerain::search
