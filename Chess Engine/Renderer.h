#pragma once
#include <raylib.h>
#include <algorithm> // For std::min
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

    // The renderer owns raw GPU texture handles and frees them in its
    // destructor, so copying one would unload the same textures twice.
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // --- NEW: Set the perspective (player side) ---
    // Call this to rotate the board when player is black
    void setPerspective(Side playerSide);

    // Draws the checkerboard pattern on the screen.
    void drawBoard() const;

    // Draws all the pieces on the board based on the current board state.
    void drawPieces(const Board& board) const;

    // Draws visual indicators (e.g., green squares) for all legal moves.
    void drawValidMoves(const std::vector<Move>& moves) const;

    // Draws a check indicator (red glow) around the king's square
    void drawCheckIndicator(const Board& board) const;

    // Draws the last move indicator (glow on from and to squares)
    void drawLastMove(Move lastMove) const;

    // Draws the game over screen. `reason` describes how the game ended
    // ("Checkmate!", "Stalemate!", "Draw by repetition", ...).
    void drawGameOverScreen(GameResult result, const char* reason) const;

    // Helper function to provide the board's screen layout information.
    // This is used by main.cpp to calculate mouse-to-square conversions.
    void getBoardDimensions(float& size, float& offsetX, float& offsetY) const;

    // --- NEW: Get the perspective (white or black) ---
    Side getPerspective() const { return m_perspective; }

private:
    // --- NEW: Track the player's perspective ---
    Side m_perspective = W;

    // --- NEW: Helper function to convert square based on perspective ---
    void getDisplayCoordinates(Square sq, int& displayFile, int& displayRank) const;

    // A map to store the texture for each piece type and color.
    // Using a map makes looking up the correct texture easy and safe.
    std::map<pieceType, Texture2D> m_pieceTextures;
};
