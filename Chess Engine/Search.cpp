#include "Search.h"
#include "MoveGenerator.h"
#include "Type.h"
#include <algorithm> // For std::sort
#include <iostream>
#include <cstring>   // For std::memset

#ifdef _MSC_VER // If using the Microsoft Visual C++ compiler
#include <intrin.h> // Include this header for _BitScanForward64
#endif

// --- Helper function to get the index of the least significant bit and clear it ---
inline Square pop_lsb(uint64_t& bitboard) {
    if (bitboard == 0) return NO_SQUARE;
#ifdef _MSC_VER
    unsigned long index;
    _BitScanForward64(&index, bitboard);
    Square lsb_index = (Square)index;
#else
    Square lsb_index = (Square)__builtin_ctzll(bitboard);
#endif
    bitboard &= bitboard - 1;
    return lsb_index;
}


// --- Piece-Square Tables (PSTs) and Material Values ---
const int pawn_pst[64] = {
     0,  0,  0,  0,  0,  0,  0,  0, 50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,  5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,  5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,  0,  0,  0,  0,  0,  0,  0,  0
};
const int knight_pst[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,-40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,-30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,-30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,-50,-40,-30,-30,-30,-30,-40,-50
};
const int bishop_pst[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,-10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,-10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,-10, 10, 10, 10, 10, 10, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,-20,-10,-10,-10,-10,-10,-10,-20
};
const int rook_pst[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,  5, 10, 10, 10, 10, 10, 10,  5,
     -5,  0,  0,  0,  0,  0,  0, -5, -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5, -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,  0,  0,  0,  5,  5,  0,  0,  0
};
const int queen_pst[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,-10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10, -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,-10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,-20,-10,-10, -5, -5,-10,-10,-20
};
const int king_pst[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,-30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,-30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,-10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20, 20, 30, 10,  0,  0, 10, 30, 20
};
const int material[12] = { 100, 320, 330, 500, 900, 20000, -100, -320, -330, -500, -900, -20000 };
const int piece_values[6] = { 100, 320, 330, 500, 900, 20000 };


int Search::scoreMove(Move move) const {
    if (getCapturedPiece(move) != noPiece) {
        // MVV-LVA scoring for captures
        return 10000 + piece_values[getCapturedPiece(move) % 6] - piece_values[getPieceMoved(move) % 6];
    }
    else {
        // For quiet moves, return the score from the history table
        return m_history[getPieceMoved(move)][getToSquare(move)];
    }
}


Move Search::findBestMove(Board& board, int depth) {
    // Clear history ONCE before starting the search process
    std::memset(m_history, 0, sizeof(m_history));
    m_transpositionTable.clear();

    Move bestMove = 0;

    // --- NEW: Iterative Deepening Loop ---
    for (int current_depth = 1; current_depth <= depth; ++current_depth) {
        int alpha = -999999;
        int bestScore = -999999;
        Side sideToMove = board.getSideToMove();
        Side opponentSide = (Side)(1 - sideToMove);

        std::vector<Move> moveList;
        MoveGenerator::generateMoves(board, moveList);

        std::sort(moveList.begin(), moveList.end(), [this](Move a, Move b) { return scoreMove(a) > scoreMove(b); });

        Move currentBestMove = 0;
        for (const auto& move : moveList) {
            board.makeMove(move);
            if (!board.isSquareAttacked(board.getKingSquare(sideToMove), opponentSide)) {
                int score = -negamax(board, current_depth - 1, -999999, -alpha);
                if (score > bestScore) {
                    bestScore = score;
                    currentBestMove = move; 
                    alpha = score;
                }
            }
            board.unmakeMove(move);
        }
        if (currentBestMove != 0) {
            bestMove = currentBestMove;
        }
    }

    // Return the best move found during the final (deepest) iteration
    return bestMove;
}


int Search::negamax(Board& board, int depth, int alpha, int beta) {
    int alphaOrig = alpha;
    uint64_t hash = board.getHashKey();

    if (m_transpositionTable.count(hash)) {
        const TTEntry& entry = m_transpositionTable.at(hash);
        if (entry.depth >= depth) {
            if (entry.flag == TTEntry::EXACT) return entry.score;
            if (entry.flag == TTEntry::LOWERBOUND) alpha = std::max(alpha, entry.score);
            else if (entry.flag == TTEntry::UPPERBOUND) beta = std::min(beta, entry.score);
            if (alpha >= beta) return entry.score;
        }
    }

    if (depth == 0) {
        return quiescence(board, alpha, beta);
    }

    int max = -999999;
    Move bestMove = 0;
    Side sideToMove = board.getSideToMove();
    Side opponentSide = (Side)(1 - sideToMove);
    std::vector<Move> moveList;
    MoveGenerator::generateMoves(board, moveList);

    std::sort(moveList.begin(), moveList.end(), [this](Move a, Move b) { return scoreMove(a) > scoreMove(b); });

    bool hasLegalMove = false;
    for (const auto& move : moveList) {
        board.makeMove(move);
        if (!board.isSquareAttacked(board.getKingSquare(sideToMove), opponentSide)) {
            hasLegalMove = true;
            int score = -negamax(board, depth - 1, -beta, -alpha);
            board.unmakeMove(move);
            if (score > max) {
                max = score;
                bestMove = move;
            }
            if (score > alpha) alpha = score;
            if (alpha >= beta) {
                // Update history table on a quiet move cutoff
                if (getCapturedPiece(move) == noPiece) {
                    m_history[getPieceMoved(move)][getToSquare(move)] += depth * depth;
                }
                break; // Pruning
            }
        }
        else {
            board.unmakeMove(move);
        }
    }

    if (!hasLegalMove) {
        if (board.isSquareAttacked(board.getKingSquare(sideToMove), opponentSide)) {
            return -99999 + depth;
        }
        else {
            return 0;
        }
    }

    // Transposition Table Store
    TTEntry entry;
    entry.hashKey = hash;
    entry.score = max;
    entry.depth = depth;
    entry.bestMove = bestMove;
    if (max <= alphaOrig) entry.flag = TTEntry::UPPERBOUND;
    else if (max >= beta) entry.flag = TTEntry::LOWERBOUND;
    else entry.flag = TTEntry::EXACT;
    m_transpositionTable[hash] = entry;

    return max;
}


int Search::quiescence(Board& board, int alpha, int beta) {
    int stand_pat = evaluate(board);
    if (stand_pat >= beta) {
        return beta;
    }
    if (alpha < stand_pat) {
        alpha = stand_pat;
    }

    Side sideToMove = board.getSideToMove();
    Side opponentSide = (Side)(1 - sideToMove);
    std::vector<Move> moveList;
    MoveGenerator::generateMoves(board, moveList);
    std::sort(moveList.begin(), moveList.end(), [this](Move a, Move b) { return scoreMove(a) > scoreMove(b); });

    for (const auto& move : moveList) {
        if (getCapturedPiece(move) == noPiece) {
            continue;
        }

        board.makeMove(move);
        if (!board.isSquareAttacked(board.getKingSquare(sideToMove), opponentSide)) {
            int score = -quiescence(board, -beta, -alpha);
            board.unmakeMove(move);

            if (score >= beta) {
                return beta;
            }
            if (score > alpha) {
                alpha = score;
            }
        }
        else {
            board.unmakeMove(move);
        }
    }
    return alpha;
}


int Search::evaluate(const Board& board) {
    int score = 0;
    Side sideToMove = board.getSideToMove();
    for (int i = 0; i < 12; ++i) {
        pieceType pt = (pieceType)i;
        uint64_t bitboard = board.getPieceBitboard(pt);
        while (bitboard) {
            Square sq = pop_lsb(bitboard);
            score += material[pt];

            if (pt == whitePawn) score += pawn_pst[sq];
            else if (pt == blackPawn) score -= pawn_pst[63 - sq];
            else if (pt == whiteKnight) score += knight_pst[sq];
            else if (pt == blackKnight) score -= knight_pst[63 - sq];
            else if (pt == whiteBishop) score += bishop_pst[sq];
            else if (pt == blackBishop) score -= bishop_pst[63 - sq];
            else if (pt == whiteRook) score += rook_pst[sq];
            else if (pt == blackRook) score -= rook_pst[63 - sq];
            else if (pt == whiteQueen) score += queen_pst[sq];
            else if (pt == blackQueen) score -= queen_pst[63 - sq];
            else if (pt == whiteKing) score += king_pst[sq];
            else if (pt == blackKing) score -= king_pst[63 - sq];
        }
    }
    return (sideToMove == W) ? score : -score;
}

