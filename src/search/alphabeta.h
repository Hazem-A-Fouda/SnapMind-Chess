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

/* search/alphabeta.h — iterative-deepening negamax with the usual pruning /
 * reduction / extension machinery (PVS, LMR, NMP, RFP, razoring, futility,
 * LMP, singular extensions, IIR, correction history, quiescence). */
#ifndef SNAPMIND_SEARCH_ALPHABETA_H
#define SNAPMIND_SEARCH_ALPHABETA_H

#include <stdint.h>
#include <stdbool.h>
#include "../board/bitboard.h"
#include "../board/board.h"
#include "searcher.h"

#define MAX_DEPTH 245
#define MAX_PLY   128

#define PV_CAP 256

typedef struct {
    Move     mv;
    int32_t  score;
    uint8_t  depth;
    uint64_t nodes;
    bool     is_forced;
    Move     pv[PV_CAP];
    int      pv_len;
} SearchResult;

/* Iterative-deepening search from `board` up to max_depth plies. */
SearchResult searcher_search(Searcher *s, Board *board, uint8_t max_depth);

/* Follow the transposition table from `board` and return the validated PV
 * (up to `depth` moves) in pv[]; returns its length. */
int searcher_extract_pv(const Searcher *s, const Board *board, uint8_t depth, Move *pv);

#endif /* SNAPMIND_SEARCH_ALPHABETA_H */
