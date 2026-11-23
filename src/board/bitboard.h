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

/* board/bitboard.h — basic types: Bitboard, PieceType, Color, Move, ScoredMove */
#ifndef SNAPMIND_BOARD_BITBOARD_H
#define SNAPMIND_BOARD_BITBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint64_t Bitboard;

typedef enum {
    PT_PAWN = 0,
    PT_KNIGHT,
    PT_BISHOP,
    PT_ROOK,
    PT_QUEEN,
    PT_KING
} PieceType;

typedef enum {
    COLOR_WHITE = 0,
    COLOR_BLACK = 1
} Color;

/* Stable integer index 0-5 (Pawn=0 ... King=5). */
static inline int piece_type_index(PieceType p) { return (int)p; }

static inline Color color_flip(Color c) { return c == COLOR_WHITE ? COLOR_BLACK : COLOR_WHITE; }
static inline int   color_index(Color c) { return (int)c; }

/* ── Bit intrinsics (trailing_zeros / count_ones) ───────────────────────── */
#if defined(__GNUC__) || defined(__clang__)
static inline int ctz64(uint64_t x)      { return __builtin_ctzll(x); }
static inline int popcount64(uint64_t x) { return __builtin_popcountll(x); }
#elif defined(_MSC_VER)
#include <intrin.h>
static inline int ctz64(uint64_t x)      { unsigned long i; _BitScanForward64(&i, x); return (int)i; }
static inline int popcount64(uint64_t x) { return (int)__popcnt64(x); }
#else
static inline int ctz64(uint64_t x) {
    int n = 0;
    while ((x & 1) == 0) { x >>= 1; n++; }
    return n;
}
static inline int popcount64(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (int)((x * 0x0101010101010101ULL) >> 56);
}
#endif

/* Move encoding (u32)
 *   bits  0- 5 : from square (0-63)
 *   bits  6-11 : to square   (0-63)
 *   bits 12-14 : promotion   0=none 1=N 2=B 3=R 4=Q
 *   bits 15-16 : flags       0=quiet 1=capture 2=castle 3=en-passant
 *
 * The all-zero move is the "null move" (Move::default() in the Rust version).
 * MOVE_NONE plays the role of Option<Move>::None: it can never be a real move
 * because real moves only use the low 17 bits.
 */
typedef uint32_t Move;
#define MOVE_NULL 0u
#define MOVE_NONE 0xFFFFFFFFu

/* Move flag constants — stored in bits 15-16 of the Move encoding. */
#define FLAG_QUIET      0
#define FLAG_CAPTURE    1
#define FLAG_CASTLE     2
#define FLAG_EN_PASSANT 3

static inline Move move_new(int from, int to, int flags, int promo) {
    return (Move)from
         | ((Move)to    << 6)
         | ((Move)promo << 12)
         | ((Move)flags << 15);
}

static inline int  move_from(Move m)    { return (int)(m & 0x3F); }
static inline int  move_to(Move m)      { return (int)((m >> 6) & 0x3F); }
static inline int  move_promo(Move m)   { return (int)((m >> 12) & 0x7); }
static inline int  move_flags(Move m)   { return (int)((m >> 15) & 0x3); }
static inline bool move_is_null(Move m) { return m == 0; }
static inline bool move_is_capture(Move m) { int f = move_flags(m); return f == 1 || f == 3; }

/* Promotion piece, or -1 when the move is not a promotion (Option::None). */
static inline int move_promo_piece(Move m) {
    switch (move_promo(m)) {
        case 1: return PT_KNIGHT;
        case 2: return PT_BISHOP;
        case 3: return PT_ROOK;
        case 4: return PT_QUEEN;
        default: return -1;
    }
}
static inline bool move_is_promo(Move m) { return move_promo_piece(m) >= 0; }

/* Writes the UCI text of a move ("e2e4", "e7e8q") into out[6]. */
static inline void move_to_uci(Move m, char out[6]) {
    static const char files[] = "abcdefgh";
    int from = move_from(m);
    int to   = move_to(m);
    int n = 0;
    out[n++] = files[from % 8];
    out[n++] = (char)('1' + from / 8);
    out[n++] = files[to % 8];
    out[n++] = (char)('1' + to / 8);
    switch (move_promo(m)) {
        case 1: out[n++] = 'n'; break;
        case 2: out[n++] = 'b'; break;
        case 3: out[n++] = 'r'; break;
        case 4: out[n++] = 'q'; break;
        default: break;
    }
    out[n] = '\0';
}

typedef struct {
    Move    mv;
    int32_t score;
    /* Cached SEE value for captures; 0 for non-captures. */
    int32_t see_val;
} ScoredMove;

static inline int      file_of(int s) { return s % 8; }
static inline int      rank_of(int s) { return s / 8; }
static inline Bitboard bit(int s)     { return 1ULL << s; }

#define RANK_1 0x00000000000000FFULL
#define RANK_2 0x000000000000FF00ULL
#define RANK_3 0x0000000000FF0000ULL
#define RANK_4 0x00000000FF000000ULL
#define RANK_5 0x000000FF00000000ULL
#define RANK_6 0x0000FF0000000000ULL
#define RANK_7 0x00FF000000000000ULL
#define RANK_8 0xFF00000000000000ULL

#define FILE_A 0x0101010101010101ULL
#define FILE_B 0x0202020202020202ULL
#define FILE_G 0x4040404040404040ULL
#define FILE_H 0x8080808080808080ULL

#endif /* SNAPMIND_BOARD_BITBOARD_H */
