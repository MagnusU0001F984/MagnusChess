#include "PerftPosition.h"

#include "board/MoveGen.h"
#include "board/Position.h"


namespace magnus::perft_detail {
namespace {

[[nodiscard]] constexpr int piece_index(Color color, PieceType type) noexcept {
    return static_cast<int>(color) * PIECE_NB + static_cast<int>(type);
}

[[nodiscard]] constexpr Bitboard square_bb(Square sq) noexcept {
    return 1ULL << sq;
}

inline void put_piece(PerftPosition& pos, std::uint8_t piece, Square sq) noexcept {
    const Bitboard bb = square_bb(sq);
    const int color = piece / PIECE_NB;
    const int type = piece % PIECE_NB;

    pos.piece[piece] |= bb;
    pos.color_occ[color] |= bb;
    pos.occupied |= bb;
    pos.board[sq] = piece;

    if (type == KING)
        pos.king_sq[color] = static_cast<std::uint8_t>(sq);
}

[[nodiscard]] inline std::uint8_t remove_piece(
    PerftPosition& pos,
    Square sq
) noexcept {
    const std::uint8_t piece = pos.board[sq];
    const Bitboard bb = square_bb(sq);
    const int color = piece / PIECE_NB;
    const int type = piece % PIECE_NB;

    pos.piece[piece] ^= bb;
    pos.color_occ[color] ^= bb;
    pos.occupied ^= bb;
    pos.board[sq] = PERFT_NO_PIECE;

    if (type == KING)
        pos.king_sq[color] = PERFT_NO_SQ;

    return piece;
}

inline void move_piece(
    PerftPosition& pos,
    Square from,
    Square to
) noexcept {
    const std::uint8_t piece = pos.board[from];
    const Bitboard mask = square_bb(from) | square_bb(to);
    const int color = piece / PIECE_NB;
    const int type = piece % PIECE_NB;

    pos.piece[piece] ^= mask;
    pos.color_occ[color] ^= mask;
    pos.occupied ^= mask;
    pos.board[from] = PERFT_NO_PIECE;
    pos.board[to] = piece;

    if (type == KING)
        pos.king_sq[color] = static_cast<std::uint8_t>(to);
}

[[nodiscard]] constexpr std::uint8_t castling_mask(Square sq) noexcept {
    switch (sq) {
        case 0:  return static_cast<std::uint8_t>(ANY_CASTLING & ~WHITE_OOO);
        case 4:  return static_cast<std::uint8_t>(BLACK_CASTLING);
        case 7:  return static_cast<std::uint8_t>(ANY_CASTLING & ~WHITE_OO);
        case 56: return static_cast<std::uint8_t>(ANY_CASTLING & ~BLACK_OOO);
        case 60: return static_cast<std::uint8_t>(WHITE_CASTLING);
        case 63: return static_cast<std::uint8_t>(ANY_CASTLING & ~BLACK_OO);
        default: return static_cast<std::uint8_t>(ANY_CASTLING);
    }
}

} // namespace

void import_position(PerftPosition& dst, const Position& src) noexcept {
    dst = PerftPosition{};
    for (std::uint8_t& piece : dst.board)
        piece = PERFT_NO_PIECE;

    for (int sq = 0; sq < SQ_NB; ++sq) {
        const Piece piece = piece_on(src, sq);
        if (piece != PIECE_NONE)
            put_piece(dst, static_cast<std::uint8_t>(piece), sq);
    }

    dst.side = static_cast<std::uint8_t>(src.side_to_move);
    dst.castling = static_cast<std::uint8_t>(src.castling_rights);
    dst.ep_sq = src.ep_sq == NO_SQ
        ? PERFT_NO_SQ
        : static_cast<std::uint8_t>(src.ep_sq);
}

void make_move(PerftPosition& pos, Move move, PerftUndo& undo) noexcept {
    const Square from = from_sq(move);
    const Square to = to_sq(move);
    const u16 flag = move_flag(move);
    const Color us = static_cast<Color>(pos.side);

    undo.castling = pos.castling;
    undo.ep_sq = pos.ep_sq;
    undo.captured = PERFT_NO_PIECE;

    pos.castling &= castling_mask(from);
    pos.castling &= castling_mask(to);
    pos.ep_sq = PERFT_NO_SQ;

    if (flag == MOVE_OO) {
        move_piece(pos, from, to);
        move_piece(pos, us == WHITE ? 7 : 63, us == WHITE ? 5 : 61);
    } else if (flag == MOVE_OOO) {
        move_piece(pos, from, to);
        move_piece(pos, us == WHITE ? 0 : 56, us == WHITE ? 3 : 59);
    } else if (flag == MOVE_EP) {
        const Square captured_sq = us == WHITE ? to - 8 : to + 8;
        undo.captured = remove_piece(pos, captured_sq);
        move_piece(pos, from, to);
    } else if (move_is_promotion(move)) {
        if (move_is_capture(move))
            undo.captured = remove_piece(pos, to);

        (void)remove_piece(pos, from);
        put_piece(
            pos,
            static_cast<std::uint8_t>(piece_index(us, promo_piece(move))),
            to
        );
    } else {
        if (move_is_capture(move))
            undo.captured = remove_piece(pos, to);

        move_piece(pos, from, to);

        if (flag == MOVE_DOUBLE_PUSH)
            pos.ep_sq = static_cast<std::uint8_t>(us == WHITE ? from + 8 : from - 8);
    }

    pos.side ^= 1;
}

void unmake_move(
    PerftPosition& pos,
    Move move,
    const PerftUndo& undo
) noexcept {
    pos.side ^= 1;

    const Color us = static_cast<Color>(pos.side);
    const Square from = from_sq(move);
    const Square to = to_sq(move);
    const u16 flag = move_flag(move);

    if (flag == MOVE_OO) {
        move_piece(pos, to, from);
        move_piece(pos, us == WHITE ? 5 : 61, us == WHITE ? 7 : 63);
    } else if (flag == MOVE_OOO) {
        move_piece(pos, to, from);
        move_piece(pos, us == WHITE ? 3 : 59, us == WHITE ? 0 : 56);
    } else if (flag == MOVE_EP) {
        move_piece(pos, to, from);
        put_piece(pos, undo.captured, us == WHITE ? to - 8 : to + 8);
    } else if (move_is_promotion(move)) {
        (void)remove_piece(pos, to);
        put_piece(
            pos,
            static_cast<std::uint8_t>(piece_index(us, PAWN)),
            from
        );

        if (undo.captured != PERFT_NO_PIECE)
            put_piece(pos, undo.captured, to);
    } else {
        move_piece(pos, to, from);

        if (undo.captured != PERFT_NO_PIECE)
            put_piece(pos, undo.captured, to);
    }

    pos.castling = undo.castling;
    pos.ep_sq = undo.ep_sq;
}

} // namespace magnus::perft_detail
