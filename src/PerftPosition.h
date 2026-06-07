#pragma once

#include <cstdint>

#include "Position.h"

namespace magnus::perft_detail {

constexpr std::uint8_t PERFT_NO_SQ = 64;
constexpr std::uint8_t PERFT_NO_PIECE = 12;

struct alignas(64) PerftPosition {
    Bitboard piece[12]{};
    Bitboard color_occ[COLOR_NB]{};
    Bitboard occupied = 0ULL;
    std::uint8_t board[SQ_NB]{};
    std::uint8_t king_sq[COLOR_NB]{ PERFT_NO_SQ, PERFT_NO_SQ };
    std::uint8_t side = WHITE;
    std::uint8_t castling = NO_CASTLING;
    std::uint8_t ep_sq = PERFT_NO_SQ;
};

struct PerftUndo {
    std::uint8_t captured = PERFT_NO_PIECE;
    std::uint8_t castling = NO_CASTLING;
    std::uint8_t ep_sq = PERFT_NO_SQ;
    std::uint8_t padding = 0;
};

static_assert(sizeof(PerftPosition) == 192);
static_assert(alignof(PerftPosition) == 64);
static_assert(sizeof(PerftUndo) == 4);

void import_position(PerftPosition& dst, const Position& src) noexcept;
void make_move(PerftPosition& pos, Move move, PerftUndo& undo) noexcept;
void unmake_move(PerftPosition& pos, Move move, const PerftUndo& undo) noexcept;

} // namespace magnus::perft_detail
