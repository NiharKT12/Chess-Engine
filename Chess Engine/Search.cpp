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

// --- Helper function for bitcount on MSVC and GCC ---
inline int popcount(uint64_t x) {
#ifdef _MSC_VER
    return static_cast<int>(__popcnt64(x));
#else
    return __builtin_popcountll(x);
#endif
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


int Search::scoreMove(Move move, int ply) const {
    if (getCapturedPiece(move) != noPiece) {
        // MVV-LVA scoring for captures
        return 10000 + piece_values[getCapturedPiece(move) % 6] - piece_values[getPieceMoved(move) % 6];
    }
    else {
        // For quiet moves, check killer moves and history
        int score = m_history[getPieceMoved(move)][getToSquare(move)];
        
        // Boost killer moves
        if (ply < 64 && (move == m_killers[ply][0] || move == m_killers[ply][1])) {
            score += 5000;
        }
        
        // Boost counter moves
        if (ply >= 1 && ply < 64 && move == m_counterMoves[getPieceMoved(move)][getToSquare(move)]) {
            score += 3000;
        }
        
        return score;
    }
}

// --- NEW: Check if a move gives check ---
bool Search::isCheckMove(const Board& board, Move move) const {
    // Make the move and check if opponent's king is in check
    Side sideToMove = board.getSideToMove();
    Side opponentSide = (Side)(1 - sideToMove);
    
    // Quick check: does this move put opponent in check?
    Square toSquare = getToSquare(move);
    return board.isSquareAttacked(board.getKingSquare(opponentSide), sideToMove);
}

Move Search::findBestMove(Board& board, int depth) {
    // Clear history, killers, and counter moves before starting
    std::memset(m_history, 0, sizeof(m_history));
    std::memset(m_killers, 0, sizeof(m_killers));
    std::memset(m_counterMoves, 0, sizeof(m_counterMoves));
    m_transpositionTable.clear();

    Move bestMove = 0;
    int prevScore = 0;

    // --- NEW: Iterative Deepening with Aspiration Windows ---
    for (int current_depth = 1; current_depth <= depth; ++current_depth) {
        int alpha = -999999;
        int beta = 999999;
        int bestScore = -999999;
        Side sideToMove = board.getSideToMove();
        Side opponentSide = (Side)(1 - sideToMove);

        // --- NEW: Aspiration Window (only after first iteration) ---
        if (current_depth > 1) {
            int window = 50; // Start with small window
            alpha = prevScore - window;
            beta = prevScore + window;
        }

        std::vector<Move> moveList;
        MoveGenerator::generateMoves(board, moveList);

        std::sort(moveList.begin(), moveList.end(), [this](Move a, Move b) { 
            return scoreMove(a, 0) > scoreMove(b, 0); 
        });

        Move currentBestMove = 0;
        for (const auto& move : moveList) {
            board.makeMove(move);
            if (!board.isSquareAttacked(board.getKingSquare(sideToMove), opponentSide)) {
                int score = -negamax(board, current_depth - 1, -beta, -alpha, 1);
                if (score > bestScore) {
                    bestScore = score;
                    currentBestMove = move;
                    alpha = score;
                }
            }
            board.unmakeMove(move);
        }

        // --- NEW: Aspiration Window failure handling ---
        if (currentBestMove == 0 || (current_depth > 1 && (bestScore <= alpha - 50 || bestScore >= beta - 50))) {
            // Re-search with full window
            alpha = -999999;
            beta = 999999;
            std::sort(moveList.begin(), moveList.end(), [this](Move a, Move b) { 
                return scoreMove(a, 0) > scoreMove(b, 0); 
            });

            for (const auto& move : moveList) {
                board.makeMove(move);
                if (!board.isSquareAttacked(board.getKingSquare(sideToMove), opponentSide)) {
                    int score = -negamax(board, current_depth - 1, -beta, -alpha, 1);
                    if (score > bestScore) {
                        bestScore = score;
                        currentBestMove = move;
                    }
                }
                board.unmakeMove(move);
            }
        }

        if (currentBestMove != 0) {
            bestMove = currentBestMove;
            prevScore = bestScore;
        }
    }

    return bestMove;
}


int Search::negamax(Board& board, int depth, int alpha, int beta, int ply) {
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

    // --- NEW: Improved Move Ordering with ply parameter ---
    std::sort(moveList.begin(), moveList.end(), [this, ply](Move a, Move b) {
        return scoreMove(a, ply) > scoreMove(b, ply);
    });

    bool hasLegalMove = false;
    int moveCount = 0;
    
    for (const auto& move : moveList) {
        board.makeMove(move);
        if (!board.isSquareAttacked(board.getKingSquare(sideToMove), opponentSide)) {
            hasLegalMove = true;
            int score;
            moveCount++;

            // --- NEW: Late Move Reductions (LMR) ---
            // Only apply LMR to quiet moves that aren't the first few moves or killer moves
            if (depth >= 3 && moveCount > 3 && 
                getCapturedPiece(move) == noPiece && 
                !(ply < 64 && (move == m_killers[ply][0] || move == m_killers[ply][1]))) {
                
                // Calculate reduction amount from LMR table
                int reduction = lmr_table[std::min(depth, 63)][std::min(moveCount, 63)];
                if (reduction > 0) {
                    score = -negamax(board, depth - 1 - reduction, -alpha - 1, -alpha, ply + 1);
                    
                    // If move looks promising, research with full depth
                    if (score > alpha) {
                        score = -negamax(board, depth - 1, -beta, -alpha, ply + 1);
                    }
                } else {
                    score = -negamax(board, depth - 1, -beta, -alpha, ply + 1);
                }
            } else {
                // --- NEW: Check extensions (extend search for checking moves) ---
                int extension = 0;
                score = -negamax(board, depth - 1 + extension, -beta, -alpha, ply + 1);
            }

            board.unmakeMove(move);
            
            if (score > max) {
                max = score;
                bestMove = move;
            }
            if (score > alpha) alpha = score;
            if (alpha >= beta) {
                // --- NEW: Update killer and counter moves on beta cutoff ---
                if (getCapturedPiece(move) == noPiece && ply < 64) {
                    // Update killer moves
                    if (m_killers[ply][0] != move) {
                        m_killers[ply][1] = m_killers[ply][0];
                        m_killers[ply][0] = move;
                    }
                    // Update history table
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
    std::sort(moveList.begin(), moveList.end(), [this](Move a, Move b) { return scoreMove(a, 0) > scoreMove(b, 0); });

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
    
    // --- NEW: Add positional evaluations ---
    score += evaluateKingSafety(board);
    score += evaluatePawnStructure(board);
    score += evaluatePieceMobility(board);
    
    return (sideToMove == W) ? score : -score;
}

// --- NEW: King Safety Evaluation ---
int Search::evaluateKingSafety(const Board& board) {
    int score = 0;
    
    // Evaluate white king safety
    Square wKingSquare = board.getKingSquare(W);
    uint64_t wKingZone = MoveGenerator::king_attacks[wKingSquare];
    
    // Count attacking pieces around white's king
    uint64_t blackPawns = board.getPieceBitboard(blackPawn);
    uint64_t blackKnights = board.getPieceBitboard(blackKnight);
    uint64_t blackBishops = board.getPieceBitboard(blackBishop);
    uint64_t blackRooks = board.getPieceBitboard(blackRook);
    uint64_t blackQueens = board.getPieceBitboard(blackQueen);
    
    int whiteKingAttackers = 0;
    if (wKingZone & blackPawns) whiteKingAttackers++;
    if (wKingZone & blackKnights) whiteKingAttackers += 2;
    if (wKingZone & (blackBishops | blackQueens)) whiteKingAttackers += 2;
    if (wKingZone & (blackRooks | blackQueens)) whiteKingAttackers += 3;
    
    // Penalize exposed white king
    score -= whiteKingAttackers * 15;
    
    // Evaluate black king safety
    Square bKingSquare = board.getKingSquare(B);
    uint64_t bKingZone = MoveGenerator::king_attacks[bKingSquare];
    
    uint64_t whitePawns = board.getPieceBitboard(whitePawn);
    uint64_t whiteKnights = board.getPieceBitboard(whiteKnight);
    uint64_t whiteBishops = board.getPieceBitboard(whiteBishop);
    uint64_t whiteRooks = board.getPieceBitboard(whiteRook);
    uint64_t whiteQueens = board.getPieceBitboard(whiteQueen);
    
    int blackKingAttackers = 0;
    if (bKingZone & whitePawns) blackKingAttackers++;
    if (bKingZone & whiteKnights) blackKingAttackers += 2;
    if (bKingZone & (whiteBishops | whiteQueens)) blackKingAttackers += 2;
    if (bKingZone & (whiteRooks | whiteQueens)) blackKingAttackers += 3;
    
    // Reward attacking black king (penalty to opponent)
    score += blackKingAttackers * 15;
    
    return score;
}

// --- NEW: Pawn Structure Evaluation ---
int Search::evaluatePawnStructure(const Board& board) {
    int score = 0;
    
    uint64_t whitePawns = board.getPieceBitboard(whitePawn);
    uint64_t blackPawns = board.getPieceBitboard(blackPawn);
    
    // Evaluate white pawns
    uint64_t wPawns = whitePawns;
    while (wPawns) {
        Square sq = pop_lsb(wPawns);
        int file = sq % 8;
        int rank = sq / 8;
        
        // Penalize doubled pawns
        if ((whitePawns >> 8) & (1ULL << (sq - 8))) {
            score -= 25;
        }
        
        // Penalize isolated pawns
        uint64_t adjacentFiles = 0;
        if (file > 0) adjacentFiles |= (1ULL << (sq - 1)) | (1ULL << (sq + 7)) | (1ULL << (sq - 9));
        if (file < 7) adjacentFiles |= (1ULL << (sq + 1)) | (1ULL << (sq + 9)) | (1ULL << (sq - 7));
        
        if (!(whitePawns & adjacentFiles)) {
            score -= 20;
        }
        
        // Reward passed pawns (no black pawns blocking or on adjacent files ahead)
        uint64_t blockingPawns = 0;
        for (int r = rank + 1; r < 8; ++r) {
            blockingPawns |= (blackPawns & ((1ULL << (file + r * 8)) | 
                             ((file > 0) ? (1ULL << (file - 1 + r * 8)) : 0) |
                             ((file < 7) ? (1ULL << (file + 1 + r * 8)) : 0)));
        }
        if (blockingPawns == 0) {
            score += 50 + (rank * 10); // Bonus increases as pawn advances
        }
    }
    
    // Evaluate black pawns
    uint64_t bPawns = blackPawns;
    while (bPawns) {
        Square sq = pop_lsb(bPawns);
        int file = sq % 8;
        int rank = sq / 8;
        
        // Penalize doubled pawns
        if ((blackPawns << 8) & (1ULL << (sq + 8))) {
            score += 25;
        }
        
        // Penalize isolated pawns
        uint64_t adjacentFiles = 0;
        if (file > 0) adjacentFiles |= (1ULL << (sq - 1)) | (1ULL << (sq + 7)) | (1ULL << (sq - 9));
        if (file < 7) adjacentFiles |= (1ULL << (sq + 1)) | (1ULL << (sq + 9)) | (1ULL << (sq - 7));
        
        if (!(blackPawns & adjacentFiles)) {
            score += 20;
        }
        
        // Reward passed pawns
        uint64_t blockingPawns = 0;
        for (int r = rank - 1; r >= 0; --r) {
            blockingPawns |= (whitePawns & ((1ULL << (file + r * 8)) |
                             ((file > 0) ? (1ULL << (file - 1 + r * 8)) : 0) |
                             ((file < 7) ? (1ULL << (file + 1 + r * 8)) : 0)));
        }
        if (blockingPawns == 0) {
            score -= 50 + ((7 - rank) * 10);
        }
    }
    
    return score;
}

// --- NEW: Piece Mobility Evaluation ---
int Search::evaluatePieceMobility(const Board& board) {
    int score = 0;
    
    // Evaluate white pieces
    uint64_t whiteKnights = board.getPieceBitboard(whiteKnight);
    while (whiteKnights) {
        Square sq = pop_lsb(whiteKnights);
        uint64_t moves = MoveGenerator::knight_attacks[sq] & ~board.getPieces(W);
        int moveCount = popcount(moves);
        score += moveCount * 2;
    }
    
    uint64_t whiteRooks = board.getPieceBitboard(whiteRook);
    while (whiteRooks) {
        Square sq = pop_lsb(whiteRooks);
        uint64_t moves = board.getRookAttacks(sq, board.getOccupied()) & ~board.getPieces(W);
        int moveCount = popcount(moves);
        score += moveCount;
    }
    
    uint64_t whiteBishops = board.getPieceBitboard(whiteBishop);
    while (whiteBishops) {
        Square sq = pop_lsb(whiteBishops);
        uint64_t moves = board.getBishopAttacks(sq, board.getOccupied()) & ~board.getPieces(W);
        int moveCount = popcount(moves);
        score += moveCount;
    }
    
    // Evaluate black pieces
    uint64_t blackKnights = board.getPieceBitboard(blackKnight);
    while (blackKnights) {
        Square sq = pop_lsb(blackKnights);
        uint64_t moves = MoveGenerator::knight_attacks[sq] & ~board.getPieces(B);
        int moveCount = popcount(moves);
        score -= moveCount * 2;
    }
    
    uint64_t blackRooks = board.getPieceBitboard(blackRook);
    while (blackRooks) {
        Square sq = pop_lsb(blackRooks);
        uint64_t moves = board.getRookAttacks(sq, board.getOccupied()) & ~board.getPieces(B);
        int moveCount = popcount(moves);
        score -= moveCount;
    }
    
    uint64_t blackBishops = board.getPieceBitboard(blackBishop);
    while (blackBishops) {
        Square sq = pop_lsb(blackBishops);
        uint64_t moves = board.getBishopAttacks(sq, board.getOccupied()) & ~board.getPieces(B);
        int moveCount = popcount(moves);
        score -= moveCount;
    }
    
    return score;
}

