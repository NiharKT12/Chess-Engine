#include "MoveGenerator.h"
#include "Board.h" // Include the full definition of the Board class
#include <iostream>
#include <cmath> // For std::abs

#ifdef _MSC_VER // If using the Microsoft Visual C++ compiler
#include <intrin.h> // Include this header for _BitScanForward64
#endif

// Initialize static member variables
uint64_t MoveGenerator::king_attacks[64];
uint64_t MoveGenerator::knight_attacks[64];
uint64_t MoveGenerator::pawn_attacks[2][64];

// Helper masks for preventing wrap-around
const uint64_t notAFile = 0xfefefefefefefefeULL;
const uint64_t notHFile = 0x7f7f7f7f7f7f7f7fULL;

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

// --- INITIALIZATION ---
void MoveGenerator::init() {
    for (int sq = 0; sq < 64; ++sq) {
        initKingAttacks(sq);
        initKnightAttacks(sq);
        initPawnAttacks(sq);
    }
}

void MoveGenerator::initKingAttacks(int sq) {
    uint64_t bitboard = 1ULL << sq;
    uint64_t attacks = 0ULL;
    if ((bitboard << 8) > 0) attacks |= (bitboard << 8); // Up
    if ((bitboard >> 8) > 0) attacks |= (bitboard >> 8); // Down
    if ((bitboard & notAFile) != 0) {
        if ((bitboard >> 1) > 0) attacks |= (bitboard >> 1);  // Left
        if ((bitboard << 7) > 0) attacks |= (bitboard << 7);  // Up-Left
        if ((bitboard >> 9) > 0) attacks |= (bitboard >> 9);  // Down-Left
    }
    if ((bitboard & notHFile) != 0) {
        if ((bitboard << 1) > 0) attacks |= (bitboard << 1);  // Right
        if ((bitboard >> 7) > 0) attacks |= (bitboard >> 7);  // Down-Right
        if ((bitboard << 9) > 0) attacks |= (bitboard << 9);  // Up-Right
    }
    king_attacks[sq] = attacks;
}

void MoveGenerator::initKnightAttacks(int sq) {
    uint64_t b = 1ULL << sq;
    uint64_t attacks = 0;
    if ((b & notHFile) > 0) attacks |= (b << 17);
    if ((b & notAFile) > 0) attacks |= (b << 15);
    if ((b & notHFile & (notHFile - 1)) > 0) attacks |= (b << 10);
    if ((b & notAFile & (notAFile - 1)) > 0) attacks |= (b << 6);
    if ((b & notAFile) > 0) attacks |= (b >> 17);
    if ((b & notHFile) > 0) attacks |= (b >> 15);
    if ((b & notAFile & (notAFile - 1)) > 0) attacks |= (b >> 10);
    if ((b & notHFile & (notHFile - 1)) > 0) attacks |= (b >> 6);
    knight_attacks[sq] = attacks;
}

void MoveGenerator::initPawnAttacks(int sq) {
    uint64_t b = 1ULL << sq;
    uint64_t white_attacks = 0;
    uint64_t black_attacks = 0;
    if ((b & notAFile) > 0) {
        white_attacks |= (b << 7);
        black_attacks |= (b >> 9);
    }
    if ((b & notHFile) > 0) {
        white_attacks |= (b << 9);
        black_attacks |= (b >> 7);
    }
    pawn_attacks[W][sq] = white_attacks;
    pawn_attacks[B][sq] = black_attacks;
}

// --- MAIN MOVE GENERATION ---
void MoveGenerator::generateMoves(const Board& board, std::vector<Move>& moveList) {
    moveList.clear();
    generatePawnMoves(board, moveList);
    generateKnightMoves(board, moveList);
    generateSlidingMoves(board, moveList);
    generateKingMoves(board, moveList);
}

// --- PIECE-SPECIFIC MOVE GENERATION ---
void MoveGenerator::generatePawnMoves(const Board& board, std::vector<Move>& moveList) {
    Side side = board.getSideToMove();
    pieceType pawn = (side == W) ? whitePawn : blackPawn;
    uint64_t pawns = board.getPieceBitboard(pawn);
    uint64_t empty = ~board.getOccupied();
    uint64_t enemies = board.getPieces(side == W ? B : W);
    Square epSquare = board.getEnPassantSquare();

    int pushDir = (side == W) ? 8 : -8;
    // --- FIX: Correct promotion ranks (rank 8 for white, rank 1 for black) ---
    uint64_t promotionRank = (side == W) ? 0xFF00000000000000ULL : 0x00000000000000FFULL;
    uint64_t rank2 = (side == W) ? 0x000000000000FF00ULL : 0x00FF000000000000ULL;
    uint64_t rank3 = (side == W) ? 0x0000000000FF0000ULL : 0x0000FF0000000000ULL;

    // --- Single Pushes ---
    uint64_t singlePushes = (side == W ? (pawns << 8) : (pawns >> 8)) & empty;

    // --- Promotions from single pushes ---
    uint64_t promotions = singlePushes & promotionRank;
    while (promotions) {
        Square to = pop_lsb(promotions);
        Square from = (Square)(to - pushDir);
        moveList.push_back(createMove(from, to, pawn, noPiece, (pieceType)(whiteQueen + 6 * side), QueenPromotion));
        moveList.push_back(createMove(from, to, pawn, noPiece, (pieceType)(whiteRook + 6 * side), RookPromotion));
        moveList.push_back(createMove(from, to, pawn, noPiece, (pieceType)(whiteBishop + 6 * side), BishopPromotion));
        moveList.push_back(createMove(from, to, pawn, noPiece, (pieceType)(whiteKnight + 6 * side), KnightPromotion));
    }

    // --- Non-promotion single pushes ---
    uint64_t nonPromoPushes = singlePushes & ~promotionRank;
    while (nonPromoPushes) {
        Square to = pop_lsb(nonPromoPushes);
        Square from = (Square)(to - pushDir);
        moveList.push_back(createMove(from, to, pawn, noPiece, noPiece, QuietMove));
    }

    // --- FIX: Correct double push logic ---
    uint64_t doublePushPawns = (side == W) ? (pawns & rank2) : (pawns & rank2);
    uint64_t doublePushes = (side == W ? ((doublePushPawns << 8) & empty) << 8 : ((doublePushPawns >> 8) & empty) >> 8) & empty;

    uint64_t pushes = doublePushes;
    while (pushes) {
        Square to = pop_lsb(pushes);
        Square from = (Square)(to - 2 * pushDir);
        moveList.push_back(createMove(from, to, pawn, noPiece, noPiece, DoublePawnPush));
    }

    uint64_t tempPawns = pawns;
    while (tempPawns) {
        Square from = pop_lsb(tempPawns);
        uint64_t attacks = pawn_attacks[side][from] & enemies;
        while (attacks) {
            Square to = pop_lsb(attacks);
            pieceType captured = board.getPieceOnSquare(to);
            if ((1ULL << to) & promotionRank) {
                moveList.push_back(createMove(from, to, pawn, captured, (pieceType)(whiteQueen + 6 * side), QueenPromoCapture));
                moveList.push_back(createMove(from, to, pawn, captured, (pieceType)(whiteRook + 6 * side), RookPromoCapture));
                moveList.push_back(createMove(from, to, pawn, captured, (pieceType)(whiteBishop + 6 * side), BishopPromoCapture));
                moveList.push_back(createMove(from, to, pawn, captured, (pieceType)(whiteKnight + 6 * side), KnightPromoCapture));
            }
            else {
                moveList.push_back(createMove(from, to, pawn, captured, noPiece, Capture));
            }
        }
    }

    if (epSquare != NO_SQUARE) {
        uint64_t attackers = pawn_attacks[(side == W) ? B : W][epSquare] & pawns;
        while (attackers) {
            Square from = pop_lsb(attackers);
            // --- FIX: Correctly identify the captured pawn in en passant ---
            pieceType captured = (side == W) ? blackPawn : whitePawn;
            moveList.push_back(createMove(from, epSquare, pawn, captured, noPiece, EnPassant));
        }
    }
}

void MoveGenerator::generateKnightMoves(const Board& board, std::vector<Move>& moveList) {
    Side side = board.getSideToMove();
    pieceType knight = (side == W) ? whiteKnight : blackKnight;
    uint64_t knights = board.getPieceBitboard(knight);
    uint64_t friendlyPieces = board.getPieces(side);

    while (knights) {
        Square from = pop_lsb(knights);
        uint64_t attacks = knight_attacks[from] & ~friendlyPieces;
        while (attacks) {
            Square to = pop_lsb(attacks);
            pieceType captured = board.getPieceOnSquare(to);
            MoveFlag flag = (captured == noPiece) ? QuietMove : Capture;
            moveList.push_back(createMove(from, to, knight, captured, noPiece, flag));
        }
    }
}

void MoveGenerator::generateKingMoves(const Board& board, std::vector<Move>& moveList) {
    Side side = board.getSideToMove();
    pieceType king = (side == W) ? whiteKing : blackKing;
    uint64_t kings = board.getPieceBitboard(king);
    uint64_t friendlyPieces = board.getPieces(side);

    if (kings) {
        Square from = pop_lsb(kings); // We know there is only one king
        uint64_t attacks = king_attacks[from] & ~friendlyPieces;
        while (attacks) {
            Square to = pop_lsb(attacks);
            pieceType captured = board.getPieceOnSquare(to);
            MoveFlag flag = (captured == noPiece) ? QuietMove : Capture;
            moveList.push_back(createMove(from, to, king, captured, noPiece, flag));
        }
    }

    Side opponentSide = (Side)(1 - side);
    if (board.isSquareAttacked(board.getKingSquare(side), opponentSide)) return;

    if (board.canCastle(side, true)) { // Kingside
        if (side == W) {
            if (!(board.getOccupied() & ((1ULL << F1) | (1ULL << G1)))) {
                if (!board.isSquareAttacked(F1, opponentSide) && !board.isSquareAttacked(G1, opponentSide)) {
                    moveList.push_back(createMove(E1, G1, whiteKing, noPiece, noPiece, KingCastle));
                }
            }
        }
        else {
            if (!(board.getOccupied() & ((1ULL << F8) | (1ULL << G8)))) {
                if (!board.isSquareAttacked(F8, opponentSide) && !board.isSquareAttacked(G8, opponentSide)) {
                    moveList.push_back(createMove(E8, G8, blackKing, noPiece, noPiece, KingCastle));
                }
            }
        }
    }

    if (board.canCastle(side, false)) { // Queenside
        if (side == W) {
            if (!(board.getOccupied() & ((1ULL << D1) | (1ULL << C1) | (1ULL << B1)))) {
                if (!board.isSquareAttacked(D1, opponentSide) && !board.isSquareAttacked(C1, opponentSide)) {
                    moveList.push_back(createMove(E1, C1, whiteKing, noPiece, noPiece, QueenCastle));
                }
            }
        }
        else {
            if (!(board.getOccupied() & ((1ULL << D8) | (1ULL << C8) | (1ULL << B8)))) {
                if (!board.isSquareAttacked(D8, opponentSide) && !board.isSquareAttacked(C8, opponentSide)) {
                    moveList.push_back(createMove(E8, C8, blackKing, noPiece, noPiece, QueenCastle));
                }
            }
        }
    }
}

void MoveGenerator::generateSlidingMoves(const Board& board, std::vector<Move>& moveList) {
    Side side = board.getSideToMove();
    uint64_t friendlyPieces = board.getPieces(side);

    pieceType rook = (side == W) ? whiteRook : blackRook;
    pieceType queen = (side == W) ? whiteQueen : blackQueen;
    pieceType bishop = (side == W) ? whiteBishop : blackBishop;

    uint64_t rooksAndQueens = board.getPieceBitboard(rook) | board.getPieceBitboard(queen);
    while (rooksAndQueens) {
        Square from = pop_lsb(rooksAndQueens);
        pieceType piece = (board.getPieceBitboard(rook) & (1ULL << from)) ? rook : queen;
        uint64_t attacks = board.getRookAttacks(from, board.getOccupied()) & ~friendlyPieces;
        while (attacks) {
            Square to = pop_lsb(attacks);
            pieceType captured = board.getPieceOnSquare(to);
            MoveFlag flag = (captured == noPiece) ? QuietMove : Capture;
            moveList.push_back(createMove(from, to, piece, captured, noPiece, flag));
        }
    }

    uint64_t bishopsAndQueens = board.getPieceBitboard(bishop) | board.getPieceBitboard(queen);
    while (bishopsAndQueens) {
        Square from = pop_lsb(bishopsAndQueens);
        pieceType piece = (board.getPieceBitboard(bishop) & (1ULL << from)) ? bishop : queen;
        uint64_t attacks = board.getBishopAttacks(from, board.getOccupied()) & ~friendlyPieces;
        while (attacks) {
            Square to = pop_lsb(attacks);
            pieceType captured = board.getPieceOnSquare(to);
            MoveFlag flag = (captured == noPiece) ? QuietMove : Capture;
            moveList.push_back(createMove(from, to, piece, captured, noPiece, flag));
        }
    }
}

