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
}

Renderer::~Renderer() {
    // Loop through the map and unload every texture to prevent memory leaks.
    for (const auto& pair : m_pieceTextures) {
        UnloadTexture(pair.second);
    }
}


// --- DRAWING FUNCTIONS ---

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

                // ...calculate its position on the screen...
                int file = sq % 8;
                int rank = 7 - (sq / 8); // Invert rank for drawing (0,0 is top-left)
                Vector2 position = { offsetX + file * squareSize, offsetY + rank * squareSize };

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
        int file = toSquare % 8;
        int rank = 7 - (toSquare / 8);

        // Calculate the center of the destination square
        float circleX = offsetX + file * squareSize + squareSize / 2.0f;
        float circleY = offsetY + rank * squareSize + squareSize / 2.0f;

        DrawCircleV({ circleX, circleY }, squareSize / 4.0f, moveIndicatorColor);
    }
}


// --- HELPER FUNCTION ---

void Renderer::getBoardDimensions(float& size, float& offsetX, float& offsetY) const {
    size = std::min((float)GetScreenWidth(), (float)GetScreenHeight());
    offsetX = (GetScreenWidth() - size) / 2.0f;
    offsetY = (GetScreenHeight() - size) / 2.0f;
}
