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

/* board/movegen.h — legal / pseudo-legal move generation and attack helpers. */
#ifndef SNAPMIND_BOARD_MOVEGEN_H
#define SNAPMIND_BOARD_MOVEGEN_H

#include <stdint.h>
#include <stdbool.h>
#include "bitboard.h"
#include "board.h"
#include "magics.h"

/* ── Stack-allocated move list ──
 * The maximum number of legal moves in any reachable chess position is ~218.
 * 256 is a round power-of-two safely above that ceiling. */
#define MOVELIST_CAP 256

typedef struct {
    Move moves[MOVELIST_CAP];
    int  len;
} MoveList;

static inline void ml_init(MoveList *ml)           { ml->len = 0; }
static inline void ml_push(MoveList *ml, Move mv)  { ml->moves[ml->len++] = mv; }
static inline bool ml_is_empty(const MoveList *ml) { return ml->len == 0; }
static inline bool ml_contains(const MoveList *ml, Move mv) {
    for (int i = 0; i < ml->len; i++) if (ml->moves[i] == mv) return true;
    return false;
}

/* Attack tables (computed on the fly, no lookup needed). */
static inline Bitboard knight_attacks(int sq) {
    Bitboard b  = bit(sq);
    Bitboard l1 = (b >> 1) & ~FILE_H;
    Bitboard l2 = (b >> 2) & ~(FILE_G | FILE_H);
    Bitboard r1 = (b << 1) & ~FILE_A;
    Bitboard r2 = (b << 2) & ~(FILE_A | FILE_B);
    return ((l1 | r1) << 16) | ((l1 | r1) >> 16) | ((l2 | r2) << 8) | ((l2 | r2) >> 8);
}

static inline Bitboard king_attacks(int sq) {
    Bitboard b = bit(sq);
    Bitboard l = (b >> 1) & ~FILE_H;
    Bitboard r = (b << 1) & ~FILE_A;
    Bitboard h = l | b | r;
    return (h << 8) | (h >> 8) | l | r;
}

/* Bishop / rook attacks come from magics.h (bishop_attacks / rook_attacks). */

/* Pieces of `color` that are absolutely pinned to their king. */
Bitboard pinned_pieces(const Board *board, Color color);

void generate_legal_moves(Board *board, MoveList *out);
/* Only legal captures + promotions — used by quiescence search. */
void generate_legal_captures(Board *board, MoveList *out);

bool is_in_check(const Board *board, Color color);
/* True if `sq` is attacked by any piece of `attacker` color. */
bool square_attacked_by(const Board *board, int sq, Color attacker);

#endif /* SNAPMIND_BOARD_MOVEGEN_H */
