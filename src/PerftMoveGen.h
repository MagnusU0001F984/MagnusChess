#pragma once

#include "Memory.h"
#include "PerftPosition.h"

namespace magnus::perft_detail {

constexpr int PERFT_MAX_MOVES = 256;

struct PerftMoveList {
    Move moves[PERFT_MAX_MOVES]{};
    int size = 0;
};

void generate_legal(
    const PerftPosition& pos,
    const memory::Memory& mem,
    PerftMoveList& list
) noexcept;

int count_legal(
    const PerftPosition& pos,
    const memory::Memory& mem
) noexcept;

} // namespace magnus::perft_detail
