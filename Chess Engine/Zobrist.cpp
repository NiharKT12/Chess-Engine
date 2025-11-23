#include "Zobrist.h"
#include <random>

// Initialize the static member variables
uint64_t Zobrist::pieceKeys[12][64];
uint64_t Zobrist::enPassantKeys[8];
uint64_t Zobrist::castlingKeys[16];
uint64_t Zobrist::blackToMoveKey;

// A simple pseudo-random number generator for reproducibility
uint64_t simple_rand() {
    static uint64_t seed = 0x9876543210abcdefULL;
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}

void Zobrist::init() {
    for (int i = 0; i < 12; ++i) {
        for (int j = 0; j < 64; ++j) {
            pieceKeys[i][j] = simple_rand();
        }
    }
    for (int i = 0; i < 8; ++i) {
        enPassantKeys[i] = simple_rand();
    }
    for (int i = 0; i < 16; ++i) {
        castlingKeys[i] = simple_rand();
    }
    blackToMoveKey = simple_rand();
}

