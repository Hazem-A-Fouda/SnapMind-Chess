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
 */

#include <stdio.h>
#include <stdlib.h>

#include "pawn_tt.h"
#include "../board/zobrist.h"

/* Empty-slot sentinel: UINT64_MAX, so that a real position whose pawn hash
 * happens to be 0 does not collide with an uninitialized entry and silently
 * receive a cached (mg=0, eg=0) score. */
#define PAWN_EMPTY_KEY UINT64_MAX

_Static_assert(sizeof(PawnEntry) == 16, "PawnEntry must be 16 bytes");

static size_t next_power_of_two(size_t x) {
    size_t p = 1;
    while (p < x) p <<= 1;
    return p;
}

void pawn_ht_init(PawnHashTable *t, size_t size_mb) {
    size_t n = next_power_of_two((size_mb * 1024 * 1024) / sizeof(PawnEntry)) >> 1;
    if (n < 1) n = 1;
    t->entries = (PawnEntry *)malloc(n * sizeof(PawnEntry));
    if (!t->entries) { fprintf(stderr, "out of memory (pawn table)\n"); exit(1); }
    t->mask = (uint64_t)n - 1;
    for (size_t i = 0; i < n; i++) {
        t->entries[i].key = PAWN_EMPTY_KEY;
        t->entries[i].mg  = 0;
        t->entries[i].eg  = 0;
    }
}

void pawn_ht_clear(PawnHashTable *t) {
    size_t n = (size_t)t->mask + 1;
    for (size_t i = 0; i < n; i++) {
        t->entries[i].key = PAWN_EMPTY_KEY;
        t->entries[i].mg  = 0;
        t->entries[i].eg  = 0;
    }
}

uint64_t pawn_hash(const Board *board) {
    uint64_t h = 0;
    Bitboard white_pawns = board_bb(board, COLOR_WHITE, PT_PAWN);
    while (white_pawns != 0) {
        int sq = ctz64(white_pawns);
        h ^= zobrist_piece_key(PT_PAWN, COLOR_WHITE, sq);
        white_pawns &= white_pawns - 1;
    }
    Bitboard black_pawns = board_bb(board, COLOR_BLACK, PT_PAWN);
    while (black_pawns != 0) {
        int sq = ctz64(black_pawns);
        h ^= zobrist_piece_key(PT_PAWN, COLOR_BLACK, sq);
        black_pawns &= black_pawns - 1;
    }
    return h;
}
