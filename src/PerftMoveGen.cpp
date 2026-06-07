#include "PerftMoveGen.h"

#include "Attack.h"
#include "MoveGen.h"

#include <bit>

namespace magnus::perft_detail {
namespace {

constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
constexpr Bitboard FILE_H_BB = 0x8080808080808080ULL;
constexpr Bitboard RANK_2_BB = 0x000000000000FF00ULL;
constexpr Bitboard RANK_3_BB = 0x0000000000FF0000ULL;
constexpr Bitboard RANK_6_BB = 0x0000FF0000000000ULL;
constexpr Bitboard RANK_7_BB = 0x00FF000000000000ULL;

template<Color C, PieceType Pt>
[[nodiscard]] constexpr int piece_index() noexcept {
    return static_cast<int>(C) * PIECE_NB + static_cast<int>(Pt);
}

template<Color C, PieceType Pt>
[[nodiscard]] inline Bitboard piece_bb(const PerftPosition& pos) noexcept {
    return pos.piece[piece_index<C, Pt>()];
}

[[nodiscard]] inline Square pop_lsb(Bitboard& bb) noexcept {
    const Square sq = static_cast<Square>(std::countr_zero(bb));
    bb &= bb - 1;
    return sq;
}

template<bool Store>
struct MoveSink {
    Move* moves = nullptr;
    int size = 0;

    inline void add(Move move) noexcept {
        if constexpr (Store)
            moves[size] = move;
        ++size;
    }
};

struct GenInfo {
    Square king_sq = NO_SQ;
    Bitboard occupied = 0ULL;
    Bitboard us_occ = 0ULL;
    Bitboard them_occ = 0ULL;
    Bitboard checkers = 0ULL;
    Bitboard pinned = 0ULL;
    Bitboard capture_mask = 0ULL;
    Bitboard push_mask = 0ULL;
    bool in_check = false;
    bool double_check = false;
};

template<Color By>
[[nodiscard]] inline Bitboard attackers_to(
    const PerftPosition& pos,
    const memory::Memory& mem,
    Square sq,
    Bitboard occupied,
    Bitboard removed_pawns = 0ULL
) noexcept {
    constexpr Color Other = By == WHITE ? BLACK : WHITE;
    const Bitboard pawns = piece_bb<By, PAWN>(pos) & ~removed_pawns;

    return (pawn_attacks(mem, Other, sq) & pawns)
         | (knight_attacks(mem, sq) & piece_bb<By, KNIGHT>(pos))
         | (king_attacks(mem, sq) & piece_bb<By, KING>(pos))
         | (bishop_attacks_fast(mem, sq, occupied)
            & (piece_bb<By, BISHOP>(pos) | piece_bb<By, QUEEN>(pos)))
         | (rook_attacks_fast(mem, sq, occupied)
            & (piece_bb<By, ROOK>(pos) | piece_bb<By, QUEEN>(pos)));
}

template<Color Us>
[[nodiscard]] GenInfo init_info(
    const PerftPosition& pos,
    const memory::Memory& mem
) noexcept {
    constexpr Color Them = Us == WHITE ? BLACK : WHITE;

    GenInfo info;
    info.king_sq = static_cast<Square>(pos.king_sq[Us]);
    info.occupied = pos.occupied;
    info.us_occ = pos.color_occ[Us];
    info.them_occ = pos.color_occ[Them];
    info.checkers = attackers_to<Them>(
        pos,
        mem,
        info.king_sq,
        info.occupied
    );

    Bitboard pinners =
        rook_xray_attacks(mem, info.king_sq, info.occupied, info.us_occ)
            & (piece_bb<Them, ROOK>(pos) | piece_bb<Them, QUEEN>(pos));
    pinners |=
        bishop_xray_attacks(mem, info.king_sq, info.occupied, info.us_occ)
            & (piece_bb<Them, BISHOP>(pos) | piece_bb<Them, QUEEN>(pos));

    while (pinners) {
        const Square pinner = pop_lsb(pinners);
        info.pinned |= between_bb(mem, info.king_sq, pinner) & info.us_occ;
    }

    info.in_check = info.checkers != 0ULL;
    info.double_check =
        info.in_check && (info.checkers & (info.checkers - 1)) != 0ULL;

    if (!info.in_check) {
        info.capture_mask = info.them_occ;
        info.push_mask = ~info.occupied;
    } else if (!info.double_check) {
        const Square checker = static_cast<Square>(std::countr_zero(info.checkers));
        const Bitboard checker_bb = bb_of(checker);
        const Bitboard sliders =
            piece_bb<Them, BISHOP>(pos)
            | piece_bb<Them, ROOK>(pos)
            | piece_bb<Them, QUEEN>(pos);

        info.capture_mask = checker_bb;
        info.push_mask = (checker_bb & sliders)
            ? between_bb(mem, info.king_sq, checker)
            : 0ULL;
    }

    return info;
}

template<bool Store>
inline void emit_mask(
    MoveSink<Store>& sink,
    Square from,
    Bitboard targets,
    Bitboard them_occ
) noexcept {
    while (targets) {
        const Square to = pop_lsb(targets);
        sink.add(magnus::make_move(
            from,
            to,
            (them_occ & bb_of(to)) ? MOVE_CAPTURE : MOVE_QUIET
        ));
    }
}

template<bool Store>
inline void emit_promotions(
    MoveSink<Store>& sink,
    Square from,
    Square to,
    bool capture
) noexcept {
    const u16 base = capture ? MOVE_CAP_PROMO_N : MOVE_PROMO_N;
    sink.add(magnus::make_move(from, to, base));
    sink.add(magnus::make_move(from, to, static_cast<u16>(base + 1)));
    sink.add(magnus::make_move(from, to, static_cast<u16>(base + 2)));
    sink.add(magnus::make_move(from, to, static_cast<u16>(base + 3)));
}

template<Color Us, PieceType Pt, bool Store>
inline void generate_piece_moves(
    const PerftPosition& pos,
    const memory::Memory& mem,
    const GenInfo& info,
    MoveSink<Store>& sink
) noexcept {
    Bitboard pieces = piece_bb<Us, Pt>(pos);
    const Bitboard evasion_mask = info.capture_mask | info.push_mask;

    while (pieces) {
        const Square from = pop_lsb(pieces);
        Bitboard targets;

        if constexpr (Pt == KNIGHT)
            targets = knight_attacks(mem, from);
        else if constexpr (Pt == BISHOP)
            targets = bishop_attacks_fast(mem, from, info.occupied);
        else if constexpr (Pt == ROOK)
            targets = rook_attacks_fast(mem, from, info.occupied);
        else
            targets = queen_attacks_fast(mem, from, info.occupied);

        targets &= ~info.us_occ;

        if (info.pinned & bb_of(from))
            targets &= line_bb(mem, info.king_sq, from);

        if (info.in_check)
            targets &= evasion_mask;

        emit_mask(sink, from, targets, info.them_occ);
    }
}

template<Color Us>
[[nodiscard]] inline Bitboard shift_push(Bitboard pawns) noexcept {
    if constexpr (Us == WHITE)
        return pawns << 8;
    else
        return pawns >> 8;
}

template<Color Us>
[[nodiscard]] inline Bitboard shift_left_capture(Bitboard pawns) noexcept {
    if constexpr (Us == WHITE)
        return (pawns & ~FILE_A_BB) << 7;
    else
        return (pawns & ~FILE_A_BB) >> 9;
}

template<Color Us>
[[nodiscard]] inline Bitboard shift_right_capture(Bitboard pawns) noexcept {
    if constexpr (Us == WHITE)
        return (pawns & ~FILE_H_BB) << 9;
    else
        return (pawns & ~FILE_H_BB) >> 7;
}

template<Color Us, int Delta, bool Store>
inline void emit_pawn_targets(
    MoveSink<Store>& sink,
    Bitboard targets,
    u16 flag
) noexcept {
    while (targets) {
        const Square to = pop_lsb(targets);
        sink.add(magnus::make_move(to - Delta, to, flag));
    }
}

template<Color Us, int Delta, bool Store>
inline void emit_promotion_targets(
    MoveSink<Store>& sink,
    Bitboard targets,
    bool capture
) noexcept {
    while (targets) {
        const Square to = pop_lsb(targets);
        emit_promotions(sink, to - Delta, to, capture);
    }
}

template<Color Us, bool Store>
inline void generate_unpinned_pawns(
    const GenInfo& info,
    Bitboard pawns,
    MoveSink<Store>& sink
) noexcept {
    constexpr int Push = Us == WHITE ? 8 : -8;
    constexpr int Left = Us == WHITE ? 7 : -9;
    constexpr int Right = Us == WHITE ? 9 : -7;
    constexpr Bitboard PromotionRank = Us == WHITE ? RANK_7_BB : RANK_2_BB;
    constexpr Bitboard DoubleMidRank = Us == WHITE ? RANK_3_BB : RANK_6_BB;

    const Bitboard empty = ~info.occupied;
    const Bitboard promotion_pawns = pawns & PromotionRank;
    const Bitboard normal_pawns = pawns & ~PromotionRank;

    Bitboard one = shift_push<Us>(normal_pawns) & empty;
    Bitboard two = shift_push<Us>(one & DoubleMidRank) & empty;
    Bitboard promotions = shift_push<Us>(promotion_pawns) & empty;

    Bitboard left = shift_left_capture<Us>(normal_pawns) & info.them_occ;
    Bitboard right = shift_right_capture<Us>(normal_pawns) & info.them_occ;
    Bitboard left_promotions =
        shift_left_capture<Us>(promotion_pawns) & info.them_occ;
    Bitboard right_promotions =
        shift_right_capture<Us>(promotion_pawns) & info.them_occ;

    if (info.in_check) {
        one &= info.push_mask;
        two &= info.push_mask;
        promotions &= info.push_mask;
        left &= info.capture_mask;
        right &= info.capture_mask;
        left_promotions &= info.capture_mask;
        right_promotions &= info.capture_mask;
    }

    emit_pawn_targets<Us, Push>(sink, one, MOVE_QUIET);
    emit_pawn_targets<Us, Push * 2>(sink, two, MOVE_DOUBLE_PUSH);
    emit_pawn_targets<Us, Left>(sink, left, MOVE_CAPTURE);
    emit_pawn_targets<Us, Right>(sink, right, MOVE_CAPTURE);
    emit_promotion_targets<Us, Push>(sink, promotions, false);
    emit_promotion_targets<Us, Left>(sink, left_promotions, true);
    emit_promotion_targets<Us, Right>(sink, right_promotions, true);
}

template<Color Us, bool Store>
inline void generate_pinned_pawns(
    const memory::Memory& mem,
    const GenInfo& info,
    Bitboard pawns,
    MoveSink<Store>& sink
) noexcept {
    constexpr int Push = Us == WHITE ? 8 : -8;
    constexpr int Left = Us == WHITE ? 7 : -9;
    constexpr int Right = Us == WHITE ? 9 : -7;
    constexpr int PromotionRank = Us == WHITE ? 6 : 1;
    constexpr int DoubleRank = Us == WHITE ? 1 : 6;

    while (pawns) {
        const Square from = pop_lsb(pawns);
        const Bitboard line = line_bb(mem, info.king_sq, from);
        const int rank = rank_of(from);
        const int file = file_of(from);
        const Square one = from + Push;
        const Bitboard one_bb = bb_of(one);

        if ((info.occupied & one_bb) == 0ULL) {
            if ((line & one_bb) &&
                (!info.in_check || (info.push_mask & one_bb))) {
                if (rank == PromotionRank)
                    emit_promotions(sink, from, one, false);
                else
                    sink.add(magnus::make_move(from, one, MOVE_QUIET));
            }

            if (rank == DoubleRank) {
                const Square two = from + 2 * Push;
                const Bitboard two_bb = bb_of(two);
                if ((info.occupied & two_bb) == 0ULL &&
                    (line & two_bb) &&
                    (!info.in_check || (info.push_mask & two_bb))) {
                    sink.add(magnus::make_move(from, two, MOVE_DOUBLE_PUSH));
                }
            }
        }

        if (file > 0) {
            const Square to = from + Left;
            const Bitboard to_bb = bb_of(to);
            if ((info.them_occ & to_bb) &&
                (line & to_bb) &&
                (!info.in_check || (info.capture_mask & to_bb))) {
                if (rank == PromotionRank)
                    emit_promotions(sink, from, to, true);
                else
                    sink.add(magnus::make_move(from, to, MOVE_CAPTURE));
            }
        }

        if (file < 7) {
            const Square to = from + Right;
            const Bitboard to_bb = bb_of(to);
            if ((info.them_occ & to_bb) &&
                (line & to_bb) &&
                (!info.in_check || (info.capture_mask & to_bb))) {
                if (rank == PromotionRank)
                    emit_promotions(sink, from, to, true);
                else
                    sink.add(magnus::make_move(from, to, MOVE_CAPTURE));
            }
        }
    }

}

template<Color Us, bool Store>
inline void generate_en_passant(
    const PerftPosition& pos,
    const memory::Memory& mem,
    const GenInfo& info,
    MoveSink<Store>& sink
) noexcept {
    constexpr Color Them = Us == WHITE ? BLACK : WHITE;

    if (pos.ep_sq == PERFT_NO_SQ || info.double_check)
        return;

    const Square to = static_cast<Square>(pos.ep_sq);
    const Square captured_sq = Us == WHITE ? to - 8 : to + 8;
    const Bitboard captured_bb = bb_of(captured_sq);

    if (pos.board[captured_sq] != piece_index<Them, PAWN>())
        return;

    if (info.in_check &&
        !(info.capture_mask & captured_bb) &&
        !(info.push_mask & bb_of(to))) {
        return;
    }

    Bitboard pawns =
        pawn_attacks(mem, Them, to) & piece_bb<Us, PAWN>(pos);

    while (pawns) {
        const Square from = pop_lsb(pawns);
        const Bitboard occupied =
            (info.occupied ^ bb_of(from) ^ captured_bb) | bb_of(to);

        if (attackers_to<Them>(
                pos,
                mem,
                info.king_sq,
                occupied,
                captured_bb
            ) == 0ULL) {
            sink.add(magnus::make_move(from, to, MOVE_EP));
        }
    }
}

template<Color Us, bool Store>
inline void generate_king_moves(
    const PerftPosition& pos,
    const memory::Memory& mem,
    const GenInfo& info,
    MoveSink<Store>& sink
) noexcept {
    constexpr Color Them = Us == WHITE ? BLACK : WHITE;

    Bitboard targets = king_attacks(mem, info.king_sq) & ~info.us_occ;
    const Bitboard occupied_without_king =
        info.occupied ^ bb_of(info.king_sq);

    while (targets) {
        const Square to = pop_lsb(targets);
        if (attackers_to<Them>(
                pos,
                mem,
                to,
                occupied_without_king
            ) == 0ULL) {
            sink.add(magnus::make_move(
                info.king_sq,
                to,
                (info.them_occ & bb_of(to)) ? MOVE_CAPTURE : MOVE_QUIET
            ));
        }
    }
}

template<Color Us, bool Store>
inline void generate_castling(
    const PerftPosition& pos,
    const memory::Memory& mem,
    const GenInfo& info,
    MoveSink<Store>& sink
) noexcept {
    constexpr Color Them = Us == WHITE ? BLACK : WHITE;

    if (info.in_check)
        return;

    const Bitboard occupied_without_king =
        info.occupied ^ bb_of(info.king_sq);

    if constexpr (Us == WHITE) {
        if ((pos.castling & WHITE_OO) &&
            pos.board[4] == piece_index<WHITE, KING>() &&
            pos.board[7] == piece_index<WHITE, ROOK>() &&
            !(info.occupied & (bb_of(5) | bb_of(6))) &&
            attackers_to<Them>(pos, mem, 5, occupied_without_king) == 0ULL &&
            attackers_to<Them>(pos, mem, 6, occupied_without_king) == 0ULL) {
            sink.add(magnus::make_move(4, 6, MOVE_OO));
        }

        if ((pos.castling & WHITE_OOO) &&
            pos.board[4] == piece_index<WHITE, KING>() &&
            pos.board[0] == piece_index<WHITE, ROOK>() &&
            !(info.occupied & (bb_of(1) | bb_of(2) | bb_of(3))) &&
            attackers_to<Them>(pos, mem, 3, occupied_without_king) == 0ULL &&
            attackers_to<Them>(pos, mem, 2, occupied_without_king) == 0ULL) {
            sink.add(magnus::make_move(4, 2, MOVE_OOO));
        }
    } else {
        if ((pos.castling & BLACK_OO) &&
            pos.board[60] == piece_index<BLACK, KING>() &&
            pos.board[63] == piece_index<BLACK, ROOK>() &&
            !(info.occupied & (bb_of(61) | bb_of(62))) &&
            attackers_to<Them>(pos, mem, 61, occupied_without_king) == 0ULL &&
            attackers_to<Them>(pos, mem, 62, occupied_without_king) == 0ULL) {
            sink.add(magnus::make_move(60, 62, MOVE_OO));
        }

        if ((pos.castling & BLACK_OOO) &&
            pos.board[60] == piece_index<BLACK, KING>() &&
            pos.board[56] == piece_index<BLACK, ROOK>() &&
            !(info.occupied & (bb_of(57) | bb_of(58) | bb_of(59))) &&
            attackers_to<Them>(pos, mem, 59, occupied_without_king) == 0ULL &&
            attackers_to<Them>(pos, mem, 58, occupied_without_king) == 0ULL) {
            sink.add(magnus::make_move(60, 58, MOVE_OOO));
        }
    }
}

template<Color Us, bool Store>
inline void generate_for(
    const PerftPosition& pos,
    const memory::Memory& mem,
    MoveSink<Store>& sink
) noexcept {
    const GenInfo info = init_info<Us>(pos, mem);

    generate_king_moves<Us>(pos, mem, info, sink);

    if (!info.double_check) {
        generate_piece_moves<Us, KNIGHT>(pos, mem, info, sink);
        generate_piece_moves<Us, BISHOP>(pos, mem, info, sink);
        generate_piece_moves<Us, ROOK>(pos, mem, info, sink);
        generate_piece_moves<Us, QUEEN>(pos, mem, info, sink);

        const Bitboard pawns = piece_bb<Us, PAWN>(pos);
        generate_unpinned_pawns<Us>(info, pawns & ~info.pinned, sink);
        generate_pinned_pawns<Us>(
            mem,
            info,
            pawns & info.pinned,
            sink
        );
        generate_en_passant<Us>(pos, mem, info, sink);
        generate_castling<Us>(pos, mem, info, sink);
    }
}

template<bool Store>
[[nodiscard]] int generate_impl(
    const PerftPosition& pos,
    const memory::Memory& mem,
    Move* moves
) noexcept {
    MoveSink<Store> sink{moves, 0};

    if (pos.side == WHITE)
        generate_for<WHITE>(pos, mem, sink);
    else
        generate_for<BLACK>(pos, mem, sink);

    return sink.size;
}

} // namespace

void generate_legal(
    const PerftPosition& pos,
    const memory::Memory& mem,
    PerftMoveList& list
) noexcept {
    list.size = generate_impl<true>(pos, mem, list.moves);
}

int count_legal(
    const PerftPosition& pos,
    const memory::Memory& mem
) noexcept {
    return generate_impl<false>(pos, mem, nullptr);
}

} // namespace magnus::perft_detail
