#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Type.h"

// --- History Struct ---
// Used to store previous game states for unmaking moves.
struct BoardState {
    uint8_t castlingRights;
    Square enPassantSquare;
    uint64_t hashKey; // Store the hash key for perfect undos
};

class MoveGenerator;

class Board {
public:
    Board();
    void setupFromFen(const std::string& fen);
    void makeMove(Move m);
    void unmakeMove(Move m);
    uint64_t generateHashKey() const;

    // --- Getters ---
    uint64_t getPieceBitboard(pieceType pt) const;
    uint64_t getWhitePieces() const;
    uint64_t getBlackPieces() const;
    uint64_t getOccupied() const;
    uint64_t getPieces(Side side) const;
    Side getSideToMove() const;
    Square getKingSquare(Side side) const;
    Square getEnPassantSquare() const;
    bool canCastle(Side side, bool kingside) const;
    pieceType getPieceOnSquare(Square sq) const;
    bool isSquareAttacked(Square sq, Side attackerSide) const;

    // Getter for the current position's hash key
    uint64_t getHashKey() const { return m_hashKey; }


    // --- NEW: Make attack calculation methods available for evaluation ---
    uint64_t getRookAttacks(Square sq, uint64_t occupied) const;
    uint64_t getBishopAttacks(Square sq, uint64_t occupied) const;
    
    // --- NEW: Null move for null move pruning ---
    void makeNullMove();
    void unmakeNullMove();

private:
    void removePiece(pieceType pt, Square sq);
    void addPiece(pieceType pt, Square sq);

    uint64_t m_pieceBitboards[12];
    Side m_sideToMove;
    uint8_t m_castlingRights;
    Square m_enPassantSquare;
    Square m_kingSquare[2];
    uint64_t m_hashKey;
    std::vector<BoardState> m_history;

    friend class MoveGenerator;
};

