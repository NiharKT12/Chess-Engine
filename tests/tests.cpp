// Regression tests for the engine core. Deliberately free of raylib so the
// whole suite builds and runs from the command line:
//
//     tests/run_tests.sh          (bash / MSYS2)
//     tests\run_tests.bat         (MSVC developer prompt)
//
// The engine's internals are exercised directly. Opening up access for the test
// binary keeps the production headers free of test-only accessors; Search.cpp
// itself is compiled normally.
#define private public
#include "Search.h"
#undef private

#include "Board.h"
#include "MoveGenerator.h"
#include "Zobrist.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

static void check(bool ok, const std::string& what) {
    g_checks++;
    if (!ok) {
        g_failures++;
        std::printf("  FAIL  %s\n", what.c_str());
    }
}

static void checkEq(long long got, long long want, const std::string& what) {
    g_checks++;
    if (got != want) {
        g_failures++;
        std::printf("  FAIL  %s  (got %lld, want %lld)\n", what.c_str(), got, want);
    }
}

static const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// ---------------------------------------------------------------------------
// Perft: the ground truth for move generation, make/unmake and legality.
// ---------------------------------------------------------------------------

static uint64_t perft(Board& b, int depth) {
    if (depth == 0) return 1;
    std::vector<Move> moves;
    MoveGenerator::generateMoves(b, moves);
    const Side us = b.getSideToMove();
    const Side them = (Side)(1 - us);
    uint64_t nodes = 0;
    for (Move m : moves) {
        b.makeMove(m);
        if (!b.isSquareAttacked(b.getKingSquare(us), them)) nodes += perft(b, depth - 1);
        b.unmakeMove(m);
    }
    return nodes;
}

static void testPerft() {
    std::printf("perft\n");
    struct Case { const char* fen; int depth; uint64_t expected; const char* name; };
    const Case cases[] = {
        {START_FEN, 4, 197281,  "startpos depth 4"},
        {START_FEN, 5, 4865609, "startpos depth 5"},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603, "kiwipete depth 4"},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624, "position 3 depth 5"},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333, "position 4 depth 4"},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487, "position 5 depth 4"},
        {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594, "position 6 depth 4"},
    };
    for (const Case& c : cases) {
        Board b;
        b.setupFromFen(c.fen);
        checkEq((long long)perft(b, c.depth), (long long)c.expected, c.name);
    }
}

// ---------------------------------------------------------------------------
// The incremental Zobrist key, the cached occupancy bitboards and the mailbox
// must all agree with a from-scratch recomputation after every single move.
// ---------------------------------------------------------------------------

static bool stateIsConsistent(const Board& b) {
    if (b.getHashKey() != b.generateHashKey()) return false;

    uint64_t white = 0, black = 0;
    for (int i = 0; i < 6; ++i) white |= b.m_pieceBitboards[i];
    for (int i = 6; i < 12; ++i) black |= b.m_pieceBitboards[i];
    if (b.getWhitePieces() != white) return false;
    if (b.getBlackPieces() != black) return false;
    if (b.getOccupied() != (white | black)) return false;

    for (int sq = 0; sq < 64; ++sq) {
        pieceType expected = noPiece;
        for (int i = 0; i < 12; ++i)
            if (b.m_pieceBitboards[i] & (1ULL << sq)) { expected = (pieceType)i; break; }
        if (b.getPieceOnSquare((Square)sq) != expected) return false;
    }
    return true;
}

static bool walkAndVerify(Board& b, int depth) {
    if (depth == 0) return true;
    std::vector<Move> moves;
    MoveGenerator::generateMoves(b, moves);
    const Side us = b.getSideToMove();
    const Side them = (Side)(1 - us);
    for (Move m : moves) {
        const uint64_t before = b.getHashKey();
        b.makeMove(m);
        bool ok = stateIsConsistent(b);
        if (ok && !b.isSquareAttacked(b.getKingSquare(us), them)) ok = walkAndVerify(b, depth - 1);
        b.unmakeMove(m);
        // unmakeMove must restore the position exactly.
        if (ok && b.getHashKey() != before) ok = false;
        if (ok && !stateIsConsistent(b)) ok = false;
        if (!ok) return false;
    }
    return true;
}

static void testStateConsistency() {
    std::printf("board state consistency\n");
    const char* fens[] = {
        START_FEN,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    };
    for (const char* fen : fens) {
        Board b;
        b.setupFromFen(fen);
        check(walkAndVerify(b, 3), std::string("hash/occupancy/mailbox consistent: ") + fen);
    }
}

// ---------------------------------------------------------------------------
// FEN round-tripping of the fields the engine actually reads.
// ---------------------------------------------------------------------------

static void testFenParsing() {
    std::printf("fen parsing\n");
    Board b;

    b.setupFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    checkEq(b.getSideToMove(), W, "startpos side to move");
    checkEq(b.getHalfmoveClock(), 0, "startpos halfmove clock");
    checkEq(b.getEnPassantSquare(), NO_SQUARE, "startpos has no en passant square");
    check(b.canCastle(W, true) && b.canCastle(W, false) &&
          b.canCastle(B, true) && b.canCastle(B, false), "startpos castling rights");

    b.setupFromFen("rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq g3 0 2");
    checkEq(b.getSideToMove(), B, "black to move parsed");
    checkEq(b.getEnPassantSquare(), G3, "en passant square parsed");

    // The halfmove clock used to be ignored entirely, so the fifty-move rule
    // could never trigger.
    b.setupFromFen("8/8/4k3/8/8/4K3/8/6R1 w - - 87 120");
    checkEq(b.getHalfmoveClock(), 87, "halfmove clock parsed");

    // A truncated FEN must not read past the end of the string.
    b.setupFromFen("8/8/4k3/8/8/4K3/8/6R1 w");
    checkEq(b.getHalfmoveClock(), 0, "truncated fen defaults halfmove clock");
    checkEq(b.getEnPassantSquare(), NO_SQUARE, "truncated fen defaults en passant");
}

// ---------------------------------------------------------------------------
// Piece-square table orientation.
// ---------------------------------------------------------------------------

// Exercised through evaluate() rather than by reading the tables directly, so
// the test pins down the behaviour the search actually sees. The tables are
// written a8-first while Square is a1-first; reading them with the raw square
// number turns every table upside-down for both colours.
static void testPstOrientation() {
    std::printf("piece-square table orientation\n");
    Search s;
    Board a, b;

    // A king is safest tucked on its own back rank, not marching up the board.
    a.setupFromFen("k7/8/8/8/8/8/8/6K1 w - - 0 1");   // white king on g1
    b.setupFromFen("k5K1/8/8/8/8/8/8/8 w - - 0 1");   // white king on g8
    check(s.evaluate(a) > s.evaluate(b), "white king prefers g1 to g8");

    a.setupFromFen("k7/8/8/8/8/8/8/6K1 w - - 0 1");   // white king on g1
    b.setupFromFen("k7/8/8/8/4K3/8/8/8 w - - 0 1");   // white king on e4
    check(s.evaluate(a) > s.evaluate(b), "white king prefers g1 to the centre");

    // A pawn must be worth more the closer it gets to promoting.
    int previous = 0;
    for (int rank = 1; rank <= 6; ++rank) {
        char fen[64];
        std::snprintf(fen, sizeof(fen), "4k3/%s/%s/%s/%s/%s/%s/4K3 w - - 0 1",
                      rank == 6 ? "3P4" : "8", rank == 5 ? "3P4" : "8",
                      rank == 4 ? "3P4" : "8", rank == 3 ? "3P4" : "8",
                      rank == 2 ? "3P4" : "8", rank == 1 ? "3P4" : "8");
        a.setupFromFen(fen);
        const int score = s.evaluate(a);
        if (rank > 1)
            check(score > previous,
                  std::string("white pawn gains value advancing to rank ") + (char)('1' + rank));
        previous = score;
    }
}

// ---------------------------------------------------------------------------
// The evaluation must be perfectly symmetric between the colours.
// ---------------------------------------------------------------------------

static std::string mirrorFen(const std::string& fen) {
    std::istringstream ss(fen);
    std::string placement, stm, castling, ep, hm = "0", fm = "1";
    ss >> placement >> stm >> castling >> ep;
    ss >> hm; ss >> fm;

    std::vector<std::string> ranks;
    std::string cur;
    for (char c : placement) {
        if (c == '/') { ranks.push_back(cur); cur.clear(); }
        else cur += c;
    }
    ranks.push_back(cur);
    std::reverse(ranks.begin(), ranks.end());

    std::string out;
    for (size_t i = 0; i < ranks.size(); ++i) {
        if (i) out += '/';
        for (char c : ranks[i]) {
            if (std::islower((unsigned char)c)) out += (char)std::toupper((unsigned char)c);
            else if (std::isupper((unsigned char)c)) out += (char)std::tolower((unsigned char)c);
            else out += c;
        }
    }

    std::string newCastling;
    for (char c : castling) {
        if (std::islower((unsigned char)c)) newCastling += (char)std::toupper((unsigned char)c);
        else if (std::isupper((unsigned char)c)) newCastling += (char)std::tolower((unsigned char)c);
        else newCastling += c;
    }
    if (newCastling.empty()) newCastling = "-";

    std::string newEp = "-";
    if (ep != "-" && ep.size() >= 2)
        newEp = std::string(1, ep[0]) + std::string(1, (char)('1' + (7 - (ep[1] - '1'))));

    return out + " " + (stm == "w" ? "b" : "w") + " " + newCastling + " " + newEp + " " + hm + " " + fm;
}

static void testEvaluationSymmetry() {
    std::printf("evaluation colour symmetry\n");
    Search s;
    const char* fens[] = {
        START_FEN,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "4k3/8/8/8/8/8/8/4R1K1 w - - 0 1",
        "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1",
        "8/3k4/8/2PPP3/8/8/8/4K3 w - - 0 1",
    };
    for (const char* fen : fens) {
        Board a, b;
        a.setupFromFen(fen);
        b.setupFromFen(mirrorFen(fen));
        checkEq(s.evaluate(a), s.evaluate(b), std::string("mirrored evaluation matches: ") + fen);
    }
}

// ---------------------------------------------------------------------------
// Pawn structure terms.
// ---------------------------------------------------------------------------

static void testPawnStructure() {
    std::printf("pawn structure\n");
    Search s;
    Board b;

    // A single pawn cannot be doubled. This used to cost 25 centipawns.
    b.setupFromFen("4k3/8/8/8/3P4/8/8/4K3 w - - 0 1");
    const int lone = s.evaluatePawnStructure(b);

    b.setupFromFen("4k3/8/8/3P4/3P4/8/8/4K3 w - - 0 1");
    const int doubled = s.evaluatePawnStructure(b);
    check(doubled < lone * 2, "a doubled pair scores worse than two independent pawns");

    // Three connected pawns: none doubled, none isolated.
    b.setupFromFen("4k3/8/8/8/2PPP3/8/8/4K3 w - - 0 1");
    const int connected = s.evaluatePawnStructure(b);
    check(connected > 3 * lone, "connected pawns beat three lone pawns");

    // A neighbour anywhere on an adjacent file breaks isolation, not just one
    // sitting directly beside the pawn. Each pair below moves the second pawn
    // from the b-file to the h-file at a fixed rank, so the only thing that
    // changes is whether the two pawns are isolated. Both deltas must therefore
    // be the same. The old mask only looked at the three squares flanking the
    // pawn, so a neighbour two ranks away went unnoticed and the far pair's
    // delta collapsed to zero.
    auto pawnStructureOf = [&](const char* fen) {
        Board tmp;
        tmp.setupFromFen(fen);
        return s.evaluatePawnStructure(tmp);
    };
    const int neighbourFar = pawnStructureOf("4k3/8/8/1P6/8/8/P7/4K3 w - - 0 1")   // a2 + b5
                           - pawnStructureOf("4k3/8/8/7P/8/8/P7/4K3 w - - 0 1");   // a2 + h5
    const int neighbourClose = pawnStructureOf("4k3/8/8/8/8/1P6/P7/4K3 w - - 0 1") // a2 + b3
                             - pawnStructureOf("4k3/8/8/8/8/7P/P7/4K3 w - - 0 1"); // a2 + h3
    check(neighbourFar > 0, "an adjacent-file neighbour removes the isolation penalty");
    checkEq(neighbourFar, neighbourClose,
            "isolation looks at whole files, not just neighbouring squares");
}

// ---------------------------------------------------------------------------
// Draw detection.
// ---------------------------------------------------------------------------

static Move findMove(Board& b, Square from, Square to) {
    std::vector<Move> moves;
    MoveGenerator::generateMoves(b, moves);
    for (Move m : moves)
        if (getFromSquare(m) == from && getToSquare(m) == to) return m;
    return 0;
}

static void testDrawDetection() {
    std::printf("draw detection\n");
    Board b;

    b.setupFromFen("4k3/8/8/8/8/8/8/4KB2 w - - 0 1");
    check(b.isInsufficientMaterial(), "K+B vs K is insufficient material");
    b.setupFromFen("4k3/8/8/8/8/8/8/4KR2 w - - 0 1");
    check(!b.isInsufficientMaterial(), "K+R vs K is sufficient material");
    b.setupFromFen("4k3/7P/8/8/8/8/8/4K3 w - - 0 1");
    check(!b.isInsufficientMaterial(), "a pawn is sufficient material");

    // The halfmove clock must survive make/unmake and count reversible moves.
    b.setupFromFen(START_FEN);
    checkEq(b.getHalfmoveClock(), 0, "clock starts at zero");
    Move nf3 = findMove(b, G1, F3);
    check(nf3 != 0, "Ng1-f3 is generated");
    b.makeMove(nf3);
    checkEq(b.getHalfmoveClock(), 1, "a knight move increments the clock");
    Move e5 = findMove(b, E7, E5);
    b.makeMove(e5);
    checkEq(b.getHalfmoveClock(), 0, "a pawn move resets the clock");
    b.unmakeMove(e5);
    checkEq(b.getHalfmoveClock(), 1, "unmake restores the clock");
    b.unmakeMove(nf3);
    checkEq(b.getHalfmoveClock(), 0, "unmake restores the clock to zero");

    // Shuffling the knights back and forth repeats the starting position.
    b.setupFromFen(START_FEN);
    check(!b.isRepetition(), "the starting position is not a repetition");
    const Square path[4][2] = { {G1, F3}, {G8, F6}, {F3, G1}, {F6, G8} };
    for (int i = 0; i < 4; ++i) {
        Move m = findMove(b, path[i][0], path[i][1]);
        check(m != 0, "knight shuffle move is generated");
        if (!m) return;
        b.makeMove(m);
    }
    check(b.isRepetition(), "a knight shuffle back to the start is a repetition");
}

// ---------------------------------------------------------------------------
// Search sanity: it must find forced mates, for both colours.
// ---------------------------------------------------------------------------

static void testSearchFindsMate() {
    std::printf("search finds forced mate\n");
    Search s;
    Board b;

    // Back-rank mate in one for White: Ra1-a8#.
    b.setupFromFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
    Move m = s.findBestMove(b, 4);
    check(getFromSquare(m) == A1 && getToSquare(m) == A8, "white finds Ra8#");

    // The exact mirror, for Black: Ra8-a1#.
    b.setupFromFen("r5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1");
    m = s.findBestMove(b, 4);
    check(getFromSquare(m) == A8 && getToSquare(m) == A1, "black finds Ra1#");

    // Fool's mate: Qd8-h4#.
    b.setupFromFen("rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq g3 0 2");
    m = s.findBestMove(b, 4);
    check(getFromSquare(m) == D8 && getToSquare(m) == H4, "black finds Qh4#");

    // A mate in two: White plays Qb7-b8+, Black must block with Rd8, Qxd8#.
    b.setupFromFen("6k1/1Q6/8/8/8/8/8/6K1 w - - 0 1");
    m = s.findBestMove(b, 5);
    check(m != 0, "a legal move is returned in a won position");

    // With no legal moves at all, findBestMove must report 0 rather than a
    // garbage move. This is stalemate: Black to move, king on a8, boxed in.
    b.setupFromFen("k7/8/1Q6/8/8/8/8/6K1 b - - 0 1");
    m = s.findBestMove(b, 3);
    checkEq(m, 0, "no move is returned when the side to move is stalemated");
}

// ---------------------------------------------------------------------------
// The search must not blunder material in a simple tactical position.
// ---------------------------------------------------------------------------

static void testSearchTakesFreeMaterial() {
    std::printf("search takes free material\n");
    Search s;
    Board b;

    // An undefended queen must be taken. Either the rook or the king may do it,
    // so only the destination square is asserted.
    b.setupFromFen("4k3/8/8/8/8/8/3q4/3RK3 w - - 0 1");
    for (int depth = 1; depth <= 5; ++depth) {
        Move m = s.findBestMove(b, depth);
        check(getToSquare(m) == D2,
              std::string("white captures the hanging queen at depth ") + (char)('0' + depth));
    }

    // A pawn on the seventh must promote rather than shuffle the king.
    b.setupFromFen("8/1P6/8/8/8/8/8/k1K5 w - - 0 1");
    Move promo = s.findBestMove(b, 5);
    check(getFromSquare(promo) == B7 && getToSquare(promo) == B8 &&
          getPromotionPiece(promo) == whiteQueen, "white promotes to a queen");
}

int main() {
    Zobrist::init();
    MoveGenerator::init();

    testFenParsing();
    testPerft();
    testStateConsistency();
    testPstOrientation();
    testEvaluationSymmetry();
    testPawnStructure();
    testDrawDetection();
    testSearchFindsMate();
    testSearchTakesFreeMaterial();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
