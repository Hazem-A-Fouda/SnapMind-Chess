# SnapMind

SnapMind is a UCI chess engine written in portable C. It has no external
dependencies: only the C standard library plus one OS clock call. It does not
use NNUE or any neural network, and it has no opening book or endgame
tablebase support.

- **Author:** Hazem Fouda
- **Language:** C (gnu17 / C11)
- **Protocol:** UCI
- **Threads:** single-threaded

SnapMind began as a Rust engine and was ported to C. Some comments in the
source still refer to "the Rust version".

## Building

No libraries are needed. From the `src/` directory:

```sh
gcc -O3 -o snapmind main.c uci.c board/*.c eval/*.c search/*.c
```

For a faster build on CPUs with BMI2 (Intel Haswell or newer, AMD Zen 3 or
newer), add `-march=native` or `-mbmi2`. The engine then uses `PEXT` for
sliding-piece attack lookups. Without BMI2 it falls back to classic magic
bitboards.

```sh
gcc -O3 -march=native -o snapmind main.c uci.c board/*.c eval/*.c search/*.c
```

Notes:

- With a strict `-std=c11`, `platform.h` must be the first include of a
  translation unit. The default `gnu17` mode needs nothing special.
- On Windows, compile with MinGW-w64 or clang. The clock uses
  `QueryPerformanceCounter` there.
- Large tables live in static storage, so the engine also runs on small-stack
  platforms such as Android/Termux.
- On Windows PowerShell, `*` isn't expanded for you, so use:

  ```powershell
  gcc -O3 -o snapmind.exe main.c uci.c (Get-ChildItem board,eval,search -Filter *.c).FullName
  ```

  or run the Unix-style command from Git Bash or MSYS2.

## Running

SnapMind speaks UCI over stdin/stdout. Load the binary into any UCI GUI
(Arena, Cute Chess, En Croissant, Lucas Chess, ...) or a tournament manager
(cutechess-cli, fastchess), or run it from a terminal:

```
$ ./snapmind
uci
id name SnapMind
id author Hazem Fouda
option name Hash type spin default 64 min 1 max 1024
uciok
position startpos
go movetime 1000
```

### UCI options

| Option | Type | Default | Range | Description |
| ------ | ---- | ------- | ----- | ----------- |
| `Hash` | spin | 64      | 1–1024 | Transposition table size in MB |

### Supported commands

`uci`, `isready`, `setoption`, `ucinewgame`, `position [startpos | fen ...] [moves ...]`,
`go`, `stop`, `quit`.

`go` understands `wtime`, `btime`, `winc`, `binc`, `movestogo`, `movetime`,
`depth` and `infinite`.

Pondering, `go nodes` and multi-PV are not implemented.

### Extra commands

- `perft <depth>` runs a move-generation test and prints nodes, nps and time.
  For example, `perft 5` from the start position returns `4865609` nodes.

## Features

### Board and move generation

- Bitboard representation with a mailbox for piece lookup
- Magic bitboards, with a PEXT path when built with BMI2
- Precomputed `between`, `line` and ray tables
- Zobrist hashing
- Static exchange evaluation (SEE)

### Search

- Iterative deepening with aspiration windows
- Negamax alpha-beta with a transposition table (two-bucket layout, age and PV flags)
- Check extensions
- Singular extensions, including double extensions
- Mate distance pruning
- Null-move pruning
- Reverse futility pruning, with an "improving" adjustment
- Razoring
- Late move pruning
- Late move reductions
- Internal iterative reduction
- Quiescence search with delta pruning
- Draw-score randomization to break ties between drawn lines
- Two-tier time management (soft and hard limits)

### Move ordering

- Hash move
- SEE-based capture ordering
- Killer moves and countermoves
- Butterfly history
- Continuation history (1-ply and 2-ply)
- Capture history
- Pawn-structure correction history, applied to the static eval

### Evaluation

A hand-written tapered evaluation that blends middlegame and endgame scores by
game phase:

- Material and piece-square tables
- Mobility for knights, bishops, rooks and queens
- Pawn structure (doubled, isolated, backward and connected pawns), cached in a
  pawn hash table
- Passed pawns, including king proximity and rooks behind passers
- Rooks on open and semi-open files
- Bishop pair
- Knight and bishop outposts
- Threats (pawn attacks on pieces, minors attacking majors, hanging pieces)
- King safety (pawn shield, open files, attacker weights)
- Tempo bonus and fifty-move-rule scaling

## Project layout

```
main.c            entry point and table initialisation
uci.c / uci.h     UCI front-end, time management
platform.h        monotonic clock (POSIX / Windows)
board/            bitboards, board state, move generation, magics, ray tables, Zobrist
eval/             static evaluation and pawn hash table
search/           alpha-beta, move picker, history heuristics, LMR, SEE, TT, searcher state
```

## Credits and acknowledgements

SnapMind's search, evaluation structure and board code are the author's own.
The following parts draw on other people's work:

- **Piece-square tables and piece values.** The middlegame piece-square tables
  and the base piece values (100 / 320 / 330 / 500 / 900) are taken from the
  *Simplified Evaluation Function* by Tomasz Michniewski, published on the
  [Chess Programming Wiki](https://www.chessprogramming.org/Simplified_Evaluation_Function).
  The endgame tables are adjusted variants.
- **Ray tables.** `board/ray_tables.*` (`between`, `line`, ray lookups) is
  derived in part from Stockfish's 'bitboard.cpp'. Stockfish is developed by
  the Stockfish authors and released under the GPLv3.
- **Magic bitboards.** Attack generation follows the standard method described
  on the Chess Programming Wiki (sparse random magics, carry-rippler subset
  enumeration, as popularised by Tord Romstad).
- **Search techniques.** Techniques such as null-move pruning, late move
  reductions, singular extensions, history heuristics and correction history
  are widely used across the computer-chess community. Thanks to the
  [Chess Programming Wiki](https://www.chessprogramming.org/) and the engine
  authors who publish their work openly.

## Testing

- `perft` is available from the UCI loop for verifying move generation.
- To measure strength, play SnapMind against other engines with
  [cutechess-cli](https://github.com/cutechess/cutechess) or
  [fastchess](https://github.com/Disservin/fastchess) using a balanced opening
  set.

## License

SnapMind is free software, licensed under the GNU General Public License,
version 3 or (at your option) any later version. See [LICENSE](LICENSE) for
the full text.

Parts of the code are derived from Stockfish (GPLv3); see the credits above.
SnapMind comes with ABSOLUTELY NO WARRANTY.
Test: syncing after username change.
