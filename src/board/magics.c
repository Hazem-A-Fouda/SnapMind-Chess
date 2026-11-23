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

#include "bitboard.h"
#include "magics.h"

#ifdef __BMI2__
    #include <immintrin.h>
#endif

MagicEntry g_bishop_magics[64];
MagicEntry g_rook_magics[64];
uint64_t  *g_bishop_data = NULL;
uint64_t  *g_rook_data   = NULL;

/* ── Slow (correct) attack generators — used only during table construction ── */

static uint64_t slow_bishop(int sq, uint64_t occ) {
    static const int dirs[4][2] = { {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
    int r = sq / 8, f = sq % 8;
    uint64_t a = 0;
    for (int d = 0; d < 4; d++) {
        int dr = dirs[d][0], df = dirs[d][1];
        int cr = r + dr, cf = f + df;
        while (cr >= 0 && cr < 8 && cf >= 0 && cf < 8) {
            uint64_t b = bit(cr * 8 + cf);
            a |= b;
            if ((occ & b) != 0) break;
            cr += dr; cf += df;
        }
    }
    return a;
}

static uint64_t slow_rook(int sq, uint64_t occ) {
    static const int dirs[4][2] = { {0, 1}, {0, -1}, {1, 0}, {-1, 0} };
    int r = sq / 8, f = sq % 8;
    uint64_t a = 0;
    for (int d = 0; d < 4; d++) {
        int dr = dirs[d][0], df = dirs[d][1];
        int cr = r + dr, cf = f + df;
        while (cr >= 0 && cr < 8 && cf >= 0 && cf < 8) {
            uint64_t b = bit(cr * 8 + cf);
            a |= b;
            if ((occ & b) != 0) break;
            cr += dr; cf += df;
        }
    }
    return a;
}

/* ── Relevant-occupancy masks ──
 * Edge squares are excluded: a blocker on the edge of the board cannot further
 * block the ray, so it is irrelevant to the attack set. */

static uint64_t bishop_mask(int sq) {
    static const int dirs[4][2] = { {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
    int r = sq / 8, f = sq % 8;
    uint64_t m = 0;
    for (int d = 0; d < 4; d++) {
        int dr = dirs[d][0], df = dirs[d][1];
        int cr = r + dr, cf = f + df;
        while (cr >= 1 && cr <= 6 && cf >= 1 && cf <= 6) {
            m |= bit(cr * 8 + cf);
            cr += dr; cf += df;
        }
    }
    return m;
}

static uint64_t rook_mask(int sq) {
    int r = sq / 8, f = sq % 8;
    uint64_t m = 0;
    for (int cf = 1; cf < 7; cf++) { if (cf != f) m |= bit(r * 8 + cf); }
    for (int cr = 1; cr < 7; cr++) { if (cr != r) m |= bit(cr * 8 + f); }
    return m;
}

/* ── Sparse random (AND of 3 xorshift randoms — fewer bits → faster search) ── */

static uint64_t rand_step(uint64_t *s) {
    *s ^= *s >> 12;
    *s ^= *s << 25;
    *s ^= *s >> 27;
    return *s * 0x2545F4914F6CDD1DULL;
}

static uint64_t sparse_rand(uint64_t *s) {
    /* Explicit sequencing: r() & r() & r() evaluates left to right. */
    uint64_t a = rand_step(s);
    uint64_t b = rand_step(s);
    uint64_t c = rand_step(s);
    return a & b & c;
}

/* ── Occupancy subset enumeration (Carry-Rippler trick) ── */

#define MAX_SUBSETS 4096   /* 2^12: the largest rook mask has 12 relevant bits */

static uint64_t g_occs[MAX_SUBSETS];
static uint64_t g_atks[MAX_SUBSETS];
static uint64_t g_scratch[MAX_SUBSETS];

static size_t subsets(int sq, uint64_t mask, bool bishop) {
    size_t n = 0;
    uint64_t sub = 0;
    for (;;) {
        g_occs[n] = sub;
        g_atks[n] = bishop ? slow_bishop(sq, sub) : slow_rook(sq, sub);
        n++;
        if (sub == mask) break;
        sub = (sub - mask) & mask;
    }
    return n;
}

/* ── Magic finder ──
 * Fills `table` (size entries) and returns the magic. */
static uint64_t find_magic(size_t n_subsets, size_t size, uint32_t shift,
                           uint64_t *rng, uint64_t *table) {
    for (;;) {
        uint64_t magic = sparse_rand(rng);
        for (size_t i = 0; i < size; i++) g_scratch[i] = UINT64_MAX;
        bool ok = true;

        for (size_t i = 0; i < n_subsets; i++) {
            size_t idx = (size_t)((g_occs[i] * magic) >> shift);
            if (g_scratch[idx] == UINT64_MAX) {
                g_scratch[idx] = g_atks[i];
            } else if (g_scratch[idx] != g_atks[i]) {
                ok = false;
                break;
            }
        }

        if (ok) {
            /* Replace remaining sentinel with 0 (valid empty attack set). */
            for (size_t i = 0; i < size; i++) {
                table[i] = (g_scratch[i] == UINT64_MAX) ? 0 : g_scratch[i];
            }
            return magic;
        }
    }
}

static void build_one(MagicEntry entries[64], uint64_t **data_out, bool bishop, uint64_t *rng) {
    /* First pass: total table size. */
    size_t total = 0;
    for (int sq = 0; sq < 64; sq++) {
        uint64_t mask = bishop ? bishop_mask(sq) : rook_mask(sq);
        total += (size_t)1 << popcount64(mask);
    }
    uint64_t *data = (uint64_t *)malloc(total * sizeof(uint64_t));
    if (!data) { fprintf(stderr, "out of memory (magic tables)\n"); exit(1); }

    size_t offset = 0;
    for (int sq = 0; sq < 64; sq++) {
        uint64_t mask  = bishop ? bishop_mask(sq) : rook_mask(sq);
        int      bits  = popcount64(mask);
        uint32_t shift = (uint32_t)(64 - bits);
        size_t   size  = (size_t)1 << bits;
        size_t   n     = subsets(sq, mask, bishop);

        #ifdef __BMI2__
            uint64_t *tbl = data + offset;
            for (size_t i = 0; i < n; i++)
                tbl[_pext_u64(g_occs[i], mask)] = g_atks[i];
            uint64_t magic = 0;   /* unused with PEXT */
            (void)rng;
        #else
            uint64_t magic = find_magic(n, size, shift, rng, data + offset);
        #endif
        entries[sq].mask   = mask;
        entries[sq].magic  = magic;
        entries[sq].shift  = shift;
        entries[sq].offset = offset;
        offset += size;
    }
    *data_out = data;
}

void magics_init(void) {
    if (g_bishop_data != NULL) return;   /* already built */
    uint64_t rng = 0xDEADBEEFCAFEBABEULL;
    build_one(g_bishop_magics, &g_bishop_data, true,  &rng);
    build_one(g_rook_magics,   &g_rook_data,   false, &rng);
}
