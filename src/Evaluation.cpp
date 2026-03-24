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

/*
We can use Incremental Evaluation ,they can help us to improve time to O(N) -> O(1) 
But i think this is Constant.
*/

#include "Evaluation.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <mutex>

#include "Types.h"

namespace valerain::eval {

namespace {

#include <cstdint>

constexpr Score make_score(int mg, int eg) noexcept {
    return static_cast<Score>(
        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(mg)) << 16) |
         static_cast<std::uint32_t>(static_cast<std::uint16_t>(eg))
    );
}

constexpr int mg_value(Score s) noexcept {
    const auto u = static_cast<std::uint32_t>(s);
    return static_cast<std::int16_t>(u >> 16);
}

constexpr int eg_value(Score s) noexcept {
    const auto u = static_cast<std::uint32_t>(s);
    return static_cast<std::int16_t>(u & 0xFFFFu);
}

#define S(mg, eg) make_score((mg), (eg))

constexpr int blend(Score s, int phase) noexcept {
    return (mg_value(s) * phase + eg_value(s) * (24 - phase)) / 24;
}

inline int popcount(Bitboard bb) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(bb);
#else
    int cnt = 0;
    while (bb) {
        bb &= (bb - 1);
        ++cnt;
    }
    return cnt;
#endif
}

inline Square pop_lsb(Bitboard& bb) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    Square sq = __builtin_ctzll(bb);
#else
    Square sq = 0;
    while (((bb >> sq) & 1ULL) == 0ULL) ++sq;
#endif
    bb &= bb - 1;
    return sq;
}

constexpr Square flip_vertical(Square sq) noexcept {
    return sq ^ 56;
}

constexpr Square relative_square(Color c, Square sq) noexcept {
    return c == WHITE ? sq : flip_vertical(sq);
}

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_B = FILE_A << 1;
constexpr Bitboard FILE_C = FILE_A << 2;
constexpr Bitboard FILE_D = FILE_A << 3;
constexpr Bitboard FILE_E = FILE_A << 4;
constexpr Bitboard FILE_F = FILE_A << 5;
constexpr Bitboard FILE_G = FILE_A << 6;
constexpr Bitboard FILE_H = FILE_A << 7;

constexpr Bitboard file_bb(int f) noexcept {
    return FILE_A << f;
}

constexpr Score PieceValue[PIECE_TYPE_NB] = {
    S(  82, 144),  // PAWN
    S( 337, 281),  // KNIGHT
    S( 365, 297),  // BISHOP
    S( 477, 512),  // ROOK
    S(1025, 936),  // QUEEN
    S(   0,   0)   // KING
};

constexpr int PhaseValue[PIECE_TYPE_NB] = {
    0, // PAWN
    1, // KNIGHT
    1, // BISHOP
    2, // ROOK
    4, // QUEEN
    0  // KING
};

constexpr int TOTAL_PHASE = 24;

constexpr Score PawnPST[SQ_NB] = {
    S(  0,   0), S(  0,   0), S(  0,   0), S(  0,   0), S(  0,   0), S(  0,   0), S(  0,   0), S(  0,   0),
    S( 98, 178), S(134, 173), S( 61, 158), S( 95, 134), S( 68, 147), S(126, 132), S( 34, 165), S(-11, 187),
    S( -6,  94), S(  7, 100), S( 26,  85), S( 31,  67), S( 65,  56), S( 56,  53), S( 25,  82), S(-20,  84),
    S(-14,  32), S( 13,  24), S(  6,  13), S( 21,   5), S( 23,  -2), S( 12,   4), S( 17,  17), S(-23,  17),
    S(-27,  13), S( -2,   9), S( -5,  -3), S( 12,  -7), S( 17,  -7), S(  6,  -8), S( 10,   3), S(-25,  -1),
    S(-26,   4), S( -4,   7), S( -4,  -6), S(-10,   1), S(  3,   0), S(  3,  -5), S( 33,  -1), S(-12,  -8),
    S(-35,  13), S( -1,   8), S(-20,   8), S(-23,  10), S(-15,  13), S( 24,   0), S( 38,   2), S(-22,  -7),
    S(  0,   0), S(  0,   0), S(  0,   0), S(  0,   0), S(  0,   0), S(  0,   0), S(  0,   0), S(  0,   0)
};

constexpr Score KnightPST[SQ_NB] = {
    S(-160, -70), S(-100, -40), S( -70, -20), S( -50, -10), S( -50, -10), S( -70, -20), S(-100, -40), S(-160, -70),
    S( -90, -35), S( -20, -10), S(   0,   0), S(  10,   8), S(  10,   8), S(   0,   0), S( -20, -10), S( -90, -35),
    S( -45, -15), S(  10,   4), S(  35,  12), S(  45,  18), S(  45,  18), S(  35,  12), S(  10,   4), S( -45, -15),
    S( -25,  -8), S(  20,   8), S(  45,  18), S(  60,  24), S(  60,  24), S(  45,  18), S(  20,   8), S( -25,  -8),
    S( -25,  -8), S(  20,   8), S(  45,  18), S(  60,  24), S(  60,  24), S(  45,  18), S(  20,   8), S( -25,  -8),
    S( -45, -15), S(  10,   4), S(  35,  12), S(  45,  18), S(  45,  18), S(  35,  12), S(  10,   4), S( -45, -15),
    S( -90, -35), S( -20, -10), S(   0,   0), S(  10,   8), S(  10,   8), S(   0,   0), S( -20, -10), S( -90, -35),
    S(-160, -70), S(-100, -40), S( -70, -20), S( -50, -10), S( -50, -10), S( -70, -20), S(-100, -40), S(-160, -70)
};

constexpr Score BishopPST[SQ_NB] = {
    S(-29, -14), S(  4, -21), S(-82, -11), S(-37,  -8), S(-25,  -7), S(-42,  -9), S(  7, -17), S( -8, -24),
    S(-26,  -8), S( 16,  -4), S(-18,   7), S(-13, -12), S( 30,  -3), S( 59, -13), S( 18,  -4), S(-47, -14),
    S(-16,   2), S( 37,  -8), S( 43,   0), S( 40,  -1), S( 35,  -2), S( 50,   6), S( 37,   0), S( -2,   4),
    S( -4,  -3), S(  5,   9), S( 19,  12), S( 50,   9), S( 37,  14), S( 37,  10), S(  7,   3), S( -2,   2),
    S( -6,  -6), S( 13,   3), S( 13,  13), S( 26,  19), S( 34,   7), S( 12,  10), S( 10,  -3), S(  4,  -9),
    S(  0, -12), S( 15,  -3), S( 15,   8), S( 15,  10), S( 14,  13), S( 27,   3), S( 18,  -7), S( 10, -15),
    S(  4, -14), S( 15, -18), S( 16,  -7), S(  0,  -1), S(  7,   4), S( 21,  -9), S( 33, -15), S(  1, -27),
    S(-33, -23), S( -3,  -9), S(-14, -23), S(-21,  -5), S(-13,  -9), S(-12, -16), S(-39,  -5), S(-21, -17)
};

constexpr Score RookPST[SQ_NB] = {
    // Rank 8 - 底线突击位
    S( 32,  13), S( 42,  10), S( 32,  18), S( 51,  15), S( 63,  12), S(  9,  12), S( 31,   8), S( 43,   5),
    S( 27,  11), S( 32,  13), S( 58,  13), S( 62,  11), S( 80,  -3), S( 67,   3), S( 26,   8), S( 44,   3),
    S( -5,   7), S( 19,   7), S( 26,   7), S( 36,   5), S( 17,   4), S( 45,  -3), S( 61,  -5), S( 16,  -3),
    S(-24,   4), S(-11,   3), S(  7,  13), S( 26,   1), S( 24,   2), S( 35,   1), S( -8,  -1), S(-20,   2),
    S(-36,   3), S(-26,   5), S(-12,   8), S( -1,   4), S(  9,  -5), S( -7,  -6), S(  6,  -8), S(-23, -11),
    S(-45,  -4), S(-25,   0), S(-16,  -5), S(-17,  -1), S(  3,  -7), S(  0, -12), S( -5,  -8), S(-33, -16),
    S(-44,  -6), S(-16,  -6), S(-20,   0), S( -9,   2), S( -1,  -9), S( 11,  -9), S( -6, -11), S(-71,  -3),
    S(-19,  -9), S(-13,   2), S(  1,   3), S( 17,  -1), S( 16,  -5), S(  7, -13), S(-37,   4), S(-26, -20)
};

constexpr Score QueenPST[SQ_NB] = {
    S(-28,  -9), S(  0,  22), S( 29,  22), S( 12,  27), S( 59,  27), S( 44,  19), S( 43,  10), S( 45,  20),
    S(-24, -17), S(-39,  20), S( -5,  32), S(  1,  41), S(-16,  58), S( 57,  25), S( 28,  30), S( 54,   0),
    S(-13, -20), S(-17,   6), S(  7,   9), S(  8,  49), S( 29,  47), S( 56,  35), S( 47,  19), S( 57,   9),
    S(-27,   3), S(-27,  22), S(-16,  24), S(-16,  45), S( -1,  57), S( 17,  40), S( -2,  57), S(  1,  36),
    S( -9, -18), S(-26,  28), S( -9,  19), S(-10,  47), S( -2,  31), S( -4,  34), S(  3,  39), S( -3,  23),
    S(-14, -16), S(  2, -27), S(-11,  15), S( -2,   6), S( -5,   9), S(  2,  17), S( 14,  10), S(  5,   5),
    S(-35, -22), S( -8, -23), S( 11, -30), S(  2, -16), S(  8, -16), S( 15, -23), S( -3, -36), S(  1, -32),
    S( -1, -33), S(-18, -28), S( -9, -22), S( 10, -43), S(-15,  -5), S(-25, -32), S(-31, -20), S(-50, -41)
};

constexpr Score KingPST[SQ_NB] = {
    S(  24, -50), S(  30, -30), S(  12, -20), S(   0, -10), S(   0, -10), S(  12, -20), S(  30, -30), S(  24, -50),
    S(  20, -30), S(  18, -10), S(   0,   0), S(   0,   8), S(   0,   8), S(   0,   0), S(  18, -10), S(  20, -30),
    S(  -8, -10), S( -16,  10), S( -16,  18), S( -20,  24), S( -20,  24), S( -16,  18), S( -16,  10), S(  -8, -10),
    S( -18, -10), S( -26,  18), S( -26,  26), S( -34,  34), S( -34,  34), S( -26,  26), S( -26,  18), S( -18, -10),
    S( -28, -14), S( -36,  18), S( -36,  28), S( -44,  38), S( -44,  38), S( -36,  28), S( -36,  18), S( -28, -14),
    S( -28, -18), S( -36,  10), S( -36,  20), S( -44,  30), S( -44,  30), S( -36,  20), S( -36,  10), S( -28, -18),
    S( -28, -22), S( -36,   0), S( -36,  10), S( -44,  20), S( -44,  20), S( -36,  10), S( -36,   0), S( -28, -22),
    S( -28, -40), S( -36, -12), S( -36,   0), S( -44,  10), S( -44,  10), S( -36,   0), S( -36, -12), S( -28, -40)
};

constexpr const Score* PST[PIECE_TYPE_NB] = {
    PawnPST, KnightPST, BishopPST, RookPST, QueenPST, KingPST
};

constexpr Score BishopPairBonus   = S(30, 50);
constexpr Score TempoBonus        = S(12,  8);

constexpr Score RookOpenFileBonus     = S(24, 10);
constexpr Score RookSemiOpenFileBonus = S(12,  6);

constexpr Score PassedBonusByRank[8] = {
    S(  0,   0),
    S(  0,   0),
    S( 10,  20),
    S( 18,  36),
    S( 32,  60),
    S( 56, 100),
    S( 90, 160),
    S(  0,   0)
};

constexpr Score ProtectedPassedBonusByRank[8] = {
    S( 0,  0),
    S( 0,  0),
    S( 4,  8),
    S( 8, 14),
    S(12, 24),
    S(20, 36),
    S(30, 50),
    S( 0,  0)
};

constexpr Score ConnectedPassedBonusByRank[8] = {
    S( 0,  0),
    S( 0,  0),
    S( 4,  6),
    S( 8, 12),
    S(14, 22),
    S(24, 36),
    S(36, 60),
    S( 0,  0)
};

constexpr Score CandidatePassedBonusByRank[8] = {
    S( 0,  0),
    S( 0,  0),
    S( 4,  6),
    S( 8, 12),
    S(12, 18),
    S(18, 26),
    S(24, 36),
    S( 0,  0)
};

inline Score pst_value(PieceType pt, Square sq, Color c) noexcept {
    return PST[pt][relative_square(c, sq)];
}

inline int game_phase(const Position& pos) noexcept {
    int phase = TOTAL_PHASE;

    phase -= popcount(pieces(pos, WHITE, QUEEN))  * PhaseValue[QUEEN];
    phase -= popcount(pieces(pos, BLACK, QUEEN))  * PhaseValue[QUEEN];
    phase -= popcount(pieces(pos, WHITE, ROOK))   * PhaseValue[ROOK];
    phase -= popcount(pieces(pos, BLACK, ROOK))   * PhaseValue[ROOK];
    phase -= popcount(pieces(pos, WHITE, BISHOP)) * PhaseValue[BISHOP];
    phase -= popcount(pieces(pos, BLACK, BISHOP)) * PhaseValue[BISHOP];
    phase -= popcount(pieces(pos, WHITE, KNIGHT)) * PhaseValue[KNIGHT];
    phase -= popcount(pieces(pos, BLACK, KNIGHT)) * PhaseValue[KNIGHT];

    if (phase < 0) phase = 0;
    if (phase > TOTAL_PHASE) phase = TOTAL_PHASE;
    return phase;
}

inline Score eval_piece_psqt(const Position& pos, Color c, PieceType pt) noexcept {
    Score score = S(0, 0);
    Bitboard bb = pieces(pos, c, pt);

    while (bb) {
        const Square sq = pop_lsb(bb);
        score += PieceValue[pt] + pst_value(pt, sq, c);
    }

    return score;
}

inline Score material_and_pst_side(const Position& pos, Color c) noexcept {
    Score score = S(0, 0);
    score += eval_piece_psqt(pos, c, PAWN);
    score += eval_piece_psqt(pos, c, KNIGHT);
    score += eval_piece_psqt(pos, c, BISHOP);
    score += eval_piece_psqt(pos, c, ROOK);
    score += eval_piece_psqt(pos, c, QUEEN);
    score += eval_piece_psqt(pos, c, KING);
    return score;
}

inline Score material_and_pst(const Position& pos) noexcept {
    return material_and_pst_side(pos, WHITE)
         - material_and_pst_side(pos, BLACK);
}

inline Score bishop_pair(const Position& pos) noexcept {
    Score score = S(0, 0);

    if (popcount(pieces(pos, WHITE, BISHOP)) >= 2) score += BishopPairBonus;
    if (popcount(pieces(pos, BLACK, BISHOP)) >= 2) score -= BishopPairBonus;

    return score;
}

inline bool is_passed_pawn(const Position& pos, Color c, Square sq) noexcept {
    const int f = file_of(sq);
    const int r = rank_of(sq);

    Bitboard enemyPawns = pieces(pos, Color(c ^ 1), PAWN);

    while (enemyPawns) {
        Square esq = pop_lsb(enemyPawns);
        int ef = file_of(esq);
        int er = rank_of(esq);

        if (ef < f - 1 || ef > f + 1) continue;

        if (c == WHITE) {
            if (er > r) return false;
        } else {
            if (er < r) return false;
        }
    }

    return true;
}

inline bool is_candidate_passed_pawn(const Position& pos, Color c, Square sq) noexcept {
    const int f = file_of(sq);
    const int r = rank_of(sq);

    int friendlySupport = 0;
    int enemyBlockers   = 0;

    Bitboard friendlyPawns = pieces(pos, c, PAWN);
    Bitboard enemyPawns    = pieces(pos, Color(c ^ 1), PAWN);

    while (friendlyPawns) {
        Square fsq = pop_lsb(friendlyPawns);
        if (fsq == sq) continue;

        int ff = file_of(fsq);
        int fr = rank_of(fsq);

        if (ff < f - 1 || ff > f + 1) continue;

        if (c == WHITE) {
            if (fr >= r) ++friendlySupport;
        } else {
            if (fr <= r) ++friendlySupport;
        }
    }

    while (enemyPawns) {
        Square esq = pop_lsb(enemyPawns);
        int ef = file_of(esq);
        int er = rank_of(esq);

        if (ef < f - 1 || ef > f + 1) continue;

        if (c == WHITE) {
            if (er > r) ++enemyBlockers;
        } else {
            if (er < r) ++enemyBlockers;
        }
    }

    return enemyBlockers > 0 && friendlySupport >= enemyBlockers;
}

inline bool is_protected_by_pawn(const Position& pos, Color c, Square sq) noexcept {
    int f = file_of(sq);
    int r = rank_of(sq);

    if (c == WHITE) {
        if (f > 0 && r > 0 && piece_on(pos, sq - 9) == W_PAWN) return true;
        if (f < 7 && r > 0 && piece_on(pos, sq - 7) == W_PAWN) return true;
    } else {
        if (f > 0 && r < 7 && piece_on(pos, sq + 7) == B_PAWN) return true;
        if (f < 7 && r < 7 && piece_on(pos, sq + 9) == B_PAWN) return true;
    }

    return false;
}

inline bool has_adjacent_friendly_pawn(const Position& pos, Color c, Square sq) noexcept {
    int f = file_of(sq);
    int r = rank_of(sq);

    Bitboard pawns = pieces(pos, c, PAWN);
    while (pawns) {
        Square psq = pop_lsb(pawns);
        if (psq == sq) continue;
        if (rank_of(psq) != r) continue;
        if (file_of(psq) == f - 1 || file_of(psq) == f + 1) return true;
    }

    return false;
}

inline Score passed_pawns(const Position& pos) noexcept {
    Score score = S(0, 0);

    for (Color c : {WHITE, BLACK}) {
        Bitboard pawns = pieces(pos, c, PAWN);

        while (pawns) {
            Square sq = pop_lsb(pawns);

            if (is_passed_pawn(pos, c, sq)) {
                int rr = rank_of(relative_square(c, sq));
                Score bonus = PassedBonusByRank[rr];

                if (is_protected_by_pawn(pos, c, sq))
                    bonus += ProtectedPassedBonusByRank[rr];

                if (has_adjacent_friendly_pawn(pos, c, sq))
                    bonus += ConnectedPassedBonusByRank[rr];

                score += (c == WHITE ? bonus : -bonus);
            } else if (is_candidate_passed_pawn(pos, c, sq)) {
                int rr = rank_of(relative_square(c, sq));
                Score bonus = CandidatePassedBonusByRank[rr];
                score += (c == WHITE ? bonus : -bonus);
            }
        }
    }

    return score;
}

inline bool has_friendly_pawn_on_file(const Position& pos, Color c, int f) noexcept {
    return (pieces(pos, c, PAWN) & file_bb(f)) != 0;
}

inline Score rook_file_activity(const Position& pos) noexcept {
    Score score = S(0, 0);

    for (Color c : {WHITE, BLACK}) {
        Bitboard rooks = pieces(pos, c, ROOK);

        while (rooks) {
            Square sq = pop_lsb(rooks);
            int f = file_of(sq);

            bool ownPawn   = has_friendly_pawn_on_file(pos, c, f);
            bool enemyPawn = has_friendly_pawn_on_file(pos, Color(c ^ 1), f);

            Score bonus = S(0, 0);
            if (!ownPawn && !enemyPawn) bonus = RookOpenFileBonus;
            else if (!ownPawn)          bonus = RookSemiOpenFileBonus;

            score += (c == WHITE ? bonus : -bonus);
        }
    }

    return score;
}

inline Score tempo(const Position& pos) noexcept {
    return pos.side_to_move == WHITE ? TempoBonus : -TempoBonus;
}

inline Score raw_eval_white_pov(const Position& pos) noexcept {
    Score score = S(0, 0);

    score += material_and_pst(pos);
    score += bishop_pair(pos);
    score += passed_pawns(pos);
    score += rook_file_activity(pos);
    score += tempo(pos);

    return score;
}

} // namespace

Score evaluate(const Position& pos) noexcept {
    const Score raw = raw_eval_white_pov(pos);
    const int phase = game_phase(pos);
    const int whitePov = blend(raw, phase);
    return pos.side_to_move == WHITE ? whitePov : -whitePov;
}

} // namespace valerain::eval
