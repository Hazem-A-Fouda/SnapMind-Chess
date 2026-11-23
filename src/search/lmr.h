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

/* search/lmr.h — LMR reductions: round( ln(depth) * ln(move_idx) / 2 ) */
#ifndef SNAPMIND_SEARCH_LMR_H
#define SNAPMIND_SEARCH_LMR_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t t[64][64];
} LmrTable;

void lmr_init(LmrTable *lmr);

static inline uint8_t lmr_get(const LmrTable *lmr, size_t depth, size_t move_idx) {
    size_t d = depth < 63 ? depth : 63;
    size_t m = move_idx < 63 ? move_idx : 63;
    return lmr->t[d][m];
}

/* Like lmr_get, but adjusts the reduction by the move's history score.
 * A move with good history gets reduced less; a move with bad history gets
 * reduced more.  The adjustment is capped at +-2 plies. */
static inline uint8_t lmr_get_adjusted(const LmrTable *lmr, size_t depth, size_t move_idx, int history) {
    size_t d = depth < 63 ? depth : 63;
    size_t m = move_idx < 63 ? move_idx : 63;
    int base       = (int)lmr->t[d][m];
    int adjustment = history / 8000;
    if (adjustment < -2) adjustment = -2;
    if (adjustment >  2) adjustment =  2;
    int r = base - adjustment;
    return (uint8_t)(r > 0 ? r : 0);
}

#endif /* SNAPMIND_SEARCH_LMR_H */
