#pragma once
#include <cstdint>

// The Zobrist class is responsible for generating and storing the random numbers
// used to create unique hash keys for each board position.
class Zobrist {
public:
    // This function must be called once at the start of the program
    // to initialize the random number tables.
    static void init();

    // Arrays of random 64-bit numbers for each game state component
    static uint64_t pieceKeys[12][64];
    static uint64_t enPassantKeys[8];
    static uint64_t castlingKeys[16];
    static uint64_t blackToMoveKey;
};

