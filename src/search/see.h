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

/* search/see.h — Static Exchange Evaluation.
 *
 * Net material gain after all recaptures.  Positive = good capture; negative =
 * losing exchange.  Pin-aware: pieces absolutely pinned to their king cannot
 * legally recapture, so they are never selected as attackers.
 */
#ifndef SNAPMIND_SEARCH_SEE_H
#define SNAPMIND_SEARCH_SEE_H

#include <stdint.h>
#include <stdbool.h>
#include "../board/board.h"

int see(const Board *board, int from, int to);

/* Least-valuable attacker of `sq` by `color`, using `occ` as the occupancy.
 * `pinned` = set of `color`'s pieces that are absolutely pinned (excluded).
 * Returns true and fills *att_sq / *att_val when an attacker exists. */
bool lva(const Board *board, int sq, Color color, uint64_t occ, uint64_t pinned,
         int *att_sq, int *att_val);

int val_on(const Board *board, int sq);

#endif /* SNAPMIND_SEARCH_SEE_H */
