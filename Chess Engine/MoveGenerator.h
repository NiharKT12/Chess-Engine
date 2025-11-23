#pragma once

#include <vector>
#include "Type.h" // For Move, uint64_t, etc.

class Board; // Forward declaration

class MoveGenerator {
public:
    // The main public function. It takes a board state and fills a
    // list with all pseudo-legal moves for the current side to move.
    static void generateMoves(const Board& board, std::vector<Move>& moveList);

    // A function to initialize all our pre-calculated tables.
    // This must be called once when the program starts.
    static void init();

    // --- Pre-calculated Attack Tables (Public) ---
    // These are public so other classes (like Board) can use them.
    static uint64_t king_attacks[64];
    static uint64_t knight_attacks[64];
    static uint64_t pawn_attacks[2][64];

private:
    // --- Helper Functions for Initialization ---
    static void initKingAttacks(int sq);
    static void initKnightAttacks(int sq);
    static void initPawnAttacks(int sq);

    // --- Helper Functions for Move Generation ---
    static void generatePawnMoves(const Board& board, std::vector<Move>& moveList);
    static void generateKnightMoves(const Board& board, std::vector<Move>& moveList);
    static void generateKingMoves(const Board& board, std::vector<Move>& moveList);
    static void generateSlidingMoves(const Board& board, std::vector<Move>& moveList);
};

