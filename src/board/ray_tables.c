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

#include "bitboard.h"
#include "ray_tables.h"

uint64_t g_between[64][64];
uint64_t g_line[64][64];
uint64_t g_ray_pass[64][64];
uint8_t  g_sq_dist[64][64];

/* Slow sliding attack generator (used only at init time).
 * bishop=true → diagonal rays; bishop=false → rank/file rays.
 * occ is the occupancy that blocks the ray. */
static uint64_t slow_attacks(int sq, uint64_t occ, bool bishop) {
    static const int bdirs[4][2] = { {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
    static const int rdirs[4][2] = { {0, 1}, {0, -1}, {1, 0}, {-1, 0} };
    const int (*dirs)[2] = bishop ? bdirs : rdirs;
    int r = sq / 8, f = sq % 8;

    uint64_t attacks = 0;
    for (int d = 0; d < 4; d++) {
        int dr = dirs[d][0], df = dirs[d][1];
        int cr = r + dr, cf = f + df;
        while (cr >= 0 && cr < 8 && cf >= 0 && cf < 8) {
            uint64_t b = bit(cr * 8 + cf);
            attacks |= b;
            if ((occ & b) != 0) break;
            cr += dr;
            cf += df;
        }
    }
    return attacks;
}

static int iabs_i(int x) { return x < 0 ? -x : x; }

void ray_tables_init(void) {
    /* ── Square distance (Chebyshev) ── */
    for (int s1 = 0; s1 < 64; s1++) {
        for (int s2 = 0; s2 < 64; s2++) {
            int rf = iabs_i(s1 / 8 - s2 / 8);
            int ff = iabs_i(s1 % 8 - s2 % 8);
            g_sq_dist[s1][s2] = (uint8_t)(rf > ff ? rf : ff);
        }
    }

    /* ── Line / Between / RayPass ──
     * Mirrors Stockfish: for each pair of squares, check whether a bishop or
     * rook on s1 (on an empty board) can reach s2.  If so, compute the three
     * derived ray bitboards. */
    for (int s1 = 0; s1 < 64; s1++) {
        for (int s2 = 0; s2 < 64; s2++) {
            g_between[s1][s2]  = 0;
            g_line[s1][s2]     = 0;
            g_ray_pass[s1][s2] = 0;

            /* Try both slider types (bishop = diagonal, rook = rank/file) */
            for (int k = 0; k < 2; k++) {
                bool bishop = (k == 0);
                uint64_t pseudo = slow_attacks(s1, 0, bishop);

                if ((pseudo & bit(s2)) != 0) {
                    /* LineBB */
                    uint64_t line_bb = (slow_attacks(s1, 0, bishop) & slow_attacks(s2, 0, bishop))
                                     | bit(s1) | bit(s2);
                    g_line[s1][s2] |= line_bb;

                    /* BetweenBB */
                    uint64_t between_bb = (slow_attacks(s1, bit(s2), bishop)
                                         & slow_attacks(s2, bit(s1), bishop))
                                        | bit(s2);
                    g_between[s1][s2] |= between_bb;

                    /* RayPassBB */
                    uint64_t ray_bb = slow_attacks(s1, 0, bishop)
                                    & (slow_attacks(s2, bit(s1), bishop) | bit(s2));
                    g_ray_pass[s1][s2] |= ray_bb;
                }
            }
        }
    }
}
