#pragma once
#pragma once
#include <raylib.h>
#include <vector>
#include <map>
#include <utility> // For std::pair

#include "Type.h"
#include "Board.h"

// The Renderer class is responsible for all drawing.
// It is kept separate from the core chess engine logic.
class Renderer {
public:
    // Constructor: Loads all graphical assets (piece textures).
    Renderer();
    // Destructor: Unloads all graphical assets to free memory.
    ~Renderer();

    // Draws the checkerboard pattern on the screen.
    void drawBoard() const;

    // Draws all the pieces on the board based on the current board state.
    void drawPieces(const Board& board) const;

    // Draws visual indicators (e.g., green squares) for all legal moves.
    void drawValidMoves(const std::vector<Move>& moves) const;

    // Helper function to provide the board's screen layout information.
    // This is used by main.cpp to calculate mouse-to-square conversions.
    void getBoardDimensions(float& size, float& offsetX, float& offsetY) const;

private:
    // A map to store the texture for each piece type and color.
    // Using a map makes looking up the correct texture easy and safe.
    std::map<pieceType, Texture2D> m_pieceTextures;
};
