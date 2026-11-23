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

/* search/movepick.h — move ordering */
#ifndef SNAPMIND_SEARCH_MOVEPICK_H
#define SNAPMIND_SEARCH_MOVEPICK_H

#include <stdint.h>
#include "../board/bitboard.h"
#include "../board/board.h"
#include "history.h"

/* Stack-allocated list of scored moves — avoids per-node heap allocation. */
#define SCOREDLIST_CAP 256

typedef struct {
    ScoredMove moves[SCOREDLIST_CAP];
    int        len;
} ScoredList;

/* Scores `n` moves and sorts them by descending priority into *out.
 * The sort is stable, so moves with equal scores keep generation order.
 *
 * tt_move / prev_move / cont2_ctx use MOVE_NONE for "no move" (Option::None). */
void score_moves(ScoredList *out, const Move *moves, int n, const Board *board,
                 uint16_t ply, Move tt_move, const HistoryTable *history,
                 Move prev_move, Move cont2_ctx);

#endif /* SNAPMIND_SEARCH_MOVEPICK_H */
