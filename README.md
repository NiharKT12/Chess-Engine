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
- ✅ Negamax with alpha-beta pruning and principal variation search
- ✅ Iterative deepening with aspiration windows
- ✅ Fixed-size transposition table with depth-preferred replacement
- ✅ Quiescence search for tactical accuracy
- ✅ Null move pruning, late move reductions, check extensions
- ✅ Mate-distance pruning and ply-correct mate scores
- ✅ Killer move and history heuristics, MVV-LVA capture ordering

### User Interface
- ✅ Graphical chess board with piece rendering
- ✅ Interactive piece selection and move highlighting
- ✅ Check indicator and last move visualization
- ✅ Side selection menu (play as White or Black)
- ✅ Game over detection: checkmate, stalemate, threefold repetition,
      the fifty-move rule and insufficient material

## Building

### The game

Open `Chess Engine.sln` in Visual Studio and build. The project links against
[raylib](https://www.raylib.com/); point the include and library directories at
your raylib installation. Piece art is loaded from `Assets/` at runtime — the
renderer probes a few relative paths, so it works whether the executable is run
from the IDE or directly.

### Tests

The engine core builds without raylib, so the regression suite runs from the
command line:

```
tests/run_tests.sh      # bash / MSYS2
testsun_tests.bat     # Visual Studio Developer Command Prompt
```

The suite covers:

| Area | What it pins down |
| --- | --- |
| **perft** | Move generation, make/unmake and legality against known node counts (startpos, Kiwipete and positions 3-6) |
| **Board state** | The incremental Zobrist key, cached occupancy bitboards and mailbox all match a from-scratch recomputation after every move |
| **FEN parsing** | Side to move, castling rights, en passant and the halfmove clock, including truncated FENs |
| **Piece-square tables** | Board orientation — a king prefers its own back rank, a pawn gains value as it advances |
| **Evaluation** | Colour symmetry: a position and its exact mirror must score identically |
| **Pawn structure** | Doubled, isolated and passed pawn detection |
| **Draw detection** | Repetition, the fifty-move clock and insufficient material |
| **Search** | Finds forced mates for both colours, captures hanging material, promotes, and reports no move when stalemated |

Add a case here before changing evaluation or search — the colour-symmetry and
piece-square-table checks in particular catch whole classes of orientation and
sign bugs that are otherwise invisible until the engine simply plays badly.
