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
#include <string.h>

#include "tt.h"

/* Table sizes are derived from these entry sizes; keep them identical to the
 * original Rust layout so hash-table capacity (and behaviour) is unchanged. */
_Static_assert(sizeof(TTEntry) == 24,      "TTEntry must be 24 bytes");
_Static_assert(sizeof(TTBucket) == 48,     "TTBucket must be 48 bytes");
_Static_assert(sizeof(OpeningEntry) == 24, "OpeningEntry must be 24 bytes");

static size_t next_power_of_two(size_t x) {
    size_t p = 1;
    while (p < x) p <<= 1;
    return p;
}

static inline uint8_t pack_flags(Bound bound, bool pv, uint8_t age) {
    uint8_t b = (uint8_t)bound;
    return (uint8_t)(b | ((pv ? 1u : 0u) << 2) | ((age & 0x1Fu) << 3));
}

void tt_init(TranspositionTable *t, size_t size_mb) {
    //size_t n_entries = (size_mb * 1024 * 1024) / sizeof(TTBucket);
    //size_t n = next_power_of_two(n_entries) >> 1;
    size_t n = (size_mb * 1024 * 1024) / sizeof(TTBucket);
    if (n < 1) n = 1;
    t->entries = (TTBucket *)calloc(n, sizeof(TTBucket));   /* all-zero == empty */
    if (!t->entries) {
        fprintf(stderr, "out of memory (transposition table, %zu MB)\n", size_mb);
        exit(1);
    }
    t->n_buckets = (uint64_t)n;
    t->age = 0;
}

void tt_free(TranspositionTable *t) {
    free(t->entries);
    t->entries = NULL;
}

void tt_store(TranspositionTable *t, uint64_t key, uint8_t depth, int score, Move mv, Bound bound, bool pv) {
    TTBucket *bucket = &t->entries[tt_index(t, key)];

    /* No move supplied (e.g. fail-low node): keep whatever move this position
     * already had, rather than erasing it. */
    if (move_is_null(mv)) {
        if (bucket->e[0].key == key)      mv = bucket->e[0].mv;
        else if (bucket->e[1].key == key) mv = bucket->e[1].mv;
    }

    uint8_t flags = pack_flags(bound, pv, t->age);
    TTEntry new_entry;
    new_entry.key   = key;
    new_entry.score = score;
    new_entry.mv    = mv;
    new_entry.depth = depth;
    new_entry.flags = flags;

    /* Slot 0 (depth-preferred): replace when stale, empty, same key, or new
     * entry is at least as deep.  Otherwise fall through to slot 1. */
    TTEntry *slot0 = &bucket->e[0];
    unsigned old_eff = (unsigned)slot0->depth + (tt_entry_pv(slot0) ? 2u : 0u);
    if (old_eff > 255) old_eff = 255;                      /* saturating_add */
    bool replace_slot0 = slot0->key == 0
        || tt_entry_age(slot0) != (t->age & 0x1F)
        || slot0->key == key
        || (bound == BOUND_EXACT && depth >= slot0->depth)
        || depth >= old_eff;

    if (replace_slot0) {
        *slot0 = new_entry;
    } else {
        /* Slot 1 (always-replace): unconditionally overwrite. */
        bucket->e[1] = new_entry;
    }
}

void tt_new_search(TranspositionTable *t) {
    t->age = (uint8_t)(t->age + 1);
}

void tt_clear(TranspositionTable *t) {
    size_t n = (size_t)t->n_buckets;
    memset(t->entries, 0, n * sizeof(TTBucket));
    t->age = 0;
}

/* ── Opening eval cache ── */

void oc_init(OpeningCache *c, size_t size_mb) {
    size_t n = next_power_of_two((size_mb * 1024 * 1024) / sizeof(OpeningEntry)) >> 1;
    if (n < 1) n = 1;
    c->entries = (OpeningEntry *)calloc(n, sizeof(OpeningEntry));
    if (!c->entries) {
        fprintf(stderr, "out of memory (opening cache)\n");
        exit(1);
    }
    c->mask = (uint64_t)n - 1;
}

void oc_free(OpeningCache *c) {
    free(c->entries);
    c->entries = NULL;
}

bool oc_probe(const OpeningCache *c, uint64_t key, OpeningEntry *out) {
    const OpeningEntry *e = &c->entries[key & c->mask];
    if (e->valid && e->key == key) {
        *out = *e;
        return true;
    }
    return false;
}

void oc_store(OpeningCache *c, uint64_t key, int eval, uint8_t depth, Move mv) {
    OpeningEntry *slot = &c->entries[key & c->mask];
    if (slot->valid && slot->key == key && slot->depth > depth) {
        return;
    }
    slot->key   = key;
    slot->eval  = eval;
    slot->depth = depth;
    slot->mv    = mv;
    slot->valid = 1;
}

void oc_clear(OpeningCache *c) {
    size_t n = (size_t)c->mask + 1;
    memset(c->entries, 0, n * sizeof(OpeningEntry));
}