#include "Board.h"
#include "Type.h"
#include "Zobrist.h"
#include "MoveGenerator.h"
#include <algorithm>
#include <sstream>
#include <vector>

Board::Board() { setupFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); }

// Resets every field to an empty board. Kings default to their home squares so
// that a malformed FEN with no king cannot produce an out-of-range king square.
void Board::clear() {
    for (int i = 0; i < 12; ++i) m_pieceBitboards[i] = 0ULL;
    m_sideBitboards[W] = m_sideBitboards[B] = 0ULL;
    m_occupied = 0ULL;
    for (int i = 0; i < 64; ++i) m_board[i] = noPiece;
    m_sideToMove = W;
    m_castlingRights = 0;
    m_enPassantSquare = NO_SQUARE;
    m_kingSquare[W] = E1;
    m_kingSquare[B] = E8;
    m_halfmoveClock = 0;
    m_hashKey = 0ULL;
    m_history.clear();
}

static pieceType charToPiece(char c) {
    switch (c) {
    case 'P': return whitePawn;   case 'N': return whiteKnight; case 'B': return whiteBishop;
    case 'R': return whiteRook;   case 'Q': return whiteQueen;  case 'K': return whiteKing;
    case 'p': return blackPawn;   case 'n': return blackKnight; case 'b': return blackBishop;
    case 'r': return blackRook;   case 'q': return blackQueen;  case 'k': return blackKing;
    default:  return noPiece;
    }
}

void Board::setupFromFen(const std::string& fen) {
    clear();

    // Tokenised parsing: tolerates FENs that omit the trailing move counters and
    // never reads past the end of the string.
    std::istringstream ss(fen);
    std::string placement, sideToMove, castling, enPassant;
    int halfmove = 0, fullmove = 1;

    if (!(ss >> placement)) return;
    if (!(ss >> sideToMove)) sideToMove = "w";
    if (!(ss >> castling)) castling = "-";
    if (!(ss >> enPassant)) enPassant = "-";
    if (!(ss >> halfmove)) halfmove = 0;
    if (!(ss >> fullmove)) fullmove = 1;

    int rank = 7, file = 0;
    for (char c : placement) {
        if (c == '/') { rank--; file = 0; continue; }
        if (c >= '1' && c <= '8') { file += (c - '0'); continue; }

        pieceType pt = charToPiece(c);
        if (pt != noPiece && rank >= 0 && rank < 8 && file >= 0 && file < 8) {
            Square sq = (Square)(rank * 8 + file);
            addPiece(pt, sq);
            if (pt == whiteKing) m_kingSquare[W] = sq;
            else if (pt == blackKing) m_kingSquare[B] = sq;
        }
        file++;
    }

    m_sideToMove = (sideToMove == "b") ? B : W;

    for (char c : castling) {
        if (c == 'K') m_castlingRights |= 1;
        else if (c == 'Q') m_castlingRights |= 2;
        else if (c == 'k') m_castlingRights |= 4;
        else if (c == 'q') m_castlingRights |= 8;
    }

    if (enPassant.size() >= 2 && enPassant[0] != '-') {
        int epFile = enPassant[0] - 'a';
        int epRank = enPassant[1] - '1';
        if (epFile >= 0 && epFile < 8 && epRank >= 0 && epRank < 8)
            m_enPassantSquare = (Square)(epRank * 8 + epFile);
    }

    m_halfmoveClock = (uint16_t)(halfmove > 0 ? halfmove : 0);
    m_hashKey = generateHashKey();
}

bool Board::canCastle(Side side, bool kingside) const {
    return (m_castlingRights & ((side == W) ? (kingside ? 1 : 2) : (kingside ? 4 : 8))) != 0;
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
    m_history.push_back({ m_hashKey, m_enPassantSquare, m_castlingRights, m_halfmoveClock });

    Square from = getFromSquare(m), to = getToSquare(m);
    pieceType pieceMoved = getPieceMoved(m), pieceCaptured = getCapturedPiece(m);
    MoveFlag flag = getMoveFlag(m);

    if (m_enPassantSquare != NO_SQUARE) m_hashKey ^= Zobrist::enPassantKeys[m_enPassantSquare % 8];
    m_hashKey ^= Zobrist::castlingKeys[m_castlingRights];

    m_castlingRights &= ~((from == E1 || to == E1) * 3 | (from == H1 || to == H1) * 1 | (from == A1 || to == A1) * 2 | (from == E8 || to == E8) * 12 | (from == H8 || to == H8) * 4 | (from == A8 || to == A8) * 8);
    m_castlingRights &= ~((pieceCaptured == whiteRook && to == H1) * 1 | (pieceCaptured == whiteRook && to == A1) * 2 | (pieceCaptured == blackRook && to == H8) * 4 | (pieceCaptured == blackRook && to == A8) * 8);

    m_hashKey ^= Zobrist::castlingKeys[m_castlingRights];
    m_enPassantSquare = NO_SQUARE;

    // The fifty-move counter resets on any capture or pawn move.
    if (pieceCaptured != noPiece || pieceMoved == whitePawn || pieceMoved == blackPawn) m_halfmoveClock = 0;
    else m_halfmoveClock++;

    // Remove the captured piece BEFORE relocating the mover. For an ordinary
    // capture both occupy the destination square, and clearing the victim
    // afterwards would wipe the mailbox entry the mover just wrote.
    if (pieceCaptured != noPiece) {
        Square capturedSq = to;
        if (flag == EnPassant) {
            capturedSq = (m_sideToMove == W) ? (Square)(to - 8) : (Square)(to + 8);
        }
        removePiece(pieceCaptured, capturedSq);
        m_hashKey ^= Zobrist::pieceKeys[pieceCaptured][capturedSq];
    }

    removePiece(pieceMoved, from);
    addPiece(pieceMoved, to);
    m_hashKey ^= Zobrist::pieceKeys[pieceMoved][from] ^ Zobrist::pieceKeys[pieceMoved][to];

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

    // For a promotion the piece standing on the destination is the promoted one.
    if (flag >= KnightPromotion) {
        removePiece(getPromotionPiece(m), to);
        addPiece(pieceMoved, from);
    } else {
        removePiece(pieceMoved, to);
        addPiece(pieceMoved, from);
    }

    // Put any captured piece back. The destination has already been vacated
    // above, so writing the victim's mailbox entry here is safe.
    if (pieceCaptured != noPiece) {
        Square capturedSq = to;
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

    if (pieceMoved == whiteKing) m_kingSquare[W] = from;
    else if (pieceMoved == blackKing) m_kingSquare[B] = from;

    m_castlingRights = oldState.castlingRights;
    m_enPassantSquare = oldState.enPassantSquare;
    m_halfmoveClock = oldState.halfmoveClock;
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

// Positions can only repeat across an unbroken run of reversible moves, so the
// scan is bounded by the halfmove clock. Positions with the same side to move
// are two plies apart, hence the stride of 2.
bool Board::isRepetition() const {
    int historySize = (int)m_history.size();
    int limit = std::min((int)m_halfmoveClock, historySize);
    for (int i = historySize - 2; i >= historySize - limit && i >= 0; i -= 2) {
        if (m_history[i].hashKey == m_hashKey) return true;
    }
    return false;
}

bool Board::isInsufficientMaterial() const {
    // Any pawn, rook or queen anywhere means mate is still possible.
    if (m_pieceBitboards[whitePawn] | m_pieceBitboards[blackPawn] |
        m_pieceBitboards[whiteRook] | m_pieceBitboards[blackRook] |
        m_pieceBitboards[whiteQueen] | m_pieceBitboards[blackQueen]) return false;

    int minors = popcount(m_pieceBitboards[whiteKnight] | m_pieceBitboards[whiteBishop] |
                          m_pieceBitboards[blackKnight] | m_pieceBitboards[blackBishop]);
    return minors <= 1;  // K vs K, or K + a single minor vs K
}

bool Board::hasNonPawnMaterial(Side side) const {
    int base = (side == W) ? 0 : 6;
    return (m_pieceBitboards[base + whiteKnight] | m_pieceBitboards[base + whiteBishop] |
            m_pieceBitboards[base + whiteRook]   | m_pieceBitboards[base + whiteQueen]) != 0ULL;
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
    const uint64_t bit = 1ULL << sq;
    m_pieceBitboards[pt] |= bit;
    m_sideBitboards[pt / 6] |= bit;
    m_occupied |= bit;
    m_board[sq] = pt;
}

void Board::removePiece(pieceType pt, Square sq) {
    const uint64_t bit = 1ULL << sq;
    m_pieceBitboards[pt] &= ~bit;
    m_sideBitboards[pt / 6] &= ~bit;
    m_occupied &= ~bit;
    m_board[sq] = noPiece;
}

// --- Null move implementation for null move pruning ---
void Board::makeNullMove() {
    m_history.push_back({ m_hashKey, m_enPassantSquare, m_castlingRights, m_halfmoveClock });

    m_sideToMove = (Side)(1 - m_sideToMove);
    m_hashKey ^= Zobrist::blackToMoveKey;

    if (m_enPassantSquare != NO_SQUARE) {
        m_hashKey ^= Zobrist::enPassantKeys[m_enPassantSquare % 8];
        m_enPassantSquare = NO_SQUARE;
    }

    // A null move does not produce a real game position, so stop any repetition
    // scan from reaching back across it.
    m_halfmoveClock = 0;
}

void Board::unmakeNullMove() {
    BoardState state = m_history.back();
    m_history.pop_back();

    m_sideToMove = (Side)(1 - m_sideToMove);
    m_castlingRights = state.castlingRights;
    m_enPassantSquare = state.enPassantSquare;
    m_halfmoveClock = state.halfmoveClock;
    m_hashKey = state.hashKey;
}
