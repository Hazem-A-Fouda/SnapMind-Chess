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

/* eval/pawn_tt.h — Pawn hash table.
 *
 * Caches the pawn-structure evaluation keyed by a Zobrist hash of the pawn
 * bitboards only.  Pawn structure changes infrequently relative to total node
 * count, so caching its evaluation avoids redundant computation on most nodes.
 */
#ifndef SNAPMIND_EVAL_PAWN_TT_H
#define SNAPMIND_EVAL_PAWN_TT_H

#include <stdint.h>
#include <stddef.h>
#include "../board/board.h"

typedef struct {
    uint64_t key;
    int32_t  mg;
    int32_t  eg;
} PawnEntry;

typedef struct {
    PawnEntry *entries;
    uint64_t   mask;
} PawnHashTable;

void pawn_ht_init(PawnHashTable *t, size_t size_mb);
void pawn_ht_clear(PawnHashTable *t);

/* Returns the entry for `key`, or NULL on a miss. */
static inline const PawnEntry *pawn_ht_probe(const PawnHashTable *t, uint64_t key) {
    const PawnEntry *e = &t->entries[key & t->mask];
    return (e->key == key) ? e : NULL;
}

static inline void pawn_ht_store(PawnHashTable *t, uint64_t key, int mg, int eg) {
    PawnEntry *e = &t->entries[key & t->mask];
    e->key = key;
    e->mg  = mg;
    e->eg  = eg;
}

/* Zobrist hash over the pawn bitboards only (both colours).
 * NOTE: this hash intentionally excludes king positions. */
uint64_t pawn_hash(const Board *board);

#endif /* SNAPMIND_EVAL_PAWN_TT_H */
