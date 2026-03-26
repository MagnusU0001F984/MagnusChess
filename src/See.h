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

#include "Memory.h"
#include "MoveGen.h"
#include "Position.h"

namespace valerain::search {

// Static exchange value of a capture move in centipawns.
[[nodiscard]] int see_value(
    const Position& pos,
    const memory::Memory& mem,
    Move move
) noexcept;

// Fast path for hot search code. Caller should pass a legal capture move.
[[nodiscard]] int see_value_fast(
    const Position& pos,
    const memory::Memory& mem,
    Move move
) noexcept;

// Returns whether a capture is expected to score at least `threshold`.
[[nodiscard]] bool see_ge(
    const Position& pos,
    const memory::Memory& mem,
    Move move,
    int threshold
) noexcept;

// Fast path for search hot loops. Caller should pass a legal capture move.
[[nodiscard]] bool see_ge_fast(
    const Position& pos,
    const memory::Memory& mem,
    Move move,
    int threshold
) noexcept;

} // namespace valerain::search
