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

// Enum to represent the state of the game
enum GameState {
    SIDE_SELECTION,
    IN_GAME,
    GAME_OVER
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
    
    // --- NEW: Convert screen coordinates to board coordinates based on perspective ---
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

    board.setupFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    // --- NEW: Side Selection ---
    GameState gameState = SIDE_SELECTION;
    Side playerSide = W;  // Default to white
    
    Square selectedSquare = NO_SQUARE;
    std::vector<Move> legalMoves;
    bool isGameOver = false;
    Move lastMove = 0;
    Side gameWinner = W;
    
    // --- NEW: Track position history for repetition detection ---
    std::vector<uint64_t> positionHistory;
    positionHistory.push_back(board.getHashKey());  // Add starting position

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
                    renderer.setPerspective(W);  // --- NEW: Set board perspective ---
                }
                else if (blackButtonHovered) {
                    playerSide = B;
                    gameState = IN_GAME;
                    renderer.setPerspective(B);  // --- NEW: Set board perspective (rotated) ---
                }
            }

            // --- DRAWING MENU ---
            BeginDrawing();
            ClearBackground(RAYWHITE);
            drawSideSelectionMenu(whiteButtonHovered, blackButtonHovered);
            EndDrawing();
            continue;
        }

        // --- GAME OVER SCREEN ---
        if (gameState == GAME_OVER) {
            if (IsKeyPressed(KEY_R)) {
                // Restart the game
                board.setupFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
                gameState = SIDE_SELECTION;
                selectedSquare = NO_SQUARE;
                legalMoves.clear();
                isGameOver = false;
                lastMove = 0;
                positionHistory.clear();  // --- NEW: Reset position history ---
                positionHistory.push_back(board.getHashKey());  // --- NEW: Add starting position ---
                renderer.setPerspective(W);  // --- NEW: Reset perspective to white ---
            }
            else if (IsKeyPressed(KEY_Q)) {
                break;  // Exit the game
            }

            BeginDrawing();
            ClearBackground(RAYWHITE);
            renderer.drawBoard();
            renderer.drawPieces(board);
            
            // Determine if the game ended by checkmate or stalemate
            Side currentSide = board.getSideToMove();
            Side opponentSide = (Side)(1 - currentSide);
            bool isKingInCheck = board.isSquareAttacked(board.getKingSquare(currentSide), opponentSide);
            bool isCheckmate = isKingInCheck;
            
            renderer.drawGameOverScreen(gameWinner, isCheckmate);
            EndDrawing();
            continue;
        }

        Side currentTurn = board.getSideToMove();
        Side opponentSide = (Side)(1 - currentTurn);

        // --- Human Player Input ---
        if (currentTurn == playerSide && !isGameOver) {
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Square clickedSquare = getSquareFromMouse(renderer);
                if (clickedSquare != NO_SQUARE) {
                    bool isMoveMade = false;
                    for (const auto& move : legalMoves) {
                        if (getToSquare(move) == clickedSquare) {
                            board.makeMove(move);
                            positionHistory.push_back(board.getHashKey());  // --- NEW: Track position ---
                            lastMove = move;
                            selectedSquare = NO_SQUARE; legalMoves.clear(); isMoveMade = true;
                            break;
                        }
                    }
                    if (!isMoveMade) {
                        pieceType pt = board.getPieceOnSquare(clickedSquare);
                        if (pt != noPiece && ((playerSide == W && pt < blackPawn) || (playerSide == B && pt >= blackPawn))) {
                            selectedSquare = clickedSquare;
                            std::vector<Move> pseudoLegalMoves;
                            MoveGenerator::generateMoves(board, pseudoLegalMoves);
                            legalMoves.clear();
                            for (const auto& move : pseudoLegalMoves) {
                                if (getFromSquare(move) == selectedSquare) {
                                    board.makeMove(move);
                                    if (!board.isSquareAttacked(board.getKingSquare(playerSide), opponentSide)) {
                                        legalMoves.push_back(move);
                                    }
                                    board.unmakeMove(move);
                                }
                            }
                        }
                        else { selectedSquare = NO_SQUARE; legalMoves.clear(); }
                    }
                }
            }
        }

        // --- AI Player Turn ---
        if (currentTurn != playerSide && !isGameOver) {
            Move bestMove = search.findBestMove(board, 6);
            if (bestMove != 0) { 
                board.makeMove(bestMove);
                positionHistory.push_back(board.getHashKey());
                lastMove = bestMove;
            }
        }

        // --- Game Over Check ---
        if (!isGameOver) {
            std::vector<Move> allPseudoLegalMoves;
            MoveGenerator::generateMoves(board, allPseudoLegalMoves);
            bool hasAtLeastOneLegalMove = false;
            for (const auto& move : allPseudoLegalMoves) {
                board.makeMove(move);
                if (!board.isSquareAttacked(board.getKingSquare(currentTurn), opponentSide)) {
                    hasAtLeastOneLegalMove = true;
                }
                board.unmakeMove(move);
                if (hasAtLeastOneLegalMove) break;
            }
            if (!hasAtLeastOneLegalMove) {
                isGameOver = true;
                gameState = GAME_OVER;
                gameWinner = opponentSide;  // The opponent wins
            }
        }

        // --- DRAWING ---
        BeginDrawing();
        ClearBackground(RAYWHITE);
        renderer.drawBoard();
        renderer.drawLastMove(lastMove);  // Draw the last move highlight
        renderer.drawPieces(board);
        renderer.drawCheckIndicator(board);  // Draw check indicator if applicable
        if (selectedSquare != NO_SQUARE) renderer.drawValidMoves(legalMoves);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}

