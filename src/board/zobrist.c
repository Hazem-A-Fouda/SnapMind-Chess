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

#include "zobrist.h"

uint64_t g_zobrist_keys[ZOB_TOTAL];

/* Simple 64-bit LCG (same constants and seed as the Rust version). */
static uint64_t lcg_next(uint64_t *state) {
    *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state;
}

void zobrist_init(void) {
    uint64_t rng = 0xDEADBEEFCAFEBABEULL;
    for (int i = 0; i < ZOB_TOTAL; i++) g_zobrist_keys[i] = lcg_next(&rng);
}
