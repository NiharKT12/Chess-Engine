# Chess Engine

A high-performance C++ chess engine built with bitboards, complete move generation, advanced search algorithms, and a graphical interface using Raylib.

## Overview

This chess engine implements a fully functional chess game with:
- **Bitboard-based board representation** for efficient position management
- **Complete pseudo-legal move generation** for all piece types
- **Negamax search with alpha-beta pruning** for AI decision making
- **Advanced search optimizations** (transposition tables, killer moves, history heuristic)
- **Interactive graphical interface** with mouse-based piece selection
- **Zobrist hashing** for position caching and game state tracking

## Features

### Core Engine
- ✅ Full chess rule implementation (castling, en passant, pawn promotion)
- ✅ Legal move validation and check detection
- ✅ Position evaluation with piece values and board control metrics
- ✅ Move ordering and pruning heuristics for faster search

### Search & AI
- ✅ Negamax algorithm with alpha-beta pruning
- ✅ Transposition tables for caching evaluated positions
- ✅ Quiescence search for tactical accuracy
- ✅ Late move reductions and move ordering
- ✅ Killer move heuristic and history heuristic

### User Interface
- ✅ Graphical chess board with piece rendering
- ✅ Interactive piece selection and move highlighting
- ✅ Check indicator and last move visualization
- ✅ Side selection menu (play as White or Black)
- ✅ Game over detection with checkmate/stalemate handling
