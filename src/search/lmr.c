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

#include "lmr.h"

/* Natural logarithm without libm (so the engine links with no -lm).
 * Range-reduce to [sqrt(1/2), sqrt(2)) and use the atanh series
 *     ln(x) = 2 * (z + z^3/3 + z^5/5 + ...),   z = (x-1)/(x+1)
 * which converges to full double precision in a handful of terms.
 * Only used once, at start-up, for x in 1..63. */
static double ln_approx(double x) {
    const double LN2 = 0.693147180559945309417232121458;
    int e = 0;
    while (x >= 1.4142135623730951) { x *= 0.5; e++; }
    while (x <  0.7071067811865476) { x *= 2.0; e--; }
    double z    = (x - 1.0) / (x + 1.0);
    double z2   = z * z;
    double term = z;
    double sum  = 0.0;
    for (int k = 1; k < 60; k += 2) {
        sum  += term / (double)k;
        term *= z2;
    }
    return 2.0 * sum + (double)e * LN2;
}

void lmr_init(LmrTable *lmr) {
    for (int d = 0; d < 64; d++)
        for (int m = 0; m < 64; m++)
            lmr->t[d][m] = 0;

    for (int d = 1; d < 64; d++) {
        for (int m = 1; m < 64; m++) {
            double x = ln_approx((double)d) * ln_approx((double)m) / 2.0;
            /* round half away from zero (x is never negative here) */
            int whole = (int)x;
            int r = whole + ((x - (double)whole) >= 0.5 ? 1 : 0);
            if (r > 10) r = 10;
            lmr->t[d][m] = (uint8_t)r;
        }
    }
}
