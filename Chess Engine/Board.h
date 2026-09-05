#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Type.h"

// --- History Struct ---
// Used to store previous game states for unmaking moves.
struct BoardState {
    uint64_t hashKey;          // Hash of the position *before* the move was made
    Square enPassantSquare;
    uint8_t castlingRights;
    uint16_t halfmoveClock;
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
    // Occupancy is now maintained incrementally, so these are all O(1).
    uint64_t getPieceBitboard(pieceType pt) const { return m_pieceBitboards[pt]; }
    uint64_t getWhitePieces() const { return m_sideBitboards[W]; }
    uint64_t getBlackPieces() const { return m_sideBitboards[B]; }
    uint64_t getOccupied() const { return m_occupied; }
    uint64_t getPieces(Side side) const { return m_sideBitboards[side]; }
    Side getSideToMove() const { return m_sideToMove; }
    Square getKingSquare(Side side) const { return m_kingSquare[side]; }
    Square getEnPassantSquare() const { return m_enPassantSquare; }
    bool canCastle(Side side, bool kingside) const;
    // Backed by a mailbox array instead of scanning all 12 bitboards.
    pieceType getPieceOnSquare(Square sq) const { return (sq == NO_SQUARE) ? noPiece : m_board[sq]; }
    bool isSquareAttacked(Square sq, Side attackerSide) const;

    // Getter for the current position's hash key
    uint64_t getHashKey() const { return m_hashKey; }

    // Plies since the last capture or pawn move (the fifty-move counter).
    int getHalfmoveClock() const { return m_halfmoveClock; }

    // --- Draw detection ---
    // True if the current position has occurred before within the current
    // reversible-move window. Search treats a single repetition as a draw.
    bool isRepetition() const;
    // True if neither side has enough material to force mate (K vs K, K+minor vs K).
    bool isInsufficientMaterial() const;
    // True if the side has at least one piece that is not a pawn or the king.
    // Used to avoid null-move pruning in zugzwang-prone endgames.
    bool hasNonPawnMaterial(Side side) const;

    // --- Attack calculation, also used by the evaluation ---
    uint64_t getRookAttacks(Square sq, uint64_t occupied) const;
    uint64_t getBishopAttacks(Square sq, uint64_t occupied) const;

    // --- Null move for null move pruning ---
    void makeNullMove();
    void unmakeNullMove();

private:
    void clear();
    void removePiece(pieceType pt, Square sq);
    void addPiece(pieceType pt, Square sq);

    uint64_t m_pieceBitboards[12];
    uint64_t m_sideBitboards[2];   // Cached per-side occupancy
    uint64_t m_occupied;           // Cached total occupancy
    pieceType m_board[64];         // Mailbox: which piece sits on each square
    Side m_sideToMove;
    uint8_t m_castlingRights;
    Square m_enPassantSquare;
    Square m_kingSquare[2];
    uint16_t m_halfmoveClock;
    uint64_t m_hashKey;
    std::vector<BoardState> m_history;

    friend class MoveGenerator;
};
