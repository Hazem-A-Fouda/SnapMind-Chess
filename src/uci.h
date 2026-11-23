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

/* uci.h — UCI protocol front-end */
#ifndef SNAPMIND_UCI_H
#define SNAPMIND_UCI_H

#include "board/board.h"
#include "search/search.h"

typedef struct {
    Board    board;
    Searcher searcher;        /* ~2 MB: keep the engine in static/heap memory */
    U64Vec   game_history;
} UciEngine;

/* Initialises *e in place (start position, 64 MB hash, 16 MB opening cache). */
void uci_engine_init(UciEngine *e);

/* Reads UCI commands from stdin until EOF or "quit". */
void uci_engine_run(UciEngine *e);

#endif /* SNAPMIND_UCI_H */
