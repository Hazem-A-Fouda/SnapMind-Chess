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

/* SnapMind — UCI chess engine (C port).
 *
 * Build:   make      (see the Makefile for the equivalent single gcc command)
 * Run:     ./snapmind      then talk UCI on stdin/stdout.
 */
#include <stdio.h>

#include "board/magics.h"
#include "board/ray_tables.h"
#include "board/zobrist.h"
#include "eval/eval.h"
#include "uci.h"

/* The engine holds ~2 MB of history tables, so it lives in static storage
 * rather than on the stack (Android/Termux stacks can be small). */
static UciEngine g_engine;

int main(void) {
    /* Line-buffered stdout so every UCI reply reaches the GUI immediately. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* One-time table construction (a few tens of milliseconds). */
    magics_init();
    ray_tables_init();
    zobrist_init();
    eval_init();

    uci_engine_init(&g_engine);
    uci_engine_run(&g_engine);
    return 0;
}
