// headless_main.cpp
//
// A Raylib-free CLI entry point for the chess engine, built specifically so
// an external script (our agent's run_benchmark tool) can invoke a single
// search and get back machine-readable output, without launching the GUI.
//
// Usage:
//   ./headless_engine "<fen>" <depth>
//
// Example:
//   ./headless_engine "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 6
//
// Prints a single line of JSON to stdout:
//   {"best_move": "e2e4", "time_ms": 123.45, "depth": 6, "fen": "..."}
//
// Any errors go to stderr so they don't corrupt the JSON on stdout.

#include <iostream>
#include <chrono>
#include <string>
#include <cstdlib>

#include "Type.h"
#include "Board.h"
#include "MoveGenerator.h"
#include "Search.h"
#include "Zobrist.h"

// Converts a Move into algebraic-ish coordinate notation (e.g. "e2e4", "e7e8q")
// so it's easy for a script (or an LLM) to read without needing our internal enums.
std::string moveToString(Move m) {
    Square from = getFromSquare(m);
    Square to = getToSquare(m);

    auto squareToStr = [](Square sq) -> std::string {
        char file = 'a' + (sq % 8);
        char rank = '1' + (sq / 8);
        return std::string(1, file) + std::string(1, rank);
    };

    std::string result = squareToStr(from) + squareToStr(to);

    MoveFlag flag = getMoveFlag(m);
    if (flag == QueenPromotion || flag == QueenPromoCapture) result += "q";
    else if (flag == RookPromotion || flag == RookPromoCapture) result += "r";
    else if (flag == BishopPromotion || flag == BishopPromoCapture) result += "b";
    else if (flag == KnightPromotion || flag == KnightPromoCapture) result += "n";

    return result;
}

// Minimal JSON string escaping (we only ever pass through FEN strings, but be safe).
std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " \"<fen>\" <depth>" << std::endl;
        return 1;
    }

    std::string fen = argv[1];
    int depth = std::atoi(argv[2]);

    if (depth <= 0) {
        std::cerr << "Error: depth must be a positive integer, got: " << argv[2] << std::endl;
        return 1;
    }

    // One-time global init, same as Main.cpp does before entering the game loop.
    Zobrist::init();
    MoveGenerator::init();

    Board board;
    board.setupFromFen(fen);

    Search search;

    auto startTime = std::chrono::high_resolution_clock::now();
    Move bestMove = search.findBestMove(board, depth);
    auto endTime = std::chrono::high_resolution_clock::now();

    double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    if (bestMove == 0) {
        std::cerr << "Warning: findBestMove returned no move (checkmate/stalemate position?)" << std::endl;
        std::cout << "{\"best_move\": null, \"time_ms\": " << elapsedMs
                   << ", \"depth\": " << depth
                   << ", \"fen\": \"" << jsonEscape(fen) << "\"}" << std::endl;
        return 0;
    }

    std::cout << "{\"best_move\": \"" << moveToString(bestMove) << "\""
               << ", \"time_ms\": " << elapsedMs
               << ", \"depth\": " << depth
               << ", \"fen\": \"" << jsonEscape(fen) << "\"}" << std::endl;

    return 0;
}
