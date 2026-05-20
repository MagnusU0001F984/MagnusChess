/*
MIT License

Copyright (c) 2026 MagnusU0001F984

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

#include <iostream>
#include <string_view>

#include "../src/Attack.h"
#include "../src/Common.h"
#include "../src/Memory.h"
#include "../src/MoveGen.h"
#include "../src/Perft.h"

namespace {

using namespace magnus;

struct TestContext {
    memory::Memory mem{};

    TestContext() {
        memory::memory_init(mem, 16, 8, 2);
        attack_init_backend(mem);
    }

    ~TestContext() {
        memory::memory_free(mem);
    }
};

[[nodiscard]] bool load_fen(
    const TestContext& ctx,
    Position& pos,
    std::string_view fen
) noexcept {
    return parse_fen(pos, ctx.mem, fen);
}

[[nodiscard]] bool has_move(const MoveList& list, std::string_view uci) noexcept {
    for (int i = 0; i < list.size; ++i) {
        if (move_matches_uci(list.moves[i], uci))
            return true;
    }
    return false;
}

bool expect_true(bool condition, const char* label) {
    if (condition)
        return true;

    std::cerr << "FAIL: " << label << '\n';
    return false;
}

bool expect_move(
    const TestContext& ctx,
    std::string_view fen,
    std::string_view uci,
    const char* label
) {
    Position pos{};
    if (!load_fen(ctx, pos, fen))
        return expect_true(false, "parse FEN");

    MoveList legal{};
    generate_legal(pos, ctx.mem, legal);
    return expect_true(has_move(legal, uci), label);
}

bool expect_no_move(
    const TestContext& ctx,
    std::string_view fen,
    std::string_view uci,
    const char* label
) {
    Position pos{};
    if (!load_fen(ctx, pos, fen))
        return expect_true(false, "parse FEN");

    MoveList legal{};
    generate_legal(pos, ctx.mem, legal);
    return expect_true(!has_move(legal, uci), label);
}

bool expect_perft(
    const TestContext& ctx,
    std::string_view fen,
    int depth,
    NodeCount expected,
    const char* label
) {
    Position pos{};
    if (!load_fen(ctx, pos, fen))
        return expect_true(false, "parse FEN");

    const NodeCount got = perft(pos, ctx.mem, depth);
    if (got == expected)
        return true;

    std::cerr << "FAIL: " << label
              << " expected " << expected
              << " got " << got << '\n';
    return false;
}

bool test_line_geometry(const TestContext& ctx) {
    constexpr Square e1 = 4;
    constexpr Square e2 = 12;
    constexpr Square e8 = 60;
    constexpr Square d2 = 11;
    constexpr Square c3 = 18;
    constexpr Square a5 = 32;

    bool ok = true;
    ok &= expect_true(
        (line_bb(ctx.mem, e1, e2) & bb_of(e8)) != 0ULL,
        "line_bb(e1,e2) includes e8 pinner direction"
    );
    ok &= expect_true(
        (line_bb(ctx.mem, e1, c3) & bb_of(a5)) != 0ULL,
        "line_bb(e1,c3) includes full diagonal endpoint"
    );
    ok &= expect_true(
        (between_bb(ctx.mem, e1, c3) & bb_of(d2)) != 0ULL,
        "between_bb(e1,c3) keeps segment interior"
    );
    ok &= expect_true(
        (between_bb(ctx.mem, e1, c3) & (bb_of(e1) | bb_of(c3))) == 0ULL,
        "between_bb(e1,c3) excludes endpoints"
    );
    return ok;
}

} // namespace

int main() {
    TestContext ctx{};
    bool ok = true;

    ok &= test_line_geometry(ctx);

    ok &= expect_move(
        ctx,
        "k3r3/8/8/8/8/8/4R3/4K3 w - - 0 1",
        "e2e8",
        "pinned rook can capture pinner"
    );
    ok &= expect_no_move(
        ctx,
        "k3r3/8/8/8/8/8/4R3/4K3 w - - 0 1",
        "e2d2",
        "pinned rook cannot move off pin line"
    );
    ok &= expect_move(
        ctx,
        "k3r3/8/8/7B/8/8/8/4K3 w - - 0 1",
        "h5e8",
        "single-check evasion can capture checking slider"
    );
    ok &= expect_no_move(
        ctx,
        "k3r3/8/8/8/8/8/4R1n1/4K3 w - - 0 1",
        "e2g2",
        "pinned rook cannot capture off-line knight checker"
    );

    ok &= expect_perft(
        ctx,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        2,
        2039,
        "kiwipete pinned/castling perft d2"
    );
    ok &= expect_perft(
        ctx,
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        3,
        89890,
        "pinned middlegame perft d3"
    );
    ok &= expect_perft(
        ctx,
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        3,
        2812,
        "tricky pin/ep perft d3"
    );

    if (!ok)
        return 1;

    std::cout << "MoveGen pin regression tests passed\n";
    return 0;
}
