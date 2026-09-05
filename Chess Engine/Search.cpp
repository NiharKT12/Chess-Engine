#include "Search.h"
#include "MoveGenerator.h"
#include "Type.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

// --- Piece-Square Tables (PSTs) and Material Values ---
// These tables are written from White's point of view with index 0 == a8.
// Square, however, has A1 == 0, so every lookup goes through pstIndex(), which
// maps a white piece to sq ^ 56 and a black piece to sq. Reading them with the
// raw square number turns every table upside-down for both colours.
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

const int piece_values[6] = { 100, 320, 330, 500, 900, 20000 };
static const int* const pst_for_piece[6] = {
    pawn_pst, knight_pst, bishop_pst, rook_pst, queen_pst, king_pst
};

// --- Move ordering score bands ---
// Every band is positive and disjoint, so the packed (score << 32 | move) sort
// key orders moves exactly by these priorities.
static constexpr int SCORE_TT      = 2000000;
static constexpr int SCORE_PROMO   = 1800000;
static constexpr int SCORE_CAPTURE = 1000000;
static constexpr int SCORE_KILLER1 =  900000;
static constexpr int SCORE_KILLER2 =  800000;
static constexpr int SCORE_CASTLE  =  700000;
// Kept below SCORE_CASTLE so history can never outrank a killer or a castle.
static constexpr int HISTORY_MAX   =  400000;

// --- Late Move Reduction table ---
// Built once at startup rather than hand-written, so it covers every depth.
// The old hand-written table only had 8 rows, leaving LMR inactive past depth 7.
struct LmrTable {
    int r[64][64];
    LmrTable() {
        for (int d = 0; d < 64; ++d) {
            for (int m = 0; m < 64; ++m) {
                r[d][m] = (d < 3 || m < 3) ? 0
                        : (int)(0.75 + std::log((double)d) * std::log((double)m) / 2.25);
            }
        }
    }
};
static const LmrTable lmr;

// --- Pawn structure masks ---
struct PawnMasks {
    uint64_t file[8];
    uint64_t adjacentFiles[8];
    uint64_t frontW[64], frontB[64];     // same file, strictly ahead
    uint64_t passedW[64], passedB[64];   // own + adjacent files, strictly ahead
    PawnMasks() {
        for (int f = 0; f < 8; ++f) file[f] = 0x0101010101010101ULL << f;
        for (int f = 0; f < 8; ++f) {
            adjacentFiles[f] = 0ULL;
            if (f > 0) adjacentFiles[f] |= file[f - 1];
            if (f < 7) adjacentFiles[f] |= file[f + 1];
        }
        for (int sq = 0; sq < 64; ++sq) {
            const int f = sq % 8, r = sq / 8;
            uint64_t aheadW = 0ULL, aheadB = 0ULL;
            for (int rr = r + 1; rr < 8; ++rr) aheadW |= 0xFFULL << (rr * 8);
            for (int rr = r - 1; rr >= 0; --rr) aheadB |= 0xFFULL << (rr * 8);
            frontW[sq] = file[f] & aheadW;
            frontB[sq] = file[f] & aheadB;
            passedW[sq] = (file[f] | adjacentFiles[f]) & aheadW;
            passedB[sq] = (file[f] | adjacentFiles[f]) & aheadB;
        }
    }
};
static const PawnMasks pawnMasks;

// Passed pawn bonus indexed by how far the pawn has advanced (0 = home rank).
static const int passed_bonus[8] = { 0, 10, 17, 30, 55, 90, 140, 0 };
static constexpr int DOUBLED_PENALTY  = 20;
static constexpr int ISOLATED_PENALTY = 15;

Search::Search() : m_transpositionTable(TT_ENTRIES) {
    std::memset(m_history, 0, sizeof(m_history));
    std::memset(m_killers, 0, sizeof(m_killers));
}

// --- MOVE ORDERING ---

int Search::scoreMove(Move move, Move ttMove, int ply) const {
    if (move == ttMove) return SCORE_TT;

    if (isPromotion(move)) {
        return SCORE_PROMO + piece_values[getPromotionPiece(move) % 6];
    }

    const pieceType captured = getCapturedPiece(move);
    if (captured != noPiece) {
        // MVV-LVA: prefer taking the most valuable victim with the least
        // valuable attacker.
        return SCORE_CAPTURE + piece_values[captured % 6] * 10 - piece_values[getPieceMoved(move) % 6];
    }

    if (ply >= 0 && ply < MAX_PLY) {
        if (move == m_killers[ply][0]) return SCORE_KILLER1;
        if (move == m_killers[ply][1]) return SCORE_KILLER2;
    }

    const MoveFlag flag = getMoveFlag(move);
    if (flag == KingCastle || flag == QueenCastle) return SCORE_CASTLE;

    return std::min(m_history[getPieceMoved(move)][getToSquare(move)], HISTORY_MAX);
}

void Search::orderMoves(const std::vector<Move>& moves, std::vector<uint64_t>& out,
                        Move ttMove, int ply, bool tacticalOnly) const {
    out.clear();
    out.reserve(moves.size());
    for (Move m : moves) {
        if (tacticalOnly && getCapturedPiece(m) == noPiece && !isPromotion(m)) continue;
        const uint64_t key = (uint64_t)(uint32_t)scoreMove(m, ttMove, ply);
        out.push_back((key << 32) | m);
    }
    std::sort(out.begin(), out.end(), std::greater<uint64_t>());
}

void Search::ageHistory() {
    for (int p = 0; p < 12; ++p)
        for (int s = 0; s < 64; ++s)
            m_history[p][s] /= 2;
}

void Search::recordQuietSuccess(Move move, int depth, int ply) {
    if (ply >= 0 && ply < MAX_PLY && m_killers[ply][0] != move) {
        m_killers[ply][1] = m_killers[ply][0];
        m_killers[ply][0] = move;
    }
    int& h = m_history[getPieceMoved(move)][getToSquare(move)];
    h += depth * depth;
    if (h > HISTORY_MAX) ageHistory();
}

// --- TRANSPOSITION TABLE ---

int Search::scoreToTT(int score, int ply) {
    if (score >= MATE_BOUND) return score + ply;
    if (score <= -MATE_BOUND) return score - ply;
    return score;
}

int Search::scoreFromTT(int score, int ply) {
    if (score >= MATE_BOUND) return score - ply;
    if (score <= -MATE_BOUND) return score + ply;
    return score;
}

TTEntry* Search::probeTT(uint64_t hash) {
    TTEntry& e = m_transpositionTable[hash & (TT_ENTRIES - 1)];
    return (e.flag != TT_NONE && e.hashKey == hash) ? &e : nullptr;
}

void Search::storeTT(uint64_t hash, int depth, int score, uint8_t flag, Move bestMove, int ply) {
    TTEntry& e = m_transpositionTable[hash & (TT_ENTRIES - 1)];
    // Depth-preferred replacement, but a slot left over from an earlier search
    // is always reusable.
    if (e.flag != TT_NONE && e.hashKey == hash && e.generation == m_generation && e.depth > depth) return;

    e.hashKey = hash;
    e.bestMove = bestMove;
    e.score = (int16_t)scoreToTT(score, ply);
    e.depth = (int8_t)std::min(depth, 127);
    e.flag = flag;
    e.generation = m_generation;
}

// --- ROOT ---

int Search::searchRoot(Board& board, int depth, int alpha, int beta, Move& bestMoveOut) {
    const int alphaOrig = alpha;
    const uint64_t hash = board.getHashKey();
    const Side us = board.getSideToMove();
    const Side them = (Side)(1 - us);

    Move ttMove = 0;
    if (TTEntry* e = probeTT(hash)) ttMove = e->bestMove;

    std::vector<Move>& moveList = m_moveBuf[0];
    std::vector<uint64_t>& ordered = m_orderBuf[0];
    MoveGenerator::generateMoves(board, moveList);
    orderMoves(moveList, ordered, ttMove, 0, false);

    int best = -INFINITE_SCORE;
    Move bestMove = 0;
    int legalMoves = 0;

    for (uint64_t packed : ordered) {
        const Move move = (Move)(packed & 0xFFFFFFFFULL);
        board.makeMove(move);
        if (board.isSquareAttacked(board.getKingSquare(us), them)) {
            board.unmakeMove(move);
            continue;
        }
        legalMoves++;

        int score;
        if (legalMoves == 1) {
            score = -negamax(board, depth - 1, -beta, -alpha, 1);
        } else {
            // Principal variation search: assume the first move is best and
            // verify the rest with a null window.
            score = -negamax(board, depth - 1, -alpha - 1, -alpha, 1);
            if (score > alpha && score < beta)
                score = -negamax(board, depth - 1, -beta, -alpha, 1);
        }
        board.unmakeMove(move);

        if (score > best) {
            best = score;
            bestMove = move;
            if (score > alpha) alpha = score;
        }
        if (alpha >= beta) break;
    }

    bestMoveOut = bestMove;
    if (legalMoves == 0) return -INFINITE_SCORE;

    const uint8_t flag = (best <= alphaOrig) ? TT_UPPER : (best >= beta ? TT_LOWER : TT_EXACT);
    storeTT(hash, depth, best, flag, bestMove, 0);
    return best;
}

Move Search::findBestMove(Board& board, int maxDepth) {
    std::memset(m_history, 0, sizeof(m_history));
    std::memset(m_killers, 0, sizeof(m_killers));
    // Bump the generation instead of clearing the table, so entries from the
    // previous move stay usable but lose replacement priority.
    m_generation++;

    Move bestMove = 0;
    int prevScore = 0;

    for (int depth = 1; depth <= maxDepth; ++depth) {
        int window = 40;
        int alpha = -INFINITE_SCORE, beta = INFINITE_SCORE;
        if (depth >= 4) {
            alpha = prevScore - window;
            beta = prevScore + window;
        }

        // Re-search with a wider window whenever the score escapes the
        // aspiration window. The previous version overwrote alpha inside the
        // root loop, which made a fail-low impossible to detect and let a
        // bounded, unreliable score select the move.
        while (true) {
            Move move = 0;
            const int score = searchRoot(board, depth, alpha, beta, move);

            if (move == 0) return bestMove;   // no legal moves in this position

            const bool fullWidth = (alpha <= -INFINITE_SCORE && beta >= INFINITE_SCORE);
            if (!fullWidth && score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max(-INFINITE_SCORE, score - window);
                window *= 2;
                continue;
            }
            if (!fullWidth && score >= beta) {
                beta = std::min(INFINITE_SCORE, score + window);
                window *= 2;
                continue;
            }

            bestMove = move;
            prevScore = score;
            break;
        }
    }

    return bestMove;
}

// --- MAIN SEARCH ---

int Search::negamax(Board& board, int depth, int alpha, int beta, int ply) {
    if (ply >= MAX_PLY) return evaluate(board);

    // Draw detection. Never applied at the root, where a move must be returned.
    if (ply > 0 && (board.isRepetition() || board.getHalfmoveClock() >= 100 ||
                    board.isInsufficientMaterial())) {
        return 0;
    }

    // Mate-distance pruning: nothing found deeper can beat a mate already
    // available at this ply, and nothing can be worse than being mated here.
    alpha = std::max(alpha, -MATE_VALUE + ply);
    beta = std::min(beta, MATE_VALUE - ply - 1);
    if (alpha >= beta) return alpha;

    const int alphaOrig = alpha;
    const uint64_t hash = board.getHashKey();

    Move ttMove = 0;
    if (TTEntry* e = probeTT(hash)) {
        ttMove = e->bestMove;
        if (ply > 0 && e->depth >= depth) {
            const int s = scoreFromTT(e->score, ply);
            if (e->flag == TT_EXACT) return s;
            if (e->flag == TT_LOWER && s >= beta) return s;
            if (e->flag == TT_UPPER && s <= alpha) return s;
        }
    }

    const Side us = board.getSideToMove();
    const Side them = (Side)(1 - us);
    const bool inCheck = board.isSquareAttacked(board.getKingSquare(us), them);

    // Check extension, applied before the horizon test so that a check at the
    // frontier is resolved by a real search instead of by quiescence.
    if (inCheck && ply > 0) depth++;

    if (depth <= 0) return quiescence(board, alpha, beta, ply);

    // Null move pruning. Skipped when in check, when a mate score is in play,
    // and when the side to move has nothing but pawns (zugzwang).
    if (!inCheck && depth >= 3 && ply > 0 && beta < MATE_BOUND && board.hasNonPawnMaterial(us)) {
        const int R = 2 + depth / 6;
        board.makeNullMove();
        const int nullScore = -negamax(board, depth - 1 - R, -beta, -beta + 1, ply + 1);
        board.unmakeNullMove();
        if (nullScore >= beta) {
            // Never return an unverified mate score from a null move.
            return (nullScore >= MATE_BOUND) ? beta : nullScore;
        }
    }

    std::vector<Move>& moveList = m_moveBuf[ply];
    std::vector<uint64_t>& ordered = m_orderBuf[ply];
    MoveGenerator::generateMoves(board, moveList);
    orderMoves(moveList, ordered, ttMove, ply, false);

    int best = -INFINITE_SCORE;
    Move bestMove = 0;
    int legalMoves = 0;

    for (uint64_t packed : ordered) {
        const Move move = (Move)(packed & 0xFFFFFFFFULL);
        board.makeMove(move);
        if (board.isSquareAttacked(board.getKingSquare(us), them)) {
            board.unmakeMove(move);
            continue;
        }
        legalMoves++;

        const bool isQuiet = (getCapturedPiece(move) == noPiece) && !isPromotion(move);
        int score;

        if (legalMoves == 1) {
            score = -negamax(board, depth - 1, -beta, -alpha, ply + 1);
        } else {
            int reduction = 0;
            if (depth >= 3 && legalMoves > 3 && isQuiet && !inCheck) {
                reduction = lmr.r[std::min(depth, 63)][std::min(legalMoves, 63)];
                // Clamp AFTER every adjustment. The old code clamped first and
                // then added to the reduction, which could drive the child
                // depth negative and silently skip the reduced search.
                reduction = std::max(0, std::min(reduction, depth - 2));
            }

            score = -negamax(board, depth - 1 - reduction, -alpha - 1, -alpha, ply + 1);
            if (score > alpha && reduction > 0)
                score = -negamax(board, depth - 1, -alpha - 1, -alpha, ply + 1);
            if (score > alpha && score < beta)
                score = -negamax(board, depth - 1, -beta, -alpha, ply + 1);
        }

        board.unmakeMove(move);

        if (score > best) {
            best = score;
            bestMove = move;
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) {
                    if (isQuiet) recordQuietSuccess(move, depth, ply);
                    break;
                }
            }
        }
    }

    if (legalMoves == 0) {
        // Mate distance is measured in plies from the root, not in remaining
        // depth, which extensions and reductions make meaningless.
        return inCheck ? (-MATE_VALUE + ply) : 0;
    }

    const uint8_t flag = (best <= alphaOrig) ? TT_UPPER : (best >= beta ? TT_LOWER : TT_EXACT);
    storeTT(hash, depth, best, flag, bestMove, ply);
    return best;
}

int Search::quiescence(Board& board, int alpha, int beta, int ply) {
    if (ply >= MAX_PLY) return evaluate(board);

    const int standPat = evaluate(board);
    if (standPat >= beta) return standPat;

    // Delta pruning: if even winning a queen would not reach alpha, stop.
    if (standPat + piece_values[4] + 100 < alpha) return alpha;
    if (standPat > alpha) alpha = standPat;

    const Side us = board.getSideToMove();
    const Side them = (Side)(1 - us);

    std::vector<Move>& moveList = m_qMoveBuf[ply];
    std::vector<uint64_t>& ordered = m_qOrderBuf[ply];
    MoveGenerator::generateMoves(board, moveList);
    // Captures and promotions only. Promotions used to be skipped here because
    // the filter tested only for a captured piece.
    orderMoves(moveList, ordered, 0, ply, true);

    for (uint64_t packed : ordered) {
        const Move move = (Move)(packed & 0xFFFFFFFFULL);
        board.makeMove(move);
        if (board.isSquareAttacked(board.getKingSquare(us), them)) {
            board.unmakeMove(move);
            continue;
        }
        const int score = -quiescence(board, -beta, -alpha, ply + 1);
        board.unmakeMove(move);

        if (score >= beta) return score;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

// --- EVALUATION ---
// The score is accumulated from White's point of view and negated at the end
// for Black, so every term must be symmetric between the two colours.

int Search::evaluate(const Board& board) {
    int score = 0;

    for (int i = 0; i < 12; ++i) {
        const pieceType pt = (pieceType)i;
        const Side pieceSide = (i < 6) ? W : B;
        const int* pst = pst_for_piece[i % 6];
        uint64_t bitboard = board.getPieceBitboard(pt);
        while (bitboard) {
            const Square sq = pop_lsb(bitboard);
            const int value = piece_values[i % 6] + pst[pstIndex(sq, pieceSide)];
            score += (pieceSide == W) ? value : -value;
        }
    }

    score += evaluateKingSafety(board);
    score += evaluatePawnStructure(board);
    score += evaluatePieceMobility(board);

    // Bishop pair
    if (popcount(board.getPieceBitboard(whiteBishop)) >= 2) score += 30;
    if (popcount(board.getPieceBitboard(blackBishop)) >= 2) score -= 30;

    // Rooks on semi-open and open files
    const uint64_t whitePawns = board.getPieceBitboard(whitePawn);
    const uint64_t blackPawns = board.getPieceBitboard(blackPawn);

    uint64_t whiteRooks = board.getPieceBitboard(whiteRook);
    while (whiteRooks) {
        const uint64_t fileMask = pawnMasks.file[pop_lsb(whiteRooks) % 8];
        if (!(whitePawns & fileMask)) {
            score += 15;
            if (!(blackPawns & fileMask)) score += 15;
        }
    }

    uint64_t blackRooks = board.getPieceBitboard(blackRook);
    while (blackRooks) {
        const uint64_t fileMask = pawnMasks.file[pop_lsb(blackRooks) % 8];
        if (!(blackPawns & fileMask)) {
            score -= 15;
            if (!(whitePawns & fileMask)) score -= 15;
        }
    }

    // Retaining the right to castle
    if (board.canCastle(W, true) || board.canCastle(W, false)) score += 25;
    if (board.canCastle(B, true) || board.canCastle(B, false)) score -= 25;

    // NOTE: the old "check threat" term added a fixed +50 to the white-relative
    // score whenever the side to move attacked the enemy king. It was both
    // sign-incorrect for Black and unreachable, since evaluate() only ever sees
    // positions the opponent reached legally. It has been removed.

    return (board.getSideToMove() == W) ? score : -score;
}

int Search::evaluateKingSafety(const Board& board) {
    int score = 0;

    const uint64_t wKingZone = MoveGenerator::king_attacks[board.getKingSquare(W)];
    const uint64_t bKingZone = MoveGenerator::king_attacks[board.getKingSquare(B)];

    const uint64_t bQueens = board.getPieceBitboard(blackQueen);
    int whiteKingAttackers =
        popcount(wKingZone & board.getPieceBitboard(blackPawn)) +
        popcount(wKingZone & board.getPieceBitboard(blackKnight)) * 2 +
        popcount(wKingZone & (board.getPieceBitboard(blackBishop) | bQueens)) * 2 +
        popcount(wKingZone & (board.getPieceBitboard(blackRook) | bQueens)) * 3;
    score -= whiteKingAttackers * 15;

    const uint64_t wQueens = board.getPieceBitboard(whiteQueen);
    int blackKingAttackers =
        popcount(bKingZone & board.getPieceBitboard(whitePawn)) +
        popcount(bKingZone & board.getPieceBitboard(whiteKnight)) * 2 +
        popcount(bKingZone & (board.getPieceBitboard(whiteBishop) | wQueens)) * 2 +
        popcount(bKingZone & (board.getPieceBitboard(whiteRook) | wQueens)) * 3;
    score += blackKingAttackers * 15;

    return score;
}

int Search::evaluatePawnStructure(const Board& board) {
    int score = 0;
    const uint64_t whitePawns = board.getPieceBitboard(whitePawn);
    const uint64_t blackPawns = board.getPieceBitboard(blackPawn);

    uint64_t wPawns = whitePawns;
    while (wPawns) {
        const Square sq = pop_lsb(wPawns);
        const int file = sq % 8, rank = sq / 8;

        // Doubled: another friendly pawn further up the same file. Testing for
        // a pawn *ahead* counts each stack once; the old shift-and-mask test
        // simply re-tested the pawn itself and so fired on every pawn.
        if (whitePawns & pawnMasks.frontW[sq]) score -= DOUBLED_PENALTY;

        // Isolated: no friendly pawn anywhere on either adjacent file. The old
        // mask only covered the three squares beside the pawn.
        if (!(whitePawns & pawnMasks.adjacentFiles[file])) score -= ISOLATED_PENALTY;

        // Passed: no enemy pawn ahead on this or an adjacent file, and not
        // blocked by one of our own pawns.
        if (!(blackPawns & pawnMasks.passedW[sq]) && !(whitePawns & pawnMasks.frontW[sq]))
            score += passed_bonus[rank];
    }

    uint64_t bPawns = blackPawns;
    while (bPawns) {
        const Square sq = pop_lsb(bPawns);
        const int file = sq % 8, rank = sq / 8;

        if (blackPawns & pawnMasks.frontB[sq]) score += DOUBLED_PENALTY;
        if (!(blackPawns & pawnMasks.adjacentFiles[file])) score += ISOLATED_PENALTY;
        if (!(whitePawns & pawnMasks.passedB[sq]) && !(blackPawns & pawnMasks.frontB[sq]))
            score -= passed_bonus[7 - rank];
    }

    return score;
}

int Search::evaluatePieceMobility(const Board& board) {
    int score = 0;
    const uint64_t occupied = board.getOccupied();
    const uint64_t whitePieces = board.getPieces(W);
    const uint64_t blackPieces = board.getPieces(B);

    uint64_t bb = board.getPieceBitboard(whiteKnight);
    while (bb) score += popcount(MoveGenerator::knight_attacks[pop_lsb(bb)] & ~whitePieces) * 2;

    bb = board.getPieceBitboard(whiteRook);
    while (bb) { const Square sq = pop_lsb(bb); score += popcount(board.getRookAttacks(sq, occupied) & ~whitePieces); }

    bb = board.getPieceBitboard(whiteBishop);
    while (bb) { const Square sq = pop_lsb(bb); score += popcount(board.getBishopAttacks(sq, occupied) & ~whitePieces); }

    bb = board.getPieceBitboard(blackKnight);
    while (bb) score -= popcount(MoveGenerator::knight_attacks[pop_lsb(bb)] & ~blackPieces) * 2;

    bb = board.getPieceBitboard(blackRook);
    while (bb) { const Square sq = pop_lsb(bb); score -= popcount(board.getRookAttacks(sq, occupied) & ~blackPieces); }

    bb = board.getPieceBitboard(blackBishop);
    while (bb) { const Square sq = pop_lsb(bb); score -= popcount(board.getBishopAttacks(sq, occupied) & ~blackPieces); }

    return score;
}
