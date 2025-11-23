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
    int file = (int)((mousePos.x - offsetX) / squareSize);
    int rank = 7 - (int)((mousePos.y - offsetY) / squareSize);
    return (Square)(rank * 8 + file);
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

    Square selectedSquare = NO_SQUARE;
    std::vector<Move> legalMoves;
    bool isGameOver = false;

    // --- MAIN GAME LOOP ---
    while (!WindowShouldClose()) {
        Side currentTurn = board.getSideToMove();
        Side opponentSide = (Side)(1 - currentTurn);

        // --- Human Player Input (White) ---
        if (currentTurn == W && !isGameOver) {
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Square clickedSquare = getSquareFromMouse(renderer);
                if (clickedSquare != NO_SQUARE) {
                    bool isMoveMade = false;
                    for (const auto& move : legalMoves) {
                        if (getToSquare(move) == clickedSquare) {
                            board.makeMove(move);
                            selectedSquare = NO_SQUARE; legalMoves.clear(); isMoveMade = true;
                            break;
                        }
                    }
                    if (!isMoveMade) {
                        pieceType pt = board.getPieceOnSquare(clickedSquare);
                        if (pt != noPiece && (pt < blackPawn)) {
                            selectedSquare = clickedSquare;
                            std::vector<Move> pseudoLegalMoves;
                            MoveGenerator::generateMoves(board, pseudoLegalMoves);
                            legalMoves.clear();
                            for (const auto& move : pseudoLegalMoves) {
                                if (getFromSquare(move) == selectedSquare) {
                                    board.makeMove(move);
                                    if (!board.isSquareAttacked(board.getKingSquare(W), B)) {
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

        // --- AI Player Turn (Black) ---
        if (currentTurn == B && !isGameOver) {
            Move bestMove = search.findBestMove(board, 5);
            if (bestMove != 0) { board.makeMove(bestMove); }
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
            if (!hasAtLeastOneLegalMove) isGameOver = true;
        }

        // --- DRAWING ---
        BeginDrawing();
        ClearBackground(RAYWHITE);
        renderer.drawBoard();
        renderer.drawPieces(board);
        if (selectedSquare != NO_SQUARE) renderer.drawValidMoves(legalMoves);
        if (isGameOver) {
            const char* text = "GAME OVER";
            int fontSize = 100;
            int textWidth = MeasureText(text, fontSize);
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade({ 0,0,0,180 }, 0.5f));
            DrawText(text, (GetScreenWidth() - textWidth) / 2, (GetScreenHeight() - fontSize) / 2, fontSize, { 255, 203, 0, 255 });
        }
        EndDrawing();
    }
    CloseWindow();
    return 0;
}

