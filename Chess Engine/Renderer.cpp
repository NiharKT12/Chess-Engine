#include "Renderer.h"
#include <string>
#include <iostream> // For error checking

// --- CONSTRUCTOR & DESTRUCTOR ---

Renderer::Renderer() {
    // Relative path to the asset folder. Adjust if your executable is in a different location.
    const std::string assetPath = "../Assets/";

    // Load textures for all 12 piece types and store them in the map.
    // The map key is the pieceType enum value.
    m_pieceTextures[whitePawn] = LoadTexture((assetPath + "chess-pawn-white.png").c_str());
    m_pieceTextures[whiteKnight] = LoadTexture((assetPath + "chess-knight-white.png").c_str());
    m_pieceTextures[whiteBishop] = LoadTexture((assetPath + "chess-bishop-white.png").c_str());
    m_pieceTextures[whiteRook] = LoadTexture((assetPath + "chess-rook-white.png").c_str());
    m_pieceTextures[whiteQueen] = LoadTexture((assetPath + "chess-queen-white.png").c_str());
    m_pieceTextures[whiteKing] = LoadTexture((assetPath + "chess-king-white.png").c_str());

    m_pieceTextures[blackPawn] = LoadTexture((assetPath + "chess-pawn-black.png").c_str());
    m_pieceTextures[blackKnight] = LoadTexture((assetPath + "chess-knight-black.png").c_str());
    m_pieceTextures[blackBishop] = LoadTexture((assetPath + "chess-bishop-black.png").c_str());
    m_pieceTextures[blackRook] = LoadTexture((assetPath + "chess-rook-black.png").c_str());
    m_pieceTextures[blackQueen] = LoadTexture((assetPath + "chess-queen-black.png").c_str());
    m_pieceTextures[blackKing] = LoadTexture((assetPath + "chess-king-black.png").c_str());

    // Error checking to ensure all textures were loaded successfully.
    for (const auto& pair : m_pieceTextures) {
        if (pair.second.id <= 0) {
            std::cerr << "ERROR: Failed to load texture for pieceType index " << pair.first << std::endl;
        }
    }

    // --- NEW: Initialize perspective to white ---
    m_perspective = W;
}

Renderer::~Renderer() {
    // Loop through the map and unload every texture to prevent memory leaks.
    for (const auto& pair : m_pieceTextures) {
        UnloadTexture(pair.second);
    }
}


// --- DRAWING FUNCTIONS ---

void Renderer::setPerspective(Side playerSide) {
    m_perspective = playerSide;
}

// --- NEW: Helper function to get display coordinates based on perspective ---
void Renderer::getDisplayCoordinates(Square sq, int& displayFile, int& displayRank) const {
    int file = sq % 8;
    int rank = sq / 8;

    if (m_perspective == W) {
        // White perspective: rank 0 at bottom (display as rank 7)
        displayFile = file;
        displayRank = 7 - rank;
    } else {
        // Black perspective: rotate 180 degrees
        displayFile = 7 - file;
        displayRank = rank;
    }
}

void Renderer::drawBoard() const {
    float size, offsetX, offsetY;
    getBoardDimensions(size, offsetX, offsetY);
    float squareSize = size / 8.0f;

    // Manually draw the checkerboard for perfect alignment
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            bool isLightSquare = (rank + file) % 2 != 0;
            Color squareColor = isLightSquare ? Color{ 238, 238, 210, 255 } : Color{ 118, 150, 86, 255 };
            DrawRectangle(offsetX + file * squareSize, offsetY + rank * squareSize, squareSize, squareSize, squareColor);
        }
    }
}

void Renderer::drawPieces(const Board& board) const {
    float size, offsetX, offsetY;
    getBoardDimensions(size, offsetX, offsetY);
    float squareSize = size / 8.0f;

    // Loop through all 64 squares of the board
    for (int i = 0; i < 64; ++i) {
        Square sq = (Square)i;
        pieceType pt = board.getPieceOnSquare(sq);

        // If a piece exists on the square...
        if (pt != noPiece) {
            try {
                // ...find its texture in our map...
                Texture2D texture = m_pieceTextures.at(pt);

                // ...calculate its position on the screen using perspective...
                int displayFile, displayRank;
                getDisplayCoordinates(sq, displayFile, displayRank);
                Vector2 position = { offsetX + displayFile * squareSize, offsetY + displayRank * squareSize };

                // ...and draw it, scaled to fit the square.
                DrawTextureEx(texture, position, 0.0f, squareSize / texture.width, WHITE);

            }
            catch (const std::out_of_range& e) {
                // This catch block will prevent a crash if a texture is missing from the map.
                std::cerr << "ERROR: Texture not found for pieceType " << pt << std::endl;
            }
        }
    }
}

void Renderer::drawValidMoves(const std::vector<Move>& moves) const {
    float size, offsetX, offsetY;
    getBoardDimensions(size, offsetX, offsetY);
    float squareSize = size / 8.0f;

    // A semi-transparent dark circle for move indicators
    Color moveIndicatorColor = { 40, 40, 40, 128 };

    for (const auto& move : moves) {
        Square toSquare = getToSquare(move);
        
        // --- NEW: Use perspective-aware coordinates ---
        int displayFile, displayRank;
        getDisplayCoordinates(toSquare, displayFile, displayRank);

        // Calculate the center of the destination square
        float circleX = offsetX + displayFile * squareSize + squareSize / 2.0f;
        float circleY = offsetY + displayRank * squareSize + squareSize / 2.0f;

        DrawCircleV({ circleX, circleY }, squareSize / 4.0f, moveIndicatorColor);
    }
}

void Renderer::drawCheckIndicator(const Board& board) const {
    float size, offsetX, offsetY;
    getBoardDimensions(size, offsetX, offsetY);
    float squareSize = size / 8.0f;

    Side currentSide = board.getSideToMove();
    Side opponentSide = (Side)(1 - currentSide);
    Square kingSquare = board.getKingSquare(currentSide);

    // Check if the king is under attack
    if (board.isSquareAttacked(kingSquare, opponentSide)) {
        // --- NEW: Use perspective-aware coordinates ---
        int displayFile, displayRank;
        getDisplayCoordinates(kingSquare, displayFile, displayRank);
        float kingX = offsetX + displayFile * squareSize;
        float kingY = offsetY + displayRank * squareSize;

        // Draw red glow effect around the king
        for (int i = 3; i > 0; --i) {
            float radius = (squareSize / 2.0f) * (i / 3.0f);
            Color glowColor = { 255, 0, 0, (unsigned char)(180 - i * 50) };
            DrawRectangleLines(kingX, kingY, squareSize, squareSize, glowColor);
        }

        // Draw a pulsing red border
        DrawRectangleLines(kingX, kingY, squareSize, squareSize, { 255, 0, 0, 255 });
    }
}

void Renderer::drawLastMove(Move lastMove) const {
    if (lastMove == 0) return;

    float size, offsetX, offsetY;
    getBoardDimensions(size, offsetX, offsetY);
    float squareSize = size / 8.0f;

    Square fromSquare = getFromSquare(lastMove);
    Square toSquare = getToSquare(lastMove);

    // Draw glow on the from square
    {
        // --- NEW: Use perspective-aware coordinates ---
        int displayFile, displayRank;
        getDisplayCoordinates(fromSquare, displayFile, displayRank);
        float squareX = offsetX + displayFile * squareSize;
        float squareY = offsetY + displayRank * squareSize;

        // Yellow glow for from square
        Color glowColor = { 255, 255, 100, 100 };
        DrawRectangle(squareX, squareY, squareSize, squareSize, glowColor);
        DrawRectangleLines(squareX, squareY, squareSize, squareSize, { 200, 200, 0, 200 });
    }

    // Draw glow on the to square
    {
        // --- NEW: Use perspective-aware coordinates ---
        int displayFile, displayRank;
        getDisplayCoordinates(toSquare, displayFile, displayRank);
        float squareX = offsetX + displayFile * squareSize;
        float squareY = offsetY + displayRank * squareSize;

        // Green glow for to square
        Color glowColor = { 100, 255, 100, 100 };
        DrawRectangle(squareX, squareY, squareSize, squareSize, glowColor);
        DrawRectangleLines(squareX, squareY, squareSize, squareSize, { 0, 200, 0, 200 });
    }
}

void Renderer::drawGameOverScreen(Side winner, bool isCheckmate) const {
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    // Draw semi-transparent dark overlay
    DrawRectangle(0, 0, screenWidth, screenHeight, Fade({ 0, 0, 0 }, 0.7f));

    // Determine the winner text
    const char* winnerText = (winner == W) ? "White Wins!" : "Black Wins!";
    const char* conditionText = isCheckmate ? "Checkmate!" : "Stalemate!";
    
    // Draw winner announcement
    int fontSize1 = 80;
    int textWidth1 = MeasureText(winnerText, fontSize1);
    Color winnerColor = (winner == W) ? Color{ 238, 238, 210, 255 } : Color{ 118, 150, 86, 255 };
    DrawText(winnerText, (screenWidth - textWidth1) / 2, (screenHeight / 2) - 80, fontSize1, winnerColor);

    // Draw condition text (Checkmate or Stalemate)
    int fontSize2 = 40;
    int textWidth2 = MeasureText(conditionText, fontSize2);
    DrawText(conditionText, (screenWidth - textWidth2) / 2, (screenHeight / 2) + 40, fontSize2, WHITE);

    // Draw instructions to restart
    const char* restartText = "Press R to Restart or Q to Quit";
    int fontSize3 = 20;
    int textWidth3 = MeasureText(restartText, fontSize3);
    DrawText(restartText, (screenWidth - textWidth3) / 2, (screenHeight / 2) + 120, fontSize3, { 200, 200, 200, 255 });
}


// --- HELPER FUNCTION ---

void Renderer::getBoardDimensions(float& size, float& offsetX, float& offsetY) const {
    size = std::min((float)GetScreenWidth(), (float)GetScreenHeight());
    offsetX = (GetScreenWidth() - size) / 2.0f;
    offsetY = (GetScreenHeight() - size) / 2.0f;
}
