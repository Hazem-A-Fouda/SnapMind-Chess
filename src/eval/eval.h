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

/* eval/eval.h — static evaluation (C counterpart of eval/mod_.rs). */
#ifndef SNAPMIND_EVAL_EVAL_H
#define SNAPMIND_EVAL_EVAL_H

#include "../board/board.h"

#define PAWN_VAL   100
#define KNIGHT_VAL 320
#define BISHOP_VAL 330
#define ROOK_VAL   500
#define QUEEN_VAL  900
#define KING_VAL   20000
#define MATE_SCORE 100000

/* Must be called once at start-up (allocates the pawn hash table). */
void eval_init(void);

/* Material-only evaluation, from the side to move's point of view. */
int simple_eval(const Board *board);

/* Full evaluation, from the side to move's point of view (centipawns). */
int evaluate(const Board *board);

#endif /* SNAPMIND_EVAL_EVAL_H */
