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

/* board/board.h — Board state, make/unmake, FEN parsing.
 * (This is the C counterpart of board/mod_.rs.)
 */
#ifndef SNAPMIND_BOARD_BOARD_H
#define SNAPMIND_BOARD_BOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "bitboard.h"

typedef enum { PHASE_OPENING, PHASE_MIDDLEGAME, PHASE_ENDGAME } GamePhase;

/* State snapshot saved by board_make_move and consumed by board_unmake_move.
 * en_passant and captured_piece use -1 for "none" (Option::None). */
typedef struct {
    uint8_t  castling_rights;
    int      en_passant;        /* -1 = none, else square 0-63 */
    uint16_t halfmove_clock;
    uint16_t fullmove_number;
    uint64_t hash;
    int      captured_piece;    /* -1 = none, else PieceType */
    uint8_t  captured_sq;
} UndoInfo;

typedef struct {
    Bitboard pieces[12];
    Bitboard occ_white;
    Bitboard occ_black;
    Bitboard occ_all;
    /* Per-square piece identity: 0xFF = empty, else color_offset*6 + piece_offset (0-11). */
    uint8_t  mailbox[64];
    Color    side_to_move;
    uint8_t  castling_rights;
    int      en_passant;        /* -1 = none, else square 0-63 */
    uint16_t halfmove_clock;
    uint16_t fullmove_number;
    uint64_t hash;
} Board;

static inline int color_offset(Color c) { return c == COLOR_WHITE ? 0 : 6; }
static inline int piece_offset(PieceType p) { return (int)p; }

static inline Bitboard board_bb(const Board *b, Color color, PieceType piece) {
    return b->pieces[color_offset(color) + piece_offset(piece)];
}
static inline Bitboard *board_bb_mut(Board *b, Color color, PieceType piece) {
    return &b->pieces[color_offset(color) + piece_offset(piece)];
}
static inline Bitboard board_occupied(const Board *b) { return b->occ_all; }
static inline Bitboard board_occupied_by(const Board *b, Color color) {
    return color == COLOR_WHITE ? b->occ_white : b->occ_black;
}

/* O(1) piece lookup via mailbox (Option<(PieceType, Color)>). */
typedef struct {
    bool      some;
    PieceType piece;
    Color     color;
} PieceOn;

static inline PieceOn piece_on_fast(const Board *b, int sq) {
    PieceOn r;
    uint8_t v = b->mailbox[sq];
    if (v == 0xFF) { r.some = false; r.piece = PT_PAWN; r.color = COLOR_WHITE; return r; }
    r.some  = true;
    r.color = (v < 6) ? COLOR_WHITE : COLOR_BLACK;
    r.piece = (PieceType)(v % 6);
    return r;
}

/* Compatibility shim — delegates to piece_on_fast. */
static inline PieceOn piece_on(const Board *b, int sq) { return piece_on_fast(b, sq); }

uint32_t  board_phase_material(const Board *b);
GamePhase board_game_phase(const Board *b);

void board_startpos(Board *out);
/* Returns false (and leaves *out unspecified) if the FEN is malformed. */
bool board_from_fen(Board *out, const char *fen);

UndoInfo board_make_move(Board *b, Move mv);
void     board_unmake_move(Board *b, Move mv, UndoInfo undo);
UndoInfo board_make_null_move(Board *b);
void     board_unmake_null_move(Board *b, UndoInfo undo);

#endif /* SNAPMIND_BOARD_BOARD_H */
