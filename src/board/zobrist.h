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

/* board/zobrist.h — Zobrist hashing keys */
#ifndef SNAPMIND_BOARD_ZOBRIST_H
#define SNAPMIND_BOARD_ZOBRIST_H

#include <stdint.h>
#include "bitboard.h"

#define ZOB_PIECE_KEYS  (12 * 64)
#define ZOB_SIDE_KEY    1
#define ZOB_CASTLE_KEYS 16
#define ZOB_EP_KEYS     8
#define ZOB_TOTAL       (ZOB_PIECE_KEYS + ZOB_SIDE_KEY + ZOB_CASTLE_KEYS + ZOB_EP_KEYS)

extern uint64_t g_zobrist_keys[ZOB_TOTAL];

/* Must be called once at start-up (main does this). */
void zobrist_init(void);

static inline int zobrist_piece_index(PieceType piece, Color color) {
    return (int)piece + (color == COLOR_WHITE ? 0 : 6);
}
static inline uint64_t zobrist_piece_key(PieceType piece, Color color, int square) {
    return g_zobrist_keys[zobrist_piece_index(piece, color) * 64 + square];
}
static inline uint64_t zobrist_side_key(void) {
    return g_zobrist_keys[ZOB_PIECE_KEYS];
}
static inline uint64_t zobrist_castle_key(int rights) {
    return g_zobrist_keys[ZOB_PIECE_KEYS + ZOB_SIDE_KEY + (rights & 0xF)];
}
static inline uint64_t zobrist_ep_key(int file) {
    return g_zobrist_keys[ZOB_PIECE_KEYS + ZOB_SIDE_KEY + ZOB_CASTLE_KEYS + (file & 7)];
}

#endif /* SNAPMIND_BOARD_ZOBRIST_H */
