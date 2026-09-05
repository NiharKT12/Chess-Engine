#include <raylib.h>
#include <algorithm>
#include <iostream>
#include <vector>

#include "Type.h"
#include "Board.h"
#include "MoveGenerator.h"
#include "Renderer.h"
#include "Search.h"
#include "Zobrist.h"

// How deep the engine searches for each of its moves.
static constexpr int AI_SEARCH_DEPTH = 6;

static const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Enum to represent the state of the game
enum GameState {
    SIDE_SELECTION,
    IN_GAME,
    GAME_OVER
};

// How the current game finished.
struct Outcome {
    bool over = false;
    GameResult result = RESULT_DRAW;
    const char* reason = "";
};

// Helper function to convert screen coordinates (mouse position) to board squares (0-63)
Square getSquareFromMouse(const Renderer& renderer) {
    Vector2 mousePos = GetMousePosition();
    float size, offsetX, offsetY;
    renderer.getBoardDimensions(size, offsetX, offsetY);
    float squareSize = size / 8.0f;
    if (mousePos.x < offsetX || mousePos.x >(offsetX + size) ||
        mousePos.y < offsetY || mousePos.y >(offsetY + size)) {
        return NO_SQUARE;
    }

    // Get clicked file and rank in screen space
    int screenFile = (int)((mousePos.x - offsetX) / squareSize);
    int screenRank = (int)((mousePos.y - offsetY) / squareSize);

    // Convert screen coordinates to board coordinates based on perspective
    Side perspective = renderer.getPerspective();
    int boardFile, boardRank;

    if (perspective == W) {
        // White perspective: standard mapping
        boardFile = screenFile;
        boardRank = 7 - screenRank;
    } else {
        // Black perspective: rotate 180 degrees
        boardFile = 7 - screenFile;
        boardRank = screenRank;
    }

    return (Square)(boardRank * 8 + boardFile);
}

// True if the side to move has at least one legal reply.
static bool hasLegalMove(Board& board) {
    std::vector<Move> moves;
    MoveGenerator::generateMoves(board, moves);
    const Side us = board.getSideToMove();
    const Side them = (Side)(1 - us);
    for (Move m : moves) {
        board.makeMove(m);
        const bool legal = !board.isSquareAttacked(board.getKingSquare(us), them);
        board.unmakeMove(m);
        if (legal) return true;
    }
    return false;
}

// Collects the legal moves that start on `from` for the side to move.
static void legalMovesFrom(Board& board, Square from, std::vector<Move>& out) {
    out.clear();
    std::vector<Move> pseudoLegal;
    MoveGenerator::generateMoves(board, pseudoLegal);
    const Side us = board.getSideToMove();
    const Side them = (Side)(1 - us);
    for (Move m : pseudoLegal) {
        if (getFromSquare(m) != from) continue;
        board.makeMove(m);
        if (!board.isSquareAttacked(board.getKingSquare(us), them)) out.push_back(m);
        board.unmakeMove(m);
    }
}

static int repetitionCount(const std::vector<uint64_t>& history, uint64_t key) {
    int count = 0;
    for (uint64_t h : history) if (h == key) count++;
    return count;
}

// Decides whether the game is over in the CURRENT position. Every query reads
// the live side to move rather than a value captured earlier in the frame -- the
// previous version tested the mover's king against the mover's own opponent
// after the board had already flipped, so it was asking whether each reply gave
// check instead of whether it was legal.
static Outcome evaluateOutcome(Board& board, const std::vector<uint64_t>& positionHistory) {
    const Side us = board.getSideToMove();
    const Side them = (Side)(1 - us);

    if (!hasLegalMove(board)) {
        if (board.isSquareAttacked(board.getKingSquare(us), them))
            return { true, (us == W) ? RESULT_BLACK_WINS : RESULT_WHITE_WINS, "Checkmate!" };
        return { true, RESULT_DRAW, "Stalemate!" };
    }
    if (board.getHalfmoveClock() >= 100)
        return { true, RESULT_DRAW, "Draw by the fifty-move rule" };
    if (repetitionCount(positionHistory, board.getHashKey()) >= 3)
        return { true, RESULT_DRAW, "Draw by threefold repetition" };
    if (board.isInsufficientMaterial())
        return { true, RESULT_DRAW, "Draw by insufficient material" };

    return {};
}

// Function to draw the side selection menu
void drawSideSelectionMenu(bool whiteButtonHovered, bool blackButtonHovered) {
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    // Draw semi-transparent background
    DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.7f));

    // Draw title
    const char* titleText = "Choose Your Side";
    int titleFontSize = 60;
    int titleWidth = MeasureText(titleText, titleFontSize);
    DrawText(titleText, (screenWidth - titleWidth) / 2, screenHeight / 4, titleFontSize, WHITE);

    // White button
    int buttonWidth = 200;
    int buttonHeight = 80;
    int whiteButtonX = (screenWidth / 2) - buttonWidth - 30;
    int whiteButtonY = (screenHeight / 2) - (buttonHeight / 2);
    Color whiteButtonColor = whiteButtonHovered ? Color{ 200, 200, 200, 255 } : Color{ 238, 238, 210, 255 };
    DrawRectangle(whiteButtonX, whiteButtonY, buttonWidth, buttonHeight, whiteButtonColor);
    DrawRectangleLines(whiteButtonX, whiteButtonY, buttonWidth, buttonHeight, BLACK);
    const char* whiteText = "White";
    int whiteTextWidth = MeasureText(whiteText, 40);
    DrawText(whiteText, whiteButtonX + (buttonWidth - whiteTextWidth) / 2, whiteButtonY + 20, 40, BLACK);

    // Black button
    int blackButtonX = (screenWidth / 2) + 30;
    int blackButtonY = (screenHeight / 2) - (buttonHeight / 2);
    Color blackButtonColor = blackButtonHovered ? Color{ 100, 100, 100, 255 } : Color{ 118, 150, 86, 255 };
    DrawRectangle(blackButtonX, blackButtonY, buttonWidth, buttonHeight, blackButtonColor);
    DrawRectangleLines(blackButtonX, blackButtonY, buttonWidth, buttonHeight, WHITE);
    const char* blackText = "Black";
    int blackTextWidth = MeasureText(blackText, 40);
    DrawText(blackText, blackButtonX + (buttonWidth - blackTextWidth) / 2, blackButtonY + 20, 40, WHITE);
}

// Helper function to check if a point is inside a rectangle
bool isPointInRect(Vector2 point, Rectangle rect) {
    return point.x >= rect.x && point.x <= rect.x + rect.width &&
           point.y >= rect.y && point.y <= rect.y + rect.height;
}

int main() {
    // --- INITIALIZATION ---
    const int screenWidth = 800, screenHeight = 800;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Bitboard Chess AI");
    SetTargetFPS(60);

    Zobrist::init();
    MoveGenerator::init();
    Board board;
    Renderer renderer;
    Search search;

    board.setupFromFen(START_FEN);

    GameState gameState = SIDE_SELECTION;
    Side playerSide = W;  // Default to white

    Square selectedSquare = NO_SQUARE;
    std::vector<Move> legalMoves;
    Move lastMove = 0;
    Outcome outcome;
    // The position the game-over check last ran on, so it runs once per move.
    uint64_t lastCheckedHash = 0;

    // Every position the game has passed through, for threefold repetition.
    std::vector<uint64_t> positionHistory;
    positionHistory.push_back(board.getHashKey());

    // --- MAIN GAME LOOP ---
    while (!WindowShouldClose()) {
        // --- SIDE SELECTION MENU ---
        if (gameState == SIDE_SELECTION) {
            Vector2 mousePos = GetMousePosition();
            Rectangle whiteButtonRect = { (GetScreenWidth() / 2.0f) - 200 - 30, (GetScreenHeight() / 2.0f) - 40, 200, 80 };
            Rectangle blackButtonRect = { (GetScreenWidth() / 2.0f) + 30, (GetScreenHeight() / 2.0f) - 40, 200, 80 };

            bool whiteButtonHovered = isPointInRect(mousePos, whiteButtonRect);
            bool blackButtonHovered = isPointInRect(mousePos, blackButtonRect);

            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                if (whiteButtonHovered) {
                    playerSide = W;
                    gameState = IN_GAME;
                    renderer.setPerspective(W);
                }
                else if (blackButtonHovered) {
                    playerSide = B;
                    gameState = IN_GAME;
                    renderer.setPerspective(B);
                }
            }

            BeginDrawing();
            ClearBackground(RAYWHITE);
            drawSideSelectionMenu(whiteButtonHovered, blackButtonHovered);
            EndDrawing();
            continue;
        }

        // --- GAME OVER SCREEN ---
        if (gameState == GAME_OVER) {
            if (IsKeyPressed(KEY_R)) {
                board.setupFromFen(START_FEN);
                gameState = SIDE_SELECTION;
                selectedSquare = NO_SQUARE;
                legalMoves.clear();
                lastMove = 0;
                outcome = Outcome{};
                lastCheckedHash = 0;
                positionHistory.clear();
                positionHistory.push_back(board.getHashKey());
                renderer.setPerspective(W);
            }
            else if (IsKeyPressed(KEY_Q)) {
                break;  // Exit the game
            }

            BeginDrawing();
            ClearBackground(RAYWHITE);
            renderer.drawBoard();
            renderer.drawLastMove(lastMove);
            renderer.drawPieces(board);
            renderer.drawGameOverScreen(outcome.result, outcome.reason);
            EndDrawing();
            continue;
        }

        // --- Game Over Check ---
        // Runs once for each distinct position: on entering the game, after the
        // player moves, and after the engine moves. Doing it here also means the
        // engine is never asked to search a position that is already finished.
        if (board.getHashKey() != lastCheckedHash) {
            lastCheckedHash = board.getHashKey();
            outcome = evaluateOutcome(board, positionHistory);
            if (outcome.over) {
                gameState = GAME_OVER;
                continue;
            }
        }

        // --- Human Player Input ---
        if (board.getSideToMove() == playerSide) {
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Square clickedSquare = getSquareFromMouse(renderer);
                if (clickedSquare != NO_SQUARE) {
                    // Among the legal moves landing on the clicked square there
                    // may be four promotions. Always take the queen.
                    Move chosen = 0;
                    for (Move m : legalMoves) {
                        if (getToSquare(m) != clickedSquare) continue;
                        if (chosen == 0) chosen = m;
                        if (isPromotion(m) && getPromotionPiece(m) % 6 == whiteQueen) { chosen = m; break; }
                    }

                    if (chosen != 0) {
                        board.makeMove(chosen);
                        positionHistory.push_back(board.getHashKey());
                        lastMove = chosen;
                        selectedSquare = NO_SQUARE;
                        legalMoves.clear();
                    }
                    else {
                        pieceType pt = board.getPieceOnSquare(clickedSquare);
                        const bool ownPiece = pt != noPiece &&
                            ((playerSide == W && pt < blackPawn) || (playerSide == B && pt >= blackPawn));
                        if (ownPiece) {
                            selectedSquare = clickedSquare;
                            legalMovesFrom(board, selectedSquare, legalMoves);
                        }
                        else {
                            selectedSquare = NO_SQUARE;
                            legalMoves.clear();
                        }
                    }
                }
            }
        }
        // --- AI Player Turn ---
        else {
            // The search blocks the main thread, so present a frame first --
            // otherwise the window simply freezes with no explanation.
            BeginDrawing();
            ClearBackground(RAYWHITE);
            renderer.drawBoard();
            renderer.drawLastMove(lastMove);
            renderer.drawPieces(board);
            renderer.drawCheckIndicator(board);
            DrawText("Thinking...", 12, 10, 24, Color{ 60, 60, 60, 255 });
            EndDrawing();

            Move bestMove = search.findBestMove(board, AI_SEARCH_DEPTH);
            if (bestMove != 0) {
                board.makeMove(bestMove);
                positionHistory.push_back(board.getHashKey());
                lastMove = bestMove;
            }
        }

        // --- DRAWING ---
        BeginDrawing();
        ClearBackground(RAYWHITE);
        renderer.drawBoard();
        renderer.drawLastMove(lastMove);
        renderer.drawPieces(board);
        renderer.drawCheckIndicator(board);
        if (selectedSquare != NO_SQUARE) renderer.drawValidMoves(legalMoves);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
