/*
 * SnapMind - a UCI chess engine
 * Copyright (C) 2026 Hazem Fouda
 *
 * SnapMind is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SnapMind is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Derived in part from Stockfish's bitboard.cpp,
 * Copyright (C) the Stockfish developers (GPLv3).
 */

/* board/ray_tables.h — Ray-based lookup tables ported from Stockfish's bitboard.cpp.
 *
 * Provides four O(1) lookups that replace expensive per-move ray-tracing:
 *
 *  - between(s1, s2)       — squares strictly between s1 and s2 (plus s2 itself)
 *  - line(s1, s2)          — full rank/file/diagonal line through s1 and s2
 *  - ray_pass(s1, s2)      — ray from s1 through s2 and continuing to the edge
 *  - square_distance(s1,s2)— Chebyshev (king-step) distance between two squares
 *
 * All tables are built once by ray_tables_init() (called from main).
 */
#ifndef SNAPMIND_BOARD_RAY_TABLES_H
#define SNAPMIND_BOARD_RAY_TABLES_H

#include <stdint.h>

extern uint64_t g_between[64][64];
extern uint64_t g_line[64][64];
extern uint64_t g_ray_pass[64][64];
extern uint8_t  g_sq_dist[64][64];

void ray_tables_init(void);

/* Squares strictly between s1 and s2 on the same rank/file/diagonal, plus s2
 * itself. Returns 0 if the squares are not aligned. */
static inline uint64_t between(int s1, int s2)  { return g_between[s1][s2]; }

/* All squares on the same rank, file, or diagonal through both s1 and s2,
 * including s1 and s2. Returns 0 if the squares are not aligned. */
static inline uint64_t line(int s1, int s2)     { return g_line[s1][s2]; }

/* The ray that starts at s1, passes through s2, and continues to the board
 * edge. Returns 0 if not aligned. */
static inline uint64_t ray_pass(int s1, int s2) { return g_ray_pass[s1][s2]; }

/* Chebyshev (king-step) distance:  max(|rank1-rank2|, |file1-file2|). */
static inline int square_distance(int s1, int s2) { return (int)g_sq_dist[s1][s2]; }

#endif /* SNAPMIND_BOARD_RAY_TABLES_H */
