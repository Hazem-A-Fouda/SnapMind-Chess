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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "zobrist.h"

/* ── small helpers ───────────────────────────────────────────────────────── */

static inline uint8_t mb_encode(Color color, PieceType piece) {
    return (uint8_t)(color_offset(color) + piece_offset(piece));
}

static inline void occ_set(Board *b, Color color, int sq) {
    Bitboard m = bit(sq);
    if (color == COLOR_WHITE) b->occ_white |= m; else b->occ_black |= m;
    b->occ_all |= m;
}

static inline void occ_clear(Board *b, Color color, int sq) {
    Bitboard m = bit(sq);
    if (color == COLOR_WHITE) b->occ_white &= ~m; else b->occ_black &= ~m;
    if ((b->occ_white & m) == 0 && (b->occ_black & m) == 0) {
        b->occ_all &= ~m;
    }
}

static void recompute_occ(Board *b) {
    Bitboard w = 0, k = 0;
    for (int i = 0; i < 6; i++)  w |= b->pieces[i];
    for (int i = 6; i < 12; i++) k |= b->pieces[i];
    b->occ_white = w;
    b->occ_black = k;
    b->occ_all   = w | k;

    /* Rebuild mailbox from the authoritative bitboards. */
    memset(b->mailbox, 0xFF, sizeof b->mailbox);
    for (int c = 0; c < 2; c++) {
        Color color = (Color)c;
        for (int p = 0; p < 6; p++) {
            PieceType piece = (PieceType)p;
            Bitboard bb = board_bb(b, color, piece);
            while (bb != 0) {
                int sq = ctz64(bb);
                b->mailbox[sq] = mb_encode(color, piece);
                bb &= bb - 1;
            }
        }
    }
}

uint32_t board_phase_material(const Board *b) {
    uint32_t v = 0;
    for (int c = 0; c < 2; c++) {
        int o = color_offset((Color)c);
        v += (uint32_t)popcount64(b->pieces[o + 1]);
        v += (uint32_t)popcount64(b->pieces[o + 2]);
        v += (uint32_t)popcount64(b->pieces[o + 3]) * 2;
        v += (uint32_t)popcount64(b->pieces[o + 4]) * 4;
    }
    return v;
}

GamePhase board_game_phase(const Board *b) {
    uint32_t mat = board_phase_material(b);
    if (b->fullmove_number <= 12 && mat >= 24) return PHASE_OPENING;
    else if (mat >= 10) return PHASE_MIDDLEGAME;
    else return PHASE_ENDGAME;
}

void board_startpos(Board *out) {
    if (!board_from_fen(out, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")) {
        fprintf(stderr, "start FEN must parse\n");
        abort();
    }
}

/* ── FEN parsing ─────────────────────────────────────────────────────────── */

static bool char_to_piece(char c, Color *color, PieceType *piece) {
    *color = (c >= 'A' && c <= 'Z') ? COLOR_WHITE : COLOR_BLACK;
    char lc = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    switch (lc) {
        case 'p': *piece = PT_PAWN;   return true;
        case 'n': *piece = PT_KNIGHT; return true;
        case 'b': *piece = PT_BISHOP; return true;
        case 'r': *piece = PT_ROOK;   return true;
        case 'q': *piece = PT_QUEEN;  return true;
        case 'k': *piece = PT_KING;   return true;
        default:  return false;
    }
}

/* Same behaviour as Rust's str::parse::<u16>(): optional '+', digits only,
 * must fit in 16 bits; otherwise returns false. */
static bool parse_u16(const char *s, uint16_t *out) {
    if (*s == '+') s++;
    if (*s == '\0') return false;
    uint32_t v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        v = v * 10 + (uint32_t)(*s - '0');
        if (v > 65535) return false;
    }
    *out = (uint16_t)v;
    return true;
}

#define FEN_MAX_FIELDS 8

bool board_from_fen(Board *out, const char *fen) {
    Board board;
    memset(&board, 0, sizeof board);
    memset(board.mailbox, 0xFF, sizeof board.mailbox);
    board.side_to_move    = COLOR_WHITE;
    board.en_passant      = -1;
    board.fullmove_number = 1;

    /* Split on whitespace (copy so we can NUL-terminate the fields). */
    size_t len = strlen(fen);
    char *buf = (char *)malloc(len + 1);
    if (!buf) return false;
    memcpy(buf, fen, len + 1);

    char *parts[FEN_MAX_FIELDS];
    int n_parts = 0;
    {
        char *p = buf;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
            if (!*p) break;
            char *start = p;
            while (*p && !(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
            if (*p) *p++ = '\0';
            if (n_parts < FEN_MAX_FIELDS) parts[n_parts] = start;
            n_parts++;
        }
    }
    if (n_parts > FEN_MAX_FIELDS) n_parts = FEN_MAX_FIELDS;   /* extra fields are ignored */
    if (n_parts < 4) { free(buf); return false; }

    /* Piece placement. */
    uint8_t rank = 7;
    uint8_t file = 0;
    for (const char *ch = parts[0]; *ch; ch++) {
        char c = *ch;
        if (c == '/') {
            rank = (uint8_t)(rank - 1);
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file = (uint8_t)(file + (c - '0'));
        } else {
            Color color; PieceType piece;
            if (!char_to_piece(c, &color, &piece)) { free(buf); return false; }
            uint8_t sq = (uint8_t)(rank * 8 + file);
            if (sq >= 64) { free(buf); return false; }   /* malformed FEN */
            *board_bb_mut(&board, color, piece) |= bit(sq);
            board.hash ^= zobrist_piece_key(piece, color, sq);
            board.mailbox[sq] = mb_encode(color, piece);
            file = (uint8_t)(file + 1);
        }
    }

    recompute_occ(&board);

    /* Side to move. */
    if (strcmp(parts[1], "w") == 0) {
        board.side_to_move = COLOR_WHITE;
    } else if (strcmp(parts[1], "b") == 0) {
        board.hash ^= zobrist_side_key();
        board.side_to_move = COLOR_BLACK;
    } else {
        free(buf);
        return false;
    }

    /* Castling rights. */
    for (const char *ch = parts[2]; *ch; ch++) {
        switch (*ch) {
            case 'K': board.castling_rights |= 0x8; break;   /* 0b1000 */
            case 'Q': board.castling_rights |= 0x4; break;   /* 0b0100 */
            case 'k': board.castling_rights |= 0x2; break;   /* 0b0010 */
            case 'q': board.castling_rights |= 0x1; break;   /* 0b0001 */
            default: break;
        }
    }
    board.hash ^= zobrist_castle_key(board.castling_rights);

    /* En-passant square. */
    if (strcmp(parts[3], "-") != 0) {
        const unsigned char *b = (const unsigned char *)parts[3];
        if (b[0] != '\0' && b[1] != '\0') {
            uint8_t ep_sq = (uint8_t)((uint8_t)(b[1] - '1') * 8 + (uint8_t)(b[0] - 'a'));
            if (ep_sq < 64) {   /* ignore garbage squares */
                board.en_passant = ep_sq;
                board.hash ^= zobrist_ep_key(file_of(ep_sq));
            }
        }
    }

    uint16_t v;
    if (n_parts > 4) board.halfmove_clock  = parse_u16(parts[4], &v) ? v : 0;
    if (n_parts > 5) board.fullmove_number = parse_u16(parts[5], &v) ? v : 1;

    free(buf);
    *out = board;
    return true;
}

/* ── make / unmake ───────────────────────────────────────────────────────── */

static void update_castling_rights(uint8_t *rights, int sq) {
    switch (sq) {
        case 0:  *rights &= (uint8_t)~0x4; break;   /* ~0b0100 */
        case 7:  *rights &= (uint8_t)~0x8; break;   /* ~0b1000 */
        case 56: *rights &= (uint8_t)~0x1; break;   /* ~0b0001 */
        case 63: *rights &= (uint8_t)~0x2; break;   /* ~0b0010 */
        default: break;
    }
}

UndoInfo board_make_move(Board *b, Move mv) {
    int   from  = move_from(mv);
    int   to    = move_to(mv);
    int   flags = move_flags(mv);
    Color stm   = b->side_to_move;
    Color opp   = color_flip(stm);

    UndoInfo undo;
    undo.castling_rights = b->castling_rights;
    undo.en_passant      = b->en_passant;
    undo.halfmove_clock  = b->halfmove_clock;
    undo.fullmove_number = b->fullmove_number;
    undo.hash            = b->hash;
    undo.captured_piece  = -1;
    undo.captured_sq     = (uint8_t)to;

    b->hash ^= zobrist_castle_key(b->castling_rights);
    if (b->en_passant >= 0) b->hash ^= zobrist_ep_key(file_of(b->en_passant));

    b->en_passant = -1;
    if (b->halfmove_clock < 0xFFFF) b->halfmove_clock++;   /* saturating_add(1) */

    /* O(1) moving-piece lookup via mailbox. */
    PieceOn mp = piece_on_fast(b, from);
    if (!mp.some) {
        fprintf(stderr, "make_move: no piece on from square\n");
        abort();
    }
    PieceType moving_piece = mp.piece;

    /* Lift moving piece */
    *board_bb_mut(b, stm, moving_piece) &= ~bit(from);
    occ_clear(b, stm, from);
    b->hash ^= zobrist_piece_key(moving_piece, stm, from);
    b->mailbox[from] = 0xFF;

    /* Normal capture — O(1) lookup via mailbox. */
    if (flags == FLAG_CAPTURE) {
        PieceOn cp = piece_on_fast(b, to);
        if (cp.some) {
            *board_bb_mut(b, cp.color, cp.piece) &= ~bit(to);
            occ_clear(b, cp.color, to);
            b->hash ^= zobrist_piece_key(cp.piece, cp.color, to);
            b->mailbox[to] = 0xFF;
            b->halfmove_clock = 0;
            undo.captured_piece = (int)cp.piece;
        }
    }

    /* En-passant capture */
    if (flags == FLAG_EN_PASSANT) {
        int ep_sq = (stm == COLOR_WHITE) ? to - 8 : to + 8;
        *board_bb_mut(b, opp, PT_PAWN) &= ~bit(ep_sq);
        occ_clear(b, opp, ep_sq);
        b->hash ^= zobrist_piece_key(PT_PAWN, opp, ep_sq);
        b->mailbox[ep_sq] = 0xFF;
        b->halfmove_clock = 0;
        undo.captured_piece = (int)PT_PAWN;
        undo.captured_sq    = (uint8_t)ep_sq;
    }

    /* Place landing piece (promotion or same piece) */
    int promo = move_promo_piece(mv);
    PieceType landing = (promo >= 0) ? (PieceType)promo : moving_piece;
    *board_bb_mut(b, stm, landing) |= bit(to);
    occ_set(b, stm, to);
    b->hash ^= zobrist_piece_key(landing, stm, to);
    b->mailbox[to] = mb_encode(stm, landing);

    if (moving_piece == PT_PAWN) b->halfmove_clock = 0;

    if (moving_piece == PT_PAWN) {
        if (stm == COLOR_WHITE && rank_of(from) == 1 && rank_of(to) == 3) {
            int ep_sq = from + 8;
            b->en_passant = ep_sq;
            b->hash ^= zobrist_ep_key(file_of(ep_sq));
        } else if (stm == COLOR_BLACK && rank_of(from) == 6 && rank_of(to) == 4) {
            int ep_sq = from - 8;
            b->en_passant = ep_sq;
            b->hash ^= zobrist_ep_key(file_of(ep_sq));
        }
    }

    /* Castling: move the rook */
    if (flags == FLAG_CASTLE) {
        int rk_from, rk_to;
        if      (stm == COLOR_WHITE && to == 6)  { rk_from = 7;  rk_to = 5;  }
        else if (stm == COLOR_WHITE && to == 2)  { rk_from = 0;  rk_to = 3;  }
        else if (stm == COLOR_BLACK && to == 62) { rk_from = 63; rk_to = 61; }
        else if (stm == COLOR_BLACK && to == 58) { rk_from = 56; rk_to = 59; }
        else {
            /* Invalid castle: undo everything we did and bail out. */
            b->hash            = undo.hash;
            b->en_passant      = undo.en_passant;
            b->halfmove_clock  = undo.halfmove_clock;
            b->fullmove_number = undo.fullmove_number;
            b->castling_rights = undo.castling_rights;
            *board_bb_mut(b, stm, moving_piece) |= bit(from);
            occ_set(b, stm, from);
            b->mailbox[from] = mb_encode(stm, moving_piece);
            *board_bb_mut(b, stm, landing) &= ~bit(to);
            occ_clear(b, stm, to);
            b->mailbox[to] = 0xFF;
            b->side_to_move = stm;
            return undo;
        }
        *board_bb_mut(b, stm, PT_ROOK) &= ~bit(rk_from);
        occ_clear(b, stm, rk_from);
        b->hash ^= zobrist_piece_key(PT_ROOK, stm, rk_from);
        b->mailbox[rk_from] = 0xFF;

        *board_bb_mut(b, stm, PT_ROOK) |= bit(rk_to);
        occ_set(b, stm, rk_to);
        b->hash ^= zobrist_piece_key(PT_ROOK, stm, rk_to);
        b->mailbox[rk_to] = mb_encode(stm, PT_ROOK);
    }

    if (moving_piece == PT_KING) {
        if (stm == COLOR_WHITE) b->castling_rights &= 0x3;   /* 0b0011 */
        else                    b->castling_rights &= 0xC;   /* 0b1100 */
    }
    update_castling_rights(&b->castling_rights, from);
    update_castling_rights(&b->castling_rights, to);

    b->hash ^= zobrist_castle_key(b->castling_rights);
    b->hash ^= zobrist_side_key();
    b->side_to_move = opp;
    if (opp == COLOR_WHITE) b->fullmove_number++;

    return undo;
}

void board_unmake_move(Board *b, Move mv, UndoInfo undo) {
    int from  = move_from(mv);
    int to    = move_to(mv);
    int flags = move_flags(mv);

    Color opp   = b->side_to_move;
    Color mover = color_flip(opp);

    /* O(1) landing-piece lookup via mailbox. */
    uint8_t v = b->mailbox[to];
    PieceType landing = (PieceType)(v % 6);
    PieceType moving_piece = move_is_promo(mv) ? PT_PAWN : landing;

    /* Remove landing piece from destination, restore original piece at source. */
    *board_bb_mut(b, mover, landing) &= ~bit(to);
    occ_clear(b, mover, to);
    b->mailbox[to] = 0xFF;

    *board_bb_mut(b, mover, moving_piece) |= bit(from);
    occ_set(b, mover, from);
    b->mailbox[from] = mb_encode(mover, moving_piece);

    /* Restore captured piece. */
    if (undo.captured_piece >= 0) {
        PieceType cap = (PieceType)undo.captured_piece;
        *board_bb_mut(b, opp, cap) |= bit(undo.captured_sq);
        occ_set(b, opp, undo.captured_sq);
        b->mailbox[undo.captured_sq] = mb_encode(opp, cap);
    }

    /* Restore rook for castling. */
    if (flags == FLAG_CASTLE) {
        int rk_from = -1, rk_to = -1;
        if      (mover == COLOR_WHITE && to == 6)  { rk_from = 7;  rk_to = 5;  }
        else if (mover == COLOR_WHITE && to == 2)  { rk_from = 0;  rk_to = 3;  }
        else if (mover == COLOR_BLACK && to == 62) { rk_from = 63; rk_to = 61; }
        else if (mover == COLOR_BLACK && to == 58) { rk_from = 56; rk_to = 59; }
        if (rk_from >= 0) {
            *board_bb_mut(b, mover, PT_ROOK) &= ~bit(rk_to);
            occ_clear(b, mover, rk_to);
            b->mailbox[rk_to] = 0xFF;

            *board_bb_mut(b, mover, PT_ROOK) |= bit(rk_from);
            occ_set(b, mover, rk_from);
            b->mailbox[rk_from] = mb_encode(mover, PT_ROOK);
        }
    }

    b->side_to_move    = mover;
    b->castling_rights = undo.castling_rights;
    b->en_passant      = undo.en_passant;
    b->halfmove_clock  = undo.halfmove_clock;
    b->fullmove_number = undo.fullmove_number;
    b->hash            = undo.hash;
}

UndoInfo board_make_null_move(Board *b) {
    UndoInfo undo;
    undo.castling_rights = b->castling_rights;
    undo.en_passant      = b->en_passant;
    undo.halfmove_clock  = b->halfmove_clock;
    undo.fullmove_number = b->fullmove_number;
    undo.hash            = b->hash;
    undo.captured_piece  = -1;
    undo.captured_sq     = 0;

    if (b->en_passant >= 0) b->hash ^= zobrist_ep_key(file_of(b->en_passant));
    b->en_passant = -1;
    b->hash ^= zobrist_side_key();
    Color opp = color_flip(b->side_to_move);
    b->side_to_move = opp;
    if (b->halfmove_clock < 0xFFFF) b->halfmove_clock++;
    if (opp == COLOR_WHITE) b->fullmove_number++;
    return undo;
}

void board_unmake_null_move(Board *b, UndoInfo undo) {
    b->side_to_move    = color_flip(b->side_to_move);
    b->castling_rights = undo.castling_rights;
    b->en_passant      = undo.en_passant;
    b->halfmove_clock  = undo.halfmove_clock;
    b->fullmove_number = undo.fullmove_number;
    b->hash            = undo.hash;
}
