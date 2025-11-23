#include "Board.h"
#include "Type.h"
#include "Zobrist.h"
#include "MoveGenerator.h"
#include <vector>
#include <cmath>

// Helper function
inline Square pop_lsb(uint64_t& bitboard) {
    if (bitboard == 0) return NO_SQUARE;
#ifdef _MSC_VER
    unsigned long index;
    _BitScanForward64(&index, bitboard);
#else
    unsigned long index = __builtin_ctzll(bitboard);
#endif
    bitboard &= bitboard - 1;
    return (Square)index;
}

Board::Board() { setupFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); }

void Board::setupFromFen(const std::string& fen) {
    for (int i = 0; i < 12; ++i) { m_pieceBitboards[i] = 0ULL; }
    m_history.clear();

    int rank = 7, file = 0;
    size_t i = 0;

    for (; i < fen.length(); ++i) {
        char c = fen[i];
        if (c == ' ') break;

        if (isalpha(c)) {
            Square sq = (Square)(rank * 8 + file);
            pieceType pt = noPiece;
            if (c >= 'a' && c <= 'z') { // Black pieces
                if (c == 'p') pt = blackPawn;
                else if (c == 'n') pt = blackKnight;
                else if (c == 'b') pt = blackBishop;
                else if (c == 'r') pt = blackRook;
                else if (c == 'q') pt = blackQueen;
                else if (c == 'k') { pt = blackKing; m_kingSquare[B] = sq; }
            } else { // White pieces
                if (c == 'P') pt = whitePawn;
                else if (c == 'N') pt = whiteKnight;
                else if (c == 'B') pt = whiteBishop;
                else if (c == 'R') pt = whiteRook;
                else if (c == 'Q') pt = whiteQueen;
                else if (c == 'K') { pt = whiteKing; m_kingSquare[W] = sq; }
            }
            if (pt != noPiece) addPiece(pt, sq);
            file++;
        }
        else if (isdigit(c)) { file += (c - '0'); }
        else if (c == '/') { rank--; file = 0; }
    }
    i++;

    m_sideToMove = (fen[i] == 'w') ? W : B;
    i += 2;

    m_castlingRights = 0;
    for (; i < fen.length(); ++i) {
        char c = fen[i];
        if (c == ' ') break;
        if (c == 'K') m_castlingRights |= 1; else if (c == 'Q') m_castlingRights |= 2;
        else if (c == 'k') m_castlingRights |= 4; else if (c == 'q') m_castlingRights |= 8;
    }
    i++;

    if (fen[i] != '-') {
        m_enPassantSquare = (Square)((fen[i + 1] - '1') * 8 + (fen[i] - 'a'));
    }
    else {
        m_enPassantSquare = NO_SQUARE;
    }
    m_hashKey = generateHashKey();
}

uint64_t Board::getPieceBitboard(pieceType pt) const { return Board::m_pieceBitboards[pt]; }
uint64_t Board::getWhitePieces() const { return Board::m_pieceBitboards[0] | m_pieceBitboards[1] | m_pieceBitboards[2] | m_pieceBitboards[3] | m_pieceBitboards[4] | m_pieceBitboards[5]; }
uint64_t Board::getBlackPieces() const { return m_pieceBitboards[6] | m_pieceBitboards[7] | m_pieceBitboards[8] | m_pieceBitboards[9] | m_pieceBitboards[10] | m_pieceBitboards[11]; }
uint64_t Board::getOccupied() const { return getWhitePieces() | getBlackPieces(); }
uint64_t Board::getPieces(Side side) const { return (side == W) ? getWhitePieces() : getBlackPieces(); }
Side Board::getSideToMove() const { return m_sideToMove; }
Square Board::getKingSquare(Side side) const { return m_kingSquare[side]; }
Square Board::getEnPassantSquare() const { return m_enPassantSquare; }
bool Board::canCastle(Side side, bool kingside) const { return m_castlingRights & ((side == W) ? (kingside ? 1 : 2) : (kingside ? 4 : 8)); }

pieceType Board::getPieceOnSquare(Square sq) const {
    if (sq == NO_SQUARE) return noPiece;
    uint64_t bit = 1ULL << sq;
    for (int i = 0; i < 12; ++i) { if (m_pieceBitboards[i] & bit) return (pieceType)i; }
    return noPiece;
}

bool Board::isSquareAttacked(Square sq, Side attackerSide) const {
    Side victimSide = (attackerSide == W) ? B : W;
    if ((MoveGenerator::pawn_attacks[victimSide][sq] & m_pieceBitboards[(attackerSide == W) ? whitePawn : blackPawn]) != 0) return true;
    if ((MoveGenerator::knight_attacks[sq] & m_pieceBitboards[(attackerSide == W) ? whiteKnight : blackKnight]) != 0) return true;
    if ((MoveGenerator::king_attacks[sq] & m_pieceBitboards[(attackerSide == W) ? whiteKing : blackKing]) != 0) return true;
    uint64_t occupied = getOccupied();
    uint64_t rookAndQueen = m_pieceBitboards[(attackerSide == W) ? whiteRook : blackRook] | m_pieceBitboards[(attackerSide == W) ? whiteQueen : blackQueen];
    if ((getRookAttacks(sq, occupied) & rookAndQueen) != 0) return true;
    uint64_t bishopAndQueen = m_pieceBitboards[(attackerSide == W) ? whiteBishop : blackBishop] | m_pieceBitboards[(attackerSide == W) ? whiteQueen : blackQueen];
    if ((getBishopAttacks(sq, occupied) & bishopAndQueen) != 0) return true;
    return false;
}

void Board::makeMove(Move m) {
    m_history.push_back({ m_castlingRights, m_enPassantSquare, m_hashKey });
    Square from = getFromSquare(m), to = getToSquare(m);
    pieceType pieceMoved = getPieceMoved(m), pieceCaptured = getCapturedPiece(m);
    MoveFlag flag = getMoveFlag(m);

    if (m_enPassantSquare != NO_SQUARE) m_hashKey ^= Zobrist::enPassantKeys[m_enPassantSquare % 8];
    m_hashKey ^= Zobrist::castlingKeys[m_castlingRights];

    m_castlingRights &= ~((from == E1 || to == E1) * 3 | (from == H1 || to == H1) * 1 | (from == A1 || to == A1) * 2 | (from == E8 || to == E8) * 12 | (from == H8 || to == H8) * 4 | (from == A8 || to == A8) * 8);
    m_castlingRights &= ~((pieceCaptured == whiteRook && to == H1) * 1 | (pieceCaptured == whiteRook && to == A1) * 2 | (pieceCaptured == blackRook && to == H8) * 4 | (pieceCaptured == blackRook && to == A8) * 8);

    m_hashKey ^= Zobrist::castlingKeys[m_castlingRights];
    m_enPassantSquare = NO_SQUARE;

    removePiece(pieceMoved, from);
    addPiece(pieceMoved, to);

    m_hashKey ^= Zobrist::pieceKeys[pieceMoved][from] ^ Zobrist::pieceKeys[pieceMoved][to];

    if (pieceCaptured != noPiece) {
        Square capturedSq = to;
        if (flag == EnPassant) {
            capturedSq = (m_sideToMove == W) ? (Square)(to - 8) : (Square)(to + 8);
        }
        removePiece(pieceCaptured, capturedSq);
        m_hashKey ^= Zobrist::pieceKeys[pieceCaptured][capturedSq];
    }
    if (flag == DoublePawnPush) {
        m_enPassantSquare = (m_sideToMove == W) ? (Square)(from + 8) : (Square)(from - 8);
        m_hashKey ^= Zobrist::enPassantKeys[m_enPassantSquare % 8];
    }
    else if (flag == KingCastle || flag == QueenCastle) {
        bool kingside = flag == KingCastle;
        Square rookFrom = (m_sideToMove == W) ? (kingside ? H1 : A1) : (kingside ? H8 : A8);
        Square rookTo = (m_sideToMove == W) ? (kingside ? F1 : D1) : (kingside ? F8 : D8);
        pieceType rook = (m_sideToMove == W) ? whiteRook : blackRook;
        removePiece(rook, rookFrom);
        addPiece(rook, rookTo);
        m_hashKey ^= Zobrist::pieceKeys[rook][rookFrom] ^ Zobrist::pieceKeys[rook][rookTo];
    }
    else if (flag >= KnightPromotion) {
        pieceType promotionPiece = getPromotionPiece(m);
        removePiece(pieceMoved, to);
        addPiece(promotionPiece, to);
        m_hashKey ^= Zobrist::pieceKeys[pieceMoved][to] ^ Zobrist::pieceKeys[promotionPiece][to];
    }

    if (pieceMoved == whiteKing) m_kingSquare[W] = to; else if (pieceMoved == blackKing) m_kingSquare[B] = to;
    m_sideToMove = (Side)(1 - m_sideToMove);
    m_hashKey ^= Zobrist::blackToMoveKey;
}

void Board::unmakeMove(Move m) {
    BoardState oldState = m_history.back();
    m_history.pop_back();
    m_sideToMove = (Side)(1 - m_sideToMove);

    Square from = getFromSquare(m), to = getToSquare(m);
    pieceType pieceMoved = getPieceMoved(m), pieceCaptured = getCapturedPiece(m);
    MoveFlag flag = getMoveFlag(m);

    // If it was a promotion, handle it specially
    if (flag >= KnightPromotion) {
        pieceType promotionPiece = getPromotionPiece(m);
        removePiece(promotionPiece, to);  // Remove the promoted piece from 'to'
        addPiece(pieceMoved, from);       // Add the original pawn back to 'from'
    } else {
        // Normal move: move piece back from 'to' to 'from'
        removePiece(pieceMoved, to);
        addPiece(pieceMoved, from);
    }

    // If a piece was captured, add it back to the board
    if (pieceCaptured != noPiece) {
        Square capturedSq = to;
        // Special handling for en passant captures
        if (flag == EnPassant) {
            capturedSq = (m_sideToMove == W) ? (Square)(to - 8) : (Square)(to + 8);
        }
        addPiece(pieceCaptured, capturedSq);
    }

    // If it was a castling move, move the rook back as well
    if (flag == KingCastle || flag == QueenCastle) {
        bool kingside = flag == KingCastle;
        Square rookFrom = (m_sideToMove == W) ? (kingside ? H1 : A1) : (kingside ? H8 : A8);
        Square rookTo = (m_sideToMove == W) ? (kingside ? F1 : D1) : (kingside ? F8 : D8);
        pieceType rook = (m_sideToMove == W) ? whiteRook : blackRook;
        removePiece(rook, rookTo);
        addPiece(rook, rookFrom);
    }

    // Restore king's square
    if (pieceMoved == whiteKing) m_kingSquare[W] = from;
    else if (pieceMoved == blackKing) m_kingSquare[B] = from;

    // Restore the previous board state (castling rights, en passant square, hash key)
    m_castlingRights = oldState.castlingRights;
    m_enPassantSquare = oldState.enPassantSquare;
    m_hashKey = oldState.hashKey;
}

uint64_t Board::generateHashKey() const {
    uint64_t finalKey = 0;
    for (int i = 0; i < 12; ++i) {
        uint64_t bitboard = m_pieceBitboards[i];
        while (bitboard) {
            finalKey ^= Zobrist::pieceKeys[i][pop_lsb(bitboard)];
        }
    }
    if (m_enPassantSquare != NO_SQUARE) finalKey ^= Zobrist::enPassantKeys[m_enPassantSquare % 8];
    if (m_sideToMove == B) finalKey ^= Zobrist::blackToMoveKey;
    finalKey ^= Zobrist::castlingKeys[m_castlingRights];
    return finalKey;
}

uint64_t Board::getRookAttacks(Square sq, uint64_t occupied) const {
    uint64_t attacks = 0ULL;
    const int r = sq / 8;
    const int f = sq % 8;

    // Horizontal right
    for (int i = f + 1; i < 8; ++i) {
        attacks |= (1ULL << (r * 8 + i));
        if ((1ULL << (r * 8 + i)) & occupied) break;
    }
    // Horizontal left
    for (int i = f - 1; i >= 0; --i) {
        attacks |= (1ULL << (r * 8 + i));
        if ((1ULL << (r * 8 + i)) & occupied) break;
    }
    // Vertical up
    for (int i = r + 1; i < 8; ++i) {
        attacks |= (1ULL << (i * 8 + f));
        if ((1ULL << (i * 8 + f)) & occupied) break;
    }
    // Vertical down
    for (int i = r - 1; i >= 0; --i) {
        attacks |= (1ULL << (i * 8 + f));
        if ((1ULL << (i * 8 + f)) & occupied) break;
    }
    return attacks;
}

uint64_t Board::getBishopAttacks(Square sq, uint64_t occupied) const {
    uint64_t attacks = 0ULL;
    const int r = sq / 8;
    const int f = sq % 8;

    // Up-Right
    for (int i = 1; r + i < 8 && f + i < 8; ++i) {
        attacks |= (1ULL << (sq + i * 9));
        if ((1ULL << (sq + i * 9)) & occupied) break;
    }
    // Down-Left
    for (int i = 1; r - i >= 0 && f - i >= 0; ++i) {
        attacks |= (1ULL << (sq - i * 9));
        if ((1ULL << (sq - i * 9)) & occupied) break;
    }
    // Up-Left
    for (int i = 1; r + i < 8 && f - i >= 0; ++i) {
        attacks |= (1ULL << (sq + i * 7));
        if ((1ULL << (sq + i * 7)) & occupied) break;
    }
    // Down-Right
    for (int i = 1; r - i >= 0 && f + i < 8; ++i) {
        attacks |= (1ULL << (sq - i * 7));
        if ((1ULL << (sq - i * 7)) & occupied) break;
    }
    return attacks;
}

void Board::addPiece(pieceType pt, Square sq) {
    m_pieceBitboards[pt] |= (1ULL << sq);
}

void Board::removePiece(pieceType pt, Square sq) {
    m_pieceBitboards[pt] &= ~(1ULL << sq);
}

