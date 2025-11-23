#pragma once
#include <cstdint>

// Enum for the side to move (White or Black)
enum Side {
    W = 0, B = 1
};

// Enum for all 12 piece types, plus a value for no piece
enum pieceType {
    whitePawn = 0, whiteKnight, whiteBishop, whiteRook, whiteQueen, whiteKing,
    blackPawn = 6, blackKnight, blackBishop, blackRook, blackQueen, blackKing,
    noPiece
};

// Enum for all 64 squares on the board, plus a value for no square
enum Square {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    NO_SQUARE
};

// Flags to encode special move types like castling, promotions, etc.
enum MoveFlag {
    QuietMove = 0,
    DoublePawnPush = 1,
    KingCastle = 2,
    QueenCastle = 3,
    Capture = 4,
    EnPassant = 5,
    KnightPromotion = 8, BishopPromotion = 9, RookPromotion = 10, QueenPromotion = 11,
    KnightPromoCapture = 12, BishopPromoCapture = 13, RookPromoCapture = 14, QueenPromoCapture = 15
};

// A move is packed into a 32-bit integer for efficiency.
using Move = uint32_t;

// --- Functions to pack and unpack move data ---

// Creates a move integer from its component parts.
inline Move createMove(Square from, Square to, pieceType piece, pieceType captured, pieceType promotion, MoveFlag flags) {
    return from | (to << 6) | (piece << 12) | (captured << 16) | (promotion << 20) | (flags << 24);
}

// Extracts the 'from' square (bits 0-5)
inline Square getFromSquare(Move move) { return (Square)(move & 0x3F); }
// Extracts the 'to' square (bits 6-11)
inline Square getToSquare(Move move) { return (Square)((move >> 6) & 0x3F); }
// Extracts the piece that moved (bits 12-15)
inline pieceType getPieceMoved(Move move) { return (pieceType)((move >> 12) & 0xF); }
// Extracts the captured piece (bits 16-19)
inline pieceType getCapturedPiece(Move move) { return (pieceType)((move >> 16) & 0xF); }
// Extracts the promotion piece (bits 20-23)
inline pieceType getPromotionPiece(Move move) { return (pieceType)((move >> 20) & 0xF); }
// Extracts the move flag (bits 24-27)
inline MoveFlag getMoveFlag(Move move) { return (MoveFlag)((move >> 24) & 0xF); }

