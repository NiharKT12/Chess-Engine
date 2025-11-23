#pragma once
#include "Board.h"
#include <vector>
#include <unordered_map>

struct TTEntry {
    uint64_t hashKey;
    int depth;
    enum { EXACT, LOWERBOUND, UPPERBOUND } flag;
    int score;
    Move bestMove;
};

class Search {
public:
    Move findBestMove(Board& board, int depth);

private:
    int negamax(Board& board, int depth, int alpha, int beta);
    int quiescence(Board& board, int alpha, int beta);
    int evaluate(const Board& board);

    // Scores a move for ordering purposes, now using the history heuristic.
    int scoreMove(Move move) const;

    std::unordered_map<uint64_t, TTEntry> m_transpositionTable;

    // --- NEW: History Heuristic Table ---
    // Stores a score for each quiet move (piece type on a destination square).
    int m_history[12][64];
};

