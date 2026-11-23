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

/* search/tt.h — transposition table and opening-eval cache */
#ifndef SNAPMIND_SEARCH_TT_H
#define SNAPMIND_SEARCH_TT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../board/bitboard.h"

typedef enum {
    BOUND_EXACT = 0,
    BOUND_LOWER = 1,   /* beta cutoff (fail-high) */
    BOUND_UPPER = 2    /* no alpha improvement (fail-low) */
} Bound;

/* Packed TT entry.
 * `flags` encodes three former separate fields into one byte:
 *   bits 0-1 : bound  (0=Exact, 1=Lower, 2=Upper)
 *   bit  2   : pv flag
 *   bits 3-7 : age    (5 bits, wraps at 32 generations)
 * key(8) + score(4) + mv(4) + depth(1) + flags(1) = 18 bytes, padded to 24. */
typedef struct {
    uint64_t key;
    int32_t  score;
    Move     mv;
    uint8_t  depth;
    uint8_t  flags;
} TTEntry;

static inline Bound   tt_entry_bound(const TTEntry *e) {
    switch (e->flags & 0x03) {
        case 0:  return BOUND_EXACT;
        case 1:  return BOUND_LOWER;
        default: return BOUND_UPPER;
    }
}
static inline bool    tt_entry_pv(const TTEntry *e)  { return (e->flags & 0x04) != 0; }
static inline uint8_t tt_entry_age(const TTEntry *e) { return (uint8_t)(e->flags >> 3); }

/* Two-bucket layout per index:
 *   e[0] = depth-preferred (replace only when stale/shallower)
 *   e[1] = always-replace  (absorbs all other writes) */
typedef struct {
    TTEntry e[2];
} TTBucket;

typedef struct {
    TTBucket *entries;
    uint64_t  n_buckets; // was 'mask'
    uint8_t   age;
} TranspositionTable;

/* Allocates a table of ~size_mb megabytes (any previous contents of *t are
 * NOT freed — call tt_free first when replacing a table). */
void tt_init(TranspositionTable *t, size_t size_mb);
void tt_free(TranspositionTable *t);

static inline uint64_t tt_index(const TranspositionTable *t, uint64_t key) {
#if defined(__SIZEOF_INT128__)
    return (uint64_t)(((unsigned __int128)key * t->n_buckets) >> 64);
#else
    return key % t->n_buckets;
#endif
}

/* Returns the entry for `key`, or NULL on a miss. */
static inline const TTEntry *tt_probe(const TranspositionTable *t, uint64_t key) {
    const TTBucket *bucket = &t->entries[tt_index(t, key)];
    /* Check depth-preferred slot first, then always-replace slot. */
    if (bucket->e[0].key == key) return &bucket->e[0];
    if (bucket->e[1].key == key) return &bucket->e[1];
    return NULL;
}

void tt_store(TranspositionTable *t, uint64_t key, uint8_t depth, int score, Move mv, Bound bound, bool pv);
void tt_new_search(TranspositionTable *t);
void tt_clear(TranspositionTable *t);

/* ── Opening eval cache ── */

typedef struct {
    uint64_t key;
    int32_t  eval;
    Move     mv;
    uint8_t  depth;
    uint8_t  valid;    /* replaces Option<OpeningEntry>: 0 = None */
} OpeningEntry;

typedef struct {
    OpeningEntry *entries;
    uint64_t      mask;
} OpeningCache;

void oc_init(OpeningCache *c, size_t size_mb);
void oc_free(OpeningCache *c);
/* Returns true and fills *out if `key` is cached. */
bool oc_probe(const OpeningCache *c, uint64_t key, OpeningEntry *out);
void oc_store(OpeningCache *c, uint64_t key, int eval, uint8_t depth, Move mv);
void oc_clear(OpeningCache *c);

#endif /* SNAPMIND_SEARCH_TT_H */
