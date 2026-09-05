#pragma once
#include "Board.h"
#include <cstdint>
#include <vector>

// What a stored score means relative to the window it was searched with.
enum TTFlag : uint8_t {
    TT_NONE = 0,   // empty slot
    TT_EXACT,      // score is the true value
    TT_LOWER,      // score is a lower bound (a beta cutoff happened)
    TT_UPPER       // score is an upper bound (no move beat alpha)
};

struct TTEntry {
    uint64_t hashKey = 0;
    Move bestMove = 0;
    int16_t score = 0;
    int8_t depth = -1;
    uint8_t flag = TT_NONE;
    uint8_t generation = 0;
};

class Search {
public:
    Search();

    // Searches to `depth` plies with iterative deepening and returns the best
    // move, or 0 if the side to move has no legal moves.
    Move findBestMove(Board& board, int depth);

    static constexpr int MAX_PLY = 64;
    // Larger than any real evaluation, so it can serve as +/- infinity.
    static constexpr int INFINITE_SCORE = 32000;
    // A forced mate scores MATE_VALUE minus the distance to it in plies.
    static constexpr int MATE_VALUE = 31000;
    // Any |score| above this bound represents a mate rather than an evaluation.
    static constexpr int MATE_BOUND = MATE_VALUE - MAX_PLY;

private:
    // Root search over a single window. Reports the best move it found.
    int searchRoot(Board& board, int depth, int alpha, int beta, Move& bestMoveOut);
    int negamax(Board& board, int depth, int alpha, int beta, int ply);
    int quiescence(Board& board, int alpha, int beta, int ply);
    int evaluate(const Board& board);

    // --- Move ordering ---
    int scoreMove(Move move, Move ttMove, int ply) const;
    // Packs each move as (score << 32 | move) and sorts descending, so the
    // ordering score is computed once per move instead of once per comparison.
    void orderMoves(const std::vector<Move>& moves, std::vector<uint64_t>& out,
                    Move ttMove, int ply, bool tacticalOnly) const;
    void recordQuietSuccess(Move move, int depth, int ply);
    void ageHistory();

    // --- Transposition table ---
    // Mate scores are stored relative to the node they were found at, so they
    // must be re-based on the way in and out.
    static int scoreToTT(int score, int ply);
    static int scoreFromTT(int score, int ply);
    TTEntry* probeTT(uint64_t hash);
    void storeTT(uint64_t hash, int depth, int score, uint8_t flag, Move bestMove, int ply);

    // --- Evaluation helpers ---
    int evaluateKingSafety(const Board& board);
    int evaluatePawnStructure(const Board& board);
    int evaluatePieceMobility(const Board& board);

    // A fixed-size, power-of-two table indexed by hash. Replaces the unbounded
    // std::unordered_map that used to be cleared before every move.
    static constexpr size_t TT_ENTRIES = 1u << 20;   // ~24 MB
    std::vector<TTEntry> m_transpositionTable;
    uint8_t m_generation = 0;

    // History heuristic: how often a quiet move has caused a beta cutoff.
    int m_history[12][64];
    // Two killer moves per ply.
    Move m_killers[MAX_PLY][2];

    // Per-ply scratch buffers, so no search node has to allocate. Quiescence
    // gets its own set because it runs at the same ply index as negamax.
    std::vector<Move> m_moveBuf[MAX_PLY + 1];
    std::vector<uint64_t> m_orderBuf[MAX_PLY + 1];
    std::vector<Move> m_qMoveBuf[MAX_PLY + 1];
    std::vector<uint64_t> m_qOrderBuf[MAX_PLY + 1];
};
