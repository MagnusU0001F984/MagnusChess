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

#include "Uci.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "Attack.h"
#include "Bench.h"
#include "Memory.h"
#include "MoveGen.h"
#include "Nnue.h"
#include "Search.h"

/*
This file implements the engine's minimal UCI front-end. It parses commands,
builds positions from startpos/FEN input, manages hash and NNUE options, and
derives practical search limits from the UCI go command.
*/

namespace valerain {

namespace {

constexpr int DEFAULT_UCI_DEPTH = 8;

[[nodiscard]] constexpr const char* go_usage_hint() noexcept {
    return "go <depth/movetime/nodes>";
}

[[nodiscard]] constexpr const char* go_usage_examples() noexcept {
    return "examples: go depth 8 | go movetime 1000 | go nodes 50000 | go wtime 15000 btime 15000";
}

[[nodiscard]] constexpr const char* perft_usage_hint() noexcept {
    return "perft <depth> <threads>";
}

[[nodiscard]] constexpr const char* divide_usage_hint() noexcept {
    return "divide <depth> <threads> [live]";
}

[[nodiscard]] std::string default_eval_file() {
    // Try a few common in-tree NNUE locations so the engine can work out of the box.
    constexpr const char* candidates[] = {
        "NnueFile/nn-2a5d6101d177.nnue",
        "src/NnueFile/nn-2a5d6101d177.nnue",
        "NnueFile/nn-37f18f62d772.nnue",
        "src/NnueFile/nn-37f18f62d772.nnue"
    };

    for (const char* candidate : candidates)
        if (std::filesystem::exists(candidate))
            return candidate;

    return candidates[0];
}

[[nodiscard]] bool parse_int(std::string_view sv, int& value) noexcept {
    const char* first = sv.data();
    const char* last = sv.data() + sv.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    return ec == std::errc{} && ptr == last;
}

[[nodiscard]] bool parse_u64(std::string_view sv, u64& value) noexcept {
    const char* first = sv.data();
    const char* last = sv.data() + sv.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    return ec == std::errc{} && ptr == last;
}

[[nodiscard]] bool parse_bool(std::string_view sv, bool& value) noexcept {
    if (sv == "true" || sv == "1") {
        value = true;
        return true;
    }

    if (sv == "false" || sv == "0") {
        value = false;
        return true;
    }

    return false;
}

[[nodiscard]] bool parse_square(std::string_view sv, Square& sq) noexcept {
    if (sv.size() != 2)
        return false;

    const char file = sv[0];
    const char rank = sv[1];
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8')
        return false;

    sq = static_cast<Square>((rank - '1') * 8 + (file - 'a'));
    return true;
}

[[nodiscard]] bool parse_piece_char(
    char c,
    Color& color,
    PieceType& piece_type
) noexcept {
    // FEN pieces use case to encode color and the letter to encode piece type.
    color = std::isupper(static_cast<unsigned char>(c)) ? WHITE : BLACK;

    switch (static_cast<char>(std::tolower(static_cast<unsigned char>(c)))) {
        case 'p': piece_type = PAWN; return true;
        case 'n': piece_type = KNIGHT; return true;
        case 'b': piece_type = BISHOP; return true;
        case 'r': piece_type = ROOK; return true;
        case 'q': piece_type = QUEEN; return true;
        case 'k': piece_type = KING; return true;
        default: return false;
    }
}

[[nodiscard]] bool move_matches_uci(Move move, std::string_view token) noexcept {
    if (token.size() != 4 && token.size() != 5)
        return false;

    if (token[0] != static_cast<char>('a' + file_of(from_sq(move))) ||
        token[1] != static_cast<char>('1' + rank_of(from_sq(move))) ||
        token[2] != static_cast<char>('a' + file_of(to_sq(move))) ||
        token[3] != static_cast<char>('1' + rank_of(to_sq(move)))) {
        return false;
    }

    if (!move_is_promotion(move))
        return token.size() == 4;

    if (token.size() != 5)
        return false;

    switch (promo_piece(move)) {
        case KNIGHT: return token[4] == 'n';
        case BISHOP: return token[4] == 'b';
        case ROOK:   return token[4] == 'r';
        case QUEEN:  return token[4] == 'q';
        default:     return false;
    }
}

[[nodiscard]] bool find_uci_move(
    const Position& pos,
    const memory::Memory& mem,
    std::string_view token,
    Move& move
) noexcept {
    // UCI moves are matched by generating legal moves and comparing the text form.
    MoveList list{};
    generate_legal(pos, mem, list);

    for (int i = 0; i < list.size; ++i) {
        if (move_matches_uci(list.moves[i], token)) {
            move = list.moves[i];
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool parse_fen(
    Position& pos,
    const memory::Memory& mem,
    const std::string& fen
) noexcept {
    // FEN parsing rebuilds the position through the regular piece-placement API
    // so mailbox, bitboards, eval caches, and king-square caches all stay aligned.
    std::istringstream iss(fen);

    std::string board_part;
    std::string stm_part;
    std::string castling_part;
    std::string ep_part;
    std::string halfmove_part = "0";
    std::string fullmove_part = "1";

    if (!(iss >> board_part >> stm_part >> castling_part >> ep_part))
        return false;

    iss >> halfmove_part >> fullmove_part;

    position_clear(pos);

    int rank = 7;
    int file = 0;

    for (char c : board_part) {
        if (c == '/') {
            if (file != 8 || rank == 0)
                return false;

            --rank;
            file = 0;
            continue;
        }

        if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8)
                return false;
            continue;
        }

        Color color = WHITE;
        PieceType piece_type = PAWN;
        if (!parse_piece_char(c, color, piece_type) || file >= 8)
            return false;

        position_put_piece(pos, color, piece_type, rank * 8 + file);
        ++file;
    }

    if (rank != 0 || file != 8)
        return false;

    if (stm_part == "w") pos.side_to_move = WHITE;
    else if (stm_part == "b") pos.side_to_move = BLACK;
    else return false;

    pos.castling_rights = NO_CASTLING;
    if (castling_part != "-") {
        for (char c : castling_part) {
            switch (c) {
                case 'K': pos.castling_rights |= WHITE_OO; break;
                case 'Q': pos.castling_rights |= WHITE_OOO; break;
                case 'k': pos.castling_rights |= BLACK_OO; break;
                case 'q': pos.castling_rights |= BLACK_OOO; break;
                default: return false;
            }
        }
    }

    pos.ep_sq = NO_SQ;
    if (ep_part != "-" && !parse_square(ep_part, pos.ep_sq))
        return false;

    if (!parse_int(halfmove_part, pos.halfmove_clock) || pos.halfmove_clock < 0)
        return false;

    if (!parse_int(fullmove_part, pos.fullmove_number) || pos.fullmove_number <= 0)
        return false;

    position_refresh_key(pos, mem.tables);
    return position_has_valid_kings(pos) && position_board_matches_bitboards(pos);
}

[[nodiscard]] bool apply_move_list(
    Position& pos,
    const memory::Memory& mem,
    std::istringstream& iss
) noexcept {
    std::string move_token;
    while (iss >> move_token) {
        Move move = 0;
        if (!find_uci_move(pos, mem, move_token, move))
            return false;

        do_move_copy(pos, move, mem.tables);
    }

    return true;
}

[[nodiscard]] bool set_position_from_command(
    Position& pos,
    const memory::Memory& mem,
    std::string_view command
) noexcept {
    std::istringstream iss{std::string(command)};
    std::string token;

    if (!(iss >> token))
        return false;

    if (token == "startpos") {
        set_start_position(pos);
        position_refresh_key(pos, mem.tables);
    } else if (token == "fen") {
        std::string fen;
        std::string part;

        for (int i = 0; i < 6; ++i) {
            if (!(iss >> part))
                return false;

            if (i != 0)
                fen.push_back(' ');
            fen += part;
        }

        if (!parse_fen(pos, mem, fen))
            return false;
    } else {
        return false;
    }

    if (!(iss >> token))
        return true;

    if (token != "moves")
        return false;

    return apply_move_list(pos, mem, iss);
}

[[nodiscard]] bool ensure_nnue_loaded(
    const std::string& eval_file,
    std::ostream* out
) {
    // Load lazily so HCE remains usable even when no NNUE file is configured.
    if (nnue::loaded() && nnue::path() == eval_file)
        return true;

    if (nnue::load(eval_file)) {
        if (out)
            *out << "info string loaded nnue " << eval_file << '\n';
        return true;
    }

    if (out)
        *out << "info string failed to load nnue: " << nnue::last_error() << '\n';
    return false;
}

[[nodiscard]] const char* active_eval_name(bool use_nnue) noexcept {
    return use_nnue && nnue::loaded() ? "nnue" : "hce";
}

void handle_setoption(
    memory::Memory& mem,
    bool& use_nnue,
    std::string& eval_file,
    std::ostream& out,
    std::string_view command
) {
    // Option parsing is intentionally small: hash size, hash clear, NNUE toggle,
    // and NNUE file path are all that the engine currently exposes.
    std::istringstream iss{std::string(command)};
    std::string token;
    std::string name;
    std::string value;

    iss >> token; // setoption
    iss >> token; // name

    while (iss >> token) {
        if (token == "value")
            break;

        if (!name.empty())
            name.push_back(' ');
        name += token;
    }

    std::getline(iss, value);
    if (!value.empty() && value.front() == ' ')
        value.erase(value.begin());

    if (name == "Hash") {
        int mb = 0;
        if (parse_int(value, mb) && mb > 0)
            memory::tt_resize_mb(mem.tt, static_cast<std::size_t>(mb));
    }
    else if (name == "Clear Hash") {
        memory::memory_clear_hash(mem);
    }
    else if (name == "UseNNUE") {
        bool parsed = false;
        if (parse_bool(value, parsed)) {
            if (use_nnue != parsed)
                memory::memory_clear_hash(mem);

            use_nnue = parsed;
            if (use_nnue && !ensure_nnue_loaded(eval_file, &out))
                out << "info string nnue unavailable, search will fall back to hce\n";
        }
    }
    else if (name == "EvalFile") {
        if (!value.empty()) {
            eval_file = value;
            memory::memory_clear_hash(mem);

            if (use_nnue && !ensure_nnue_loaded(eval_file, &out))
                out << "info string nnue unavailable, search will fall back to hce\n";
        }
    }
}

[[nodiscard]] bool parse_go_command(
    const Position& pos,
    std::string_view command,
    search::SearchLimits& limits
) noexcept {
    // Convert the UCI go command into depth/node/time limits understood by the
    // synchronous search loop.
    std::istringstream iss{std::string(command)};
    std::string token;
    limits.depth = search::MAX_PLY;
    limits.node_limit = 0;
    limits.soft_time_ms = 0;
    limits.hard_time_ms = 0;
    limits.infinite = false;

    int depth = 0;
    int movetime = 0;
    int wtime = 0;
    int btime = 0;
    int winc = 0;
    int binc = 0;
    int movestogo = 0;
    u64 nodes = 0;
    bool has_limit = false;

    iss >> token; // go

    while (iss >> token) {
        if (token == "depth") {
            std::string value;
            if (iss >> value) {
                int parsed = 0;
                if (parse_int(value, parsed) && parsed > 0) {
                    depth = parsed;
                    has_limit = true;
                }
                else
                    return false;
            }
            else
                return false;
        }
        else if (token == "nodes") {
            std::string value;
            if (iss >> value && parse_u64(value, nodes) && nodes > 0)
                has_limit = true;
            else
                return false;
        }
        else if (token == "movetime") {
            std::string value;
            if (iss >> value && parse_int(value, movetime) && movetime > 0)
                has_limit = true;
            else
                return false;
        }
        else if (token == "wtime") {
            std::string value;
            if (iss >> value && parse_int(value, wtime))
                has_limit = true;
            else
                return false;
        }
        else if (token == "btime") {
            std::string value;
            if (iss >> value && parse_int(value, btime))
                has_limit = true;
            else
                return false;
        }
        else if (token == "winc") {
            std::string value;
            if (iss >> value && parse_int(value, winc))
                has_limit = true;
            else
                return false;
        }
        else if (token == "binc") {
            std::string value;
            if (iss >> value && parse_int(value, binc))
                has_limit = true;
            else
                return false;
        }
        else if (token == "movestogo") {
            std::string value;
            if (iss >> value && parse_int(value, movestogo))
                has_limit = true;
            else
                return false;
        }
        else if (token == "infinite") {
            limits.infinite = true;
            has_limit = true;
        }
    }

    if (!has_limit)
        return false;

    if (depth > 0)
        limits.depth = depth;

    limits.node_limit = nodes;

    if (movetime > 0) {
        limits.soft_time_ms = movetime;
        limits.hard_time_ms = movetime;
        return true;
    }

    const int remaining = pos.side_to_move == WHITE ? wtime : btime;
    const int increment = pos.side_to_move == WHITE ? winc : binc;

    if (!limits.infinite && remaining > 0) {
        const int move_number = std::max(1, pos.fullmove_number);
        const bool sudden_death = movestogo == 0 && increment == 0;

        // Early moves are kept intentionally cheaper, especially in sudden-death
        // time controls, while later moves are allowed to spend more time.
        int phase_scale = 100;
        if (move_number <= 10)      phase_scale = sudden_death ? 50 : 60;
        else if (move_number <= 20) phase_scale = sudden_death ? 65 : 75;
        else if (move_number <= 35) phase_scale = sudden_death ? 80 : 90;
        else if (move_number <= 50) phase_scale = sudden_death ? 100 : 105;
        else                        phase_scale = sudden_death ? 115 : 120;

        const int mtg =
            movestogo > 0 ? movestogo :
            sudden_death
                ? (move_number <= 10 ? 36 :
                   move_number <= 20 ? 30 :
                   move_number <= 35 ? 24 : 18)
                : 24;

        const int reserve_div =
            sudden_death
                ? (move_number <= 10 ? 8 :
                   move_number <= 20 ? 10 :
                   move_number <= 35 ? 12 : 16)
                : (phase_scale <= 75 ? 12 :
                   phase_scale <= 90 ? 16 : 24);
        const int reserve_cap =
            sudden_death
                ? std::max(50, remaining / 3)
                : (phase_scale >= 105 ? 140 : 220);
        const int reserve_floor = sudden_death ? 50 : 10;
        const int reserve = std::clamp(remaining / reserve_div, reserve_floor, reserve_cap);
        const int usable = std::max(1, remaining - reserve);

        const int base = usable / std::max(1, mtg);
        const int inc_share = (increment * (phase_scale + 20)) / 200;

        int soft = (base * phase_scale) / 100 + inc_share;
        soft += usable / std::max(sudden_death ? 160 : 96, mtg * (sudden_death ? 12 : 8));

        const int soft_cap =
            sudden_death
                ? (phase_scale <= 65 ? usable / 6 :
                   phase_scale <= 80 ? usable / 5 :
                   usable / 4)
                : (phase_scale <= 75 ? usable / 4 :
                   phase_scale <= 90 ? usable / 3 :
                   (usable * 2) / 5);
        soft = std::max(1, std::min(soft, soft_cap));

        int hard = std::min(
            usable,
            std::max(
                soft + base * (sudden_death ? 1 : 2),
                soft * (sudden_death ? 2 : (phase_scale >= 105 ? 3 : 2))
            )
        );
        hard = std::max(soft, hard);

        limits.soft_time_ms = soft;
        limits.hard_time_ms = hard;
    }

    if (!limits.infinite &&
        limits.depth == search::MAX_PLY &&
        limits.node_limit == 0 &&
        limits.soft_time_ms == 0 &&
        limits.hard_time_ms == 0) {
        return false;
    }

    return true;
}

[[nodiscard]] bool parse_perft_command(
    std::string_view command,
    int& depth,
    std::size_t& threads
) noexcept {
    std::istringstream iss{std::string(command)};
    std::string token;
    std::string value;

    depth = -1;
    threads = 0;

    iss >> token; // perft

    if (!(iss >> value) || !parse_int(value, depth) || depth < 0)
        return false;

    int parsed_threads = 0;
    if (!(iss >> value) || !parse_int(value, parsed_threads) || parsed_threads <= 0)
        return false;

    threads = static_cast<std::size_t>(parsed_threads);

    if (iss >> value)
        return false;

    return true;
}

[[nodiscard]] bool parse_divide_command(
    std::string_view command,
    int& depth,
    std::size_t& threads,
    bool& live
) noexcept {
    std::istringstream iss{std::string(command)};
    std::string token;
    std::string value;

    depth = -1;
    threads = 0;
    live = false;

    iss >> token; // divide

    if (!(iss >> value) || !parse_int(value, depth) || depth < 0)
        return false;

    int parsed_threads = 0;
    if (!(iss >> value) || !parse_int(value, parsed_threads) || parsed_threads <= 0)
        return false;

    threads = static_cast<std::size_t>(parsed_threads);

    if (iss >> value) {
        if (value == "live")
            live = true;
        else
            return false;
    }

    return true;
}

} // namespace

int run_uci() {
    // The main loop is synchronous: each go command searches immediately and
    // prints a bestmove before the next command is read.
    memory::Memory mem{};
    memory::memory_init(mem, 64, 8, 2);
    attack_init_backend(mem);

    Position pos{};
    set_start_position(pos);
    position_refresh_key(pos, mem.tables);
    bool use_nnue = false;
    std::string eval_file = default_eval_file();

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "uci") {
            std::cout << "id name Valerain\n";
            std::cout << "id author Mazhaoze\n";
            std::cout << "option name Hash type spin default 64 min 1 max 33554432\n";
            std::cout << "option name Clear Hash type button\n";
            std::cout << "option name UseNNUE type check default false\n";
            std::cout << "option name EvalFile type string default " << eval_file << '\n';
            std::cout << "uciok\n";
        }
        else if (line == "isready") {
            std::cout << "readyok\n";
        }
        else if (line == "ucinewgame") {
            memory::memory_clear_hash(mem);
            set_start_position(pos);
            position_refresh_key(pos, mem.tables);
        }
        else if (line.rfind("setoption", 0) == 0) {
            handle_setoption(mem, use_nnue, eval_file, std::cout, line);
        }
        else if (line.rfind("position", 0) == 0) {
            if (!set_position_from_command(pos, mem, std::string_view(line).substr(9)))
                std::cout << "info string invalid position command\n";
        }
        else if (line.rfind("go", 0) == 0) {
            search::SearchLimits limits{};
            if (!parse_go_command(pos, line, limits)) {
                std::cout << "info string usage: " << go_usage_hint() << '\n';
                std::cout << "info string " << go_usage_examples() << '\n';
                continue;
            }

            if (use_nnue && !nnue::loaded() && !ensure_nnue_loaded(eval_file, &std::cout))
                std::cout << "info string nnue unavailable, search will use hce\n";

            limits.use_nnue = use_nnue;

            std::cout << "info string eval " << active_eval_name(limits.use_nnue) << '\n';
            const search::SearchResult result =
                search::iterative_deepening(pos, mem, limits, &std::cout);

            std::cout << "bestmove " << search::move_to_uci(result.best_move) << '\n';
        }
        else if (line.rfind("perft", 0) == 0) {
            int depth = -1;
            std::size_t threads = 0;
            bool live = false;
            if (!parse_divide_command(line, depth, threads, live)) {
                std::cout << divide_usage_hint() << '\n';
                continue;
            }

            divide(pos, mem, depth, std::cout, threads, true);
        }
        else if (line == "stop") {
            // The current search is synchronous, so stop is a no-op for now.
        }
        else if (line == "quit") {
            break;
        }
    }

    memory::memory_free(mem);
    return 0;
}

} // namespace valerain
