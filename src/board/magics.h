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

/* board/magics.h — Magic-bitboard attack tables for bishops and rooks.
 *
 * bishop_attacks(sq, occ) and rook_attacks(sq, occ) replace the slow ray-loop
 * approach and are called by movegen and eval transparently.
 * Tables are built once by magics_init() (call it at start-up; < 200 ms).
 */
#ifndef SNAPMIND_BOARD_MAGICS_H
#define SNAPMIND_BOARD_MAGICS_H

#include <stdint.h>
#include <stddef.h>
#ifdef __BMI2__
    #include <immintrin.h>
#endif

typedef struct {
    uint64_t mask;
    uint64_t magic;
    uint32_t shift;
    size_t   offset;
} MagicEntry;

extern MagicEntry g_bishop_magics[64];
extern MagicEntry g_rook_magics[64];
extern uint64_t  *g_bishop_data;
extern uint64_t  *g_rook_data;

void magics_init(void);

static inline size_t magic_index(const MagicEntry *e, uint64_t occ) {
    #ifdef __BMI2__
        return e->offset + (size_t)_pext_u64(occ, e->mask);
    #else
        return e->offset + (size_t)(((occ & e->mask) * e->magic) >> e->shift);
    #endif
}

static inline uint64_t bishop_attacks(int sq, uint64_t occ) {
    return g_bishop_data[magic_index(&g_bishop_magics[sq], occ)];
}

static inline uint64_t rook_attacks(int sq, uint64_t occ) {
    return g_rook_data[magic_index(&g_rook_magics[sq], occ)];
}

#endif /* SNAPMIND_BOARD_MAGICS_H */
