#include "Renderer.h"
#include <string>
#include <iostream> // For error checking

// --- CONSTRUCTOR & DESTRUCTOR ---

Renderer::Renderer() {
    // The assets live next to the solution, but the working directory depends on
    // how the program was launched (Visual Studio, a double-click, or a shell).
    // Probe the usual spots instead of assuming one.
    const char* candidates[] = { "../Assets/", "Assets/", "../../Assets/", "../../../Assets/" };
    std::string assetPath = candidates[0];
    for (const char* candidate : candidates) {
        if (FileExists((std::string(candidate) + "chess-pawn-white.png").c_str())) {
            assetPath = candidate;
            break;
        }
    }

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

    // Error checking to ensure all textures were loaded successfully. drawPieces
    // falls back to a plain disc for any that are missing, so the game stays
    // playable rather than crashing on a zero-width texture.
    for (const auto& pair : m_pieceTextures) {
        if (pair.second.id <= 0) {
            std::cerr << "ERROR: Failed to load texture for pieceType index " << pair.first
                      << " (looked in " << assetPath << ")" << std::endl;
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

    // Manually draw the checkerboard for perfect alignment.
    // Row 0 is the top of the screen, which is a8 from White's side, and a8 is
    // a light square -- so an even (row + file) must be light. The pattern is
    // unchanged by the 180-degree flip used for Black's perspective.
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            bool isLightSquare = (rank + file) % 2 == 0;
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

        if (pt == noPiece) continue;

        // ...calculate its position on the screen using perspective...
        int displayFile, displayRank;
        getDisplayCoordinates(sq, displayFile, displayRank);
        const Vector2 position = { offsetX + displayFile * squareSize, offsetY + displayRank * squareSize };

        // A texture that failed to load has id 0 and width 0, so scaling by
        // squareSize / width would divide by zero. Fall back to a plain
        // coloured disc so the piece is at least visible and playable.
        auto it = m_pieceTextures.find(pt);
        if (it == m_pieceTextures.end() || it->second.id <= 0 || it->second.width <= 0) {
            const Color fallback = (pt < blackPawn) ? Color{ 250, 250, 250, 255 } : Color{ 30, 30, 30, 255 };
            DrawCircleV({ position.x + squareSize / 2.0f, position.y + squareSize / 2.0f },
                        squareSize * 0.35f, fallback);
            DrawCircleLines((int)(position.x + squareSize / 2.0f), (int)(position.y + squareSize / 2.0f),
                            squareSize * 0.35f, GRAY);
            continue;
        }

        // ...and draw it, scaled to fit the square.
        DrawTextureEx(it->second, position, 0.0f, squareSize / it->second.width, WHITE);
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

        // Draw a red glow around the king, thickening towards the square edge.
        for (int i = 3; i > 0; --i) {
            const Color glowColor = { 255, 0, 0, (unsigned char)(180 - i * 40) };
            const float inset = (float)(3 - i);
            DrawRectangleLines(kingX + inset, kingY + inset,
                               squareSize - 2 * inset, squareSize - 2 * inset, glowColor);
        }
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

void Renderer::drawGameOverScreen(GameResult result, const char* reason) const {
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    // Draw semi-transparent dark overlay
    DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.7f));

    const char* headline = (result == RESULT_WHITE_WINS) ? "White Wins!"
                         : (result == RESULT_BLACK_WINS) ? "Black Wins!"
                         : "Draw";
    Color headlineColor = (result == RESULT_WHITE_WINS) ? Color{ 238, 238, 210, 255 }
                        : (result == RESULT_BLACK_WINS) ? Color{ 118, 150, 86, 255 }
                        : Color{ 200, 200, 200, 255 };

    int fontSize1 = 80;
    int textWidth1 = MeasureText(headline, fontSize1);
    DrawText(headline, (screenWidth - textWidth1) / 2, (screenHeight / 2) - 80, fontSize1, headlineColor);

    int fontSize2 = 40;
    int textWidth2 = MeasureText(reason, fontSize2);
    DrawText(reason, (screenWidth - textWidth2) / 2, (screenHeight / 2) + 40, fontSize2, WHITE);

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
