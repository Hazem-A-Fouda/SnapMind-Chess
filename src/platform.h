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

/* platform.h — tiny portability layer (monotonic clock only).

 * No external libraries are used anywhere in this project: only the C
 * standard library plus one OS clock call (clock_gettime on POSIX,
 * QueryPerformanceCounter on Windows).

 * NOTE: when compiling with a strict -std=c11 this header must be the first
 * include of a translation unit (it defines _POSIX_C_SOURCE for <time.h>).
 * The default gcc/clang mode (gnu17) needs nothing special.
 */
#ifndef SNAPMIND_PLATFORM_H
#define SNAPMIND_PLATFORM_H

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
static inline uint64_t now_us(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (uint64_t)(t.QuadPart * 1000000ULL / (uint64_t)freq.QuadPart);
}
#else
#include <time.h>
/* Monotonic clock in microseconds. */
static inline uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}
#endif

#endif /* SNAPMIND_PLATFORM_H */
