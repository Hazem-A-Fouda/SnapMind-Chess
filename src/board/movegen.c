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

#include "movegen.h"

/* ── Public API ─────────────────────────────────────────────────────────────
 *
 * pinned_pieces: pieces of `color` that are absolutely pinned to their king —
 * moving them off the pin ray would expose the king to an enemy slider attack.
 *
 * Algorithm (per ray type):
 *   1. Cast a ray from the king with the full-occupancy attack function.
 *   2. The first friendly piece on that ray is a *candidate* pinned piece.
 *   3. Remove it from occupancy and re-cast.  If an enemy slider of the
 *      matching type now reaches the king, the candidate is truly pinned.
 */
Bitboard pinned_pieces(const Board *board, Color color) {
    Bitboard king_bb = board_bb(board, color, PT_KING);
    if (king_bb == 0) return 0;
    int king_sq = ctz64(king_bb);
    Bitboard occ  = board_occupied(board);
    Bitboard mine = board_occupied_by(board, color);
    Color opp = color_flip(color);
    Bitboard pinned = 0;

    /* Diagonal pins: enemy bishops and queens */
    Bitboard diag_sliders = board_bb(board, opp, PT_BISHOP) | board_bb(board, opp, PT_QUEEN);
    if (diag_sliders != 0) {
        Bitboard candidates = bishop_attacks(king_sq, occ) & mine;
        Bitboard bb = candidates;
        while (bb != 0) {
            int sq = ctz64(bb);
            if ((bishop_attacks(king_sq, occ ^ bit(sq)) & diag_sliders) != 0) {
                pinned |= bit(sq);
            }
            bb &= bb - 1;
        }
    }

    /* Orthogonal pins: enemy rooks and queens */
    Bitboard orth_sliders = board_bb(board, opp, PT_ROOK) | board_bb(board, opp, PT_QUEEN);
    if (orth_sliders != 0) {
        Bitboard candidates = rook_attacks(king_sq, occ) & mine;
        Bitboard bb = candidates;
        while (bb != 0) {
            int sq = ctz64(bb);
            if ((rook_attacks(king_sq, occ ^ bit(sq)) & orth_sliders) != 0) {
                pinned |= bit(sq);
            }
            bb &= bb - 1;
        }
    }

    return pinned;
}

/* ── is_in_check / square_attacked_by ──────────────────────────────────── */

/* Squares attacked by a pawn of `color` standing on `sq`. */
static inline Bitboard pawn_attacks_from(int sq, Color color) {
    Bitboard b = bit(sq);
    if (color == COLOR_WHITE) return ((b << 9) & ~FILE_A) | ((b << 7) & ~FILE_H);
    return ((b >> 7) & ~FILE_A) | ((b >> 9) & ~FILE_H);
}

bool square_attacked_by(const Board *board, int sq, Color attacker) {
    Bitboard occ = board_occupied(board);

    Bitboard pawn_atk = (attacker == COLOR_WHITE)
        ? pawn_attacks_from(sq, COLOR_BLACK)
        : pawn_attacks_from(sq, COLOR_WHITE);
    if ((pawn_atk & board_bb(board, attacker, PT_PAWN)) != 0) return true;

    if ((knight_attacks(sq) & board_bb(board, attacker, PT_KNIGHT)) != 0) return true;

    Bitboard diag = bishop_attacks(sq, occ);
    if ((diag & (board_bb(board, attacker, PT_BISHOP) | board_bb(board, attacker, PT_QUEEN))) != 0) {
        return true;
    }

    Bitboard ortho = rook_attacks(sq, occ);
    if ((ortho & (board_bb(board, attacker, PT_ROOK) | board_bb(board, attacker, PT_QUEEN))) != 0) {
        return true;
    }

    if ((king_attacks(sq) & board_bb(board, attacker, PT_KING)) != 0) return true;

    return false;
}

bool is_in_check(const Board *board, Color color) {
    Bitboard king_bb = board_bb(board, color, PT_KING);
    if (king_bb == 0) return false;
    int king_sq = ctz64(king_bb);
    return square_attacked_by(board, king_sq, color_flip(color));
}

/* ── Legality filter ──
 *
 * Fast legality check — avoids a full make/unmake for the common case
 * (non-king, non-en-passant moves).  We simulate the new occupancy and recheck
 * whether the king is attacked, skipping all Zobrist/piece-array updates.
 *
 * King moves and en passant still use the safe slow path:
 *  - King moves need the king removed from occupancy before checking whether
 *    the destination is attacked by a sliding piece.
 *  - En passant removes a pawn from a square other than `to`.
 */
static bool is_legal(Board *board, Move mv) {
    Color stm = board->side_to_move;
    Bitboard king_bb = board_bb(board, stm, PT_KING);
    if (king_bb == 0) return true;
    int king_sq = ctz64(king_bb);
    int from = move_from(mv);
    int to   = move_to(mv);

    /* Slow path: king moves (castling included) and en passant */
    if (from == king_sq || move_flags(mv) == FLAG_EN_PASSANT) {
        UndoInfo undo = board_make_move(board, mv);
        bool in_check = is_in_check(board, stm);
        board_unmake_move(board, mv, undo);
        return !in_check;
    }

    /* Fast path: simulate updated occupancy and recheck king safety. */
    Bitboard occ_after = (board_occupied(board) ^ bit(from)) | bit(to);
    Color opp = color_flip(stm);
    /* Mask out the captured piece at `to` from opponent bitboards. */
    Bitboard excl_captured = ~bit(to);

    Bitboard diag = bishop_attacks(king_sq, occ_after);
    if ((diag & (board_bb(board, opp, PT_BISHOP) | board_bb(board, opp, PT_QUEEN)) & excl_captured) != 0) {
        return false;
    }

    Bitboard orth = rook_attacks(king_sq, occ_after);
    if ((orth & (board_bb(board, opp, PT_ROOK) | board_bb(board, opp, PT_QUEEN)) & excl_captured) != 0) {
        return false;
    }

    if ((knight_attacks(king_sq) & board_bb(board, opp, PT_KNIGHT) & excl_captured) != 0) {
        return false;
    }
    Bitboard pawn_vision = pawn_attacks_from(king_sq, stm);
    if ((pawn_vision & board_bb(board, opp, PT_PAWN) & excl_captured) != 0) {
        return false;
    }
    if ((king_attacks(king_sq) & board_bb(board, opp, PT_KING)) != 0) {
        return false;
    }

    return true;
}

/* ── Pseudo-legal generation helpers ───────────────────────────────────── */

static void push_pawn(int from, int to, int flag, MoveList *moves) {
    if (rank_of(to) == 7 || rank_of(to) == 0) {
        for (int promo = 1; promo <= 4; promo++) ml_push(moves, move_new(from, to, flag, promo));
    } else {
        ml_push(moves, move_new(from, to, flag, 0));
    }
}

static void extract_pawn_moves(Bitboard targets, int delta, int flag, MoveList *moves) {
    while (targets != 0) {
        int to   = ctz64(targets);
        int from = to - delta;
        push_pawn(from, to, flag, moves);
        targets &= targets - 1;
    }
}

static void extract_pawn_moves_neg(Bitboard targets, int delta, int flag, MoveList *moves) {
    while (targets != 0) {
        int to   = ctz64(targets);
        int from = to + delta;
        push_pawn(from, to, flag, moves);
        targets &= targets - 1;
    }
}

static void extract_moves(int from, Bitboard targets, Bitboard opp, MoveList *moves) {
    while (targets != 0) {
        int to   = ctz64(targets);
        int flag = ((bit(to) & opp) != 0) ? 1 : 0;
        ml_push(moves, move_new(from, to, flag, 0));
        targets &= targets - 1;
    }
}

/* ── Captures-only pseudo-legal generation (for quiescence search) ─────── */

static void gen_pawn_captures(const Board *board, Color color, Bitboard occ, Bitboard opp, MoveList *moves) {
    Bitboard pawns = board_bb(board, color, PT_PAWN);
    if (color == COLOR_WHITE) {
        Bitboard cap_r      = (pawns << 9) & ~FILE_A & opp;
        Bitboard cap_l      = (pawns << 7) & ~FILE_H & opp;
        Bitboard promo_push = (pawns << 8) & ~occ & RANK_8;   /* non-capture promotions */
        extract_pawn_moves(cap_r,      9, 1, moves);
        extract_pawn_moves(cap_l,      7, 1, moves);
        extract_pawn_moves(promo_push, 8, 0, moves);
        if (board->en_passant >= 0) {
            int ep = board->en_passant;
            Bitboard ep_bit = bit(ep);
            if (((pawns << 9) & ~FILE_A & ep_bit) != 0) ml_push(moves, move_new(ep - 9, ep, 3, 0));
            if (((pawns << 7) & ~FILE_H & ep_bit) != 0) ml_push(moves, move_new(ep - 7, ep, 3, 0));
        }
    } else {
        Bitboard cap_r      = (pawns >> 7) & ~FILE_A & opp;
        Bitboard cap_l      = (pawns >> 9) & ~FILE_H & opp;
        Bitboard promo_push = (pawns >> 8) & ~occ & RANK_1;   /* non-capture promotions */
        extract_pawn_moves_neg(cap_r,      7, 1, moves);
        extract_pawn_moves_neg(cap_l,      9, 1, moves);
        extract_pawn_moves_neg(promo_push, 8, 0, moves);
        if (board->en_passant >= 0) {
            int ep = board->en_passant;
            Bitboard ep_bit = bit(ep);
            if (((pawns >> 7) & ~FILE_A & ep_bit) != 0) ml_push(moves, move_new(ep + 7, ep, 3, 0));
            if (((pawns >> 9) & ~FILE_H & ep_bit) != 0) ml_push(moves, move_new(ep + 9, ep, 3, 0));
        }
    }
}

static void gen_piece_captures(const Board *board, Color color, Bitboard occ, Bitboard opp, MoveList *moves) {
    Bitboard knights = board_bb(board, color, PT_KNIGHT);
    while (knights != 0) {
        int from = ctz64(knights);
        extract_moves(from, knight_attacks(from) & opp, opp, moves);
        knights &= knights - 1;
    }
    Bitboard bishops = board_bb(board, color, PT_BISHOP);
    while (bishops != 0) {
        int from = ctz64(bishops);
        extract_moves(from, bishop_attacks(from, occ) & opp, opp, moves);
        bishops &= bishops - 1;
    }
    Bitboard rooks = board_bb(board, color, PT_ROOK);
    while (rooks != 0) {
        int from = ctz64(rooks);
        extract_moves(from, rook_attacks(from, occ) & opp, opp, moves);
        rooks &= rooks - 1;
    }
    Bitboard queens = board_bb(board, color, PT_QUEEN);
    while (queens != 0) {
        int from = ctz64(queens);
        Bitboard atk = (bishop_attacks(from, occ) | rook_attacks(from, occ)) & opp;
        extract_moves(from, atk, opp, moves);
        queens &= queens - 1;
    }
    Bitboard king_bb = board_bb(board, color, PT_KING);
    if (king_bb != 0) {
        int from = ctz64(king_bb);
        extract_moves(from, king_attacks(from) & opp, opp, moves);
    }
}

static void generate_pseudo_captures(const Board *board, MoveList *moves) {
    Color stm = board->side_to_move;
    Bitboard occ  = board_occupied(board);
    Bitboard mine = board_occupied_by(board, stm);
    Bitboard opp  = occ & ~mine;

    gen_pawn_captures(board, stm, occ, opp, moves);
    gen_piece_captures(board, stm, occ, opp, moves);
}

/* ── Full pseudo-legal piece generators ────────────────────────────────── */

static void gen_pawns(const Board *board, Color color, Bitboard occ, Bitboard opp, MoveList *moves) {
    Bitboard pawns = board_bb(board, color, PT_PAWN);
    if (color == COLOR_WHITE) {
        Bitboard push1 = (pawns << 8) & ~occ;
        Bitboard push2 = ((push1 & RANK_3) << 8) & ~occ;
        Bitboard cap_r = (pawns << 9) & ~FILE_A & opp;
        Bitboard cap_l = (pawns << 7) & ~FILE_H & opp;
        extract_pawn_moves(push1, 8,  0, moves);
        extract_pawn_moves(push2, 16, 0, moves);
        extract_pawn_moves(cap_r, 9,  1, moves);
        extract_pawn_moves(cap_l, 7,  1, moves);
        if (board->en_passant >= 0) {
            int ep = board->en_passant;
            Bitboard ep_bit = bit(ep);
            if (((pawns << 9) & ~FILE_A & ep_bit) != 0) ml_push(moves, move_new(ep - 9, ep, 3, 0));
            if (((pawns << 7) & ~FILE_H & ep_bit) != 0) ml_push(moves, move_new(ep - 7, ep, 3, 0));
        }
    } else {
        Bitboard push1 = (pawns >> 8) & ~occ;
        Bitboard push2 = ((push1 & RANK_6) >> 8) & ~occ;
        Bitboard cap_r = (pawns >> 7) & ~FILE_A & opp;
        Bitboard cap_l = (pawns >> 9) & ~FILE_H & opp;
        extract_pawn_moves_neg(push1, 8,  0, moves);
        extract_pawn_moves_neg(push2, 16, 0, moves);
        extract_pawn_moves_neg(cap_r, 7,  1, moves);
        extract_pawn_moves_neg(cap_l, 9,  1, moves);
        if (board->en_passant >= 0) {
            int ep = board->en_passant;
            Bitboard ep_bit = bit(ep);
            if (((pawns >> 7) & ~FILE_A & ep_bit) != 0) ml_push(moves, move_new(ep + 7, ep, 3, 0));
            if (((pawns >> 9) & ~FILE_H & ep_bit) != 0) ml_push(moves, move_new(ep + 9, ep, 3, 0));
        }
    }
}

static void gen_knights(const Board *board, Color color, Bitboard mine, Bitboard opp, MoveList *moves) {
    Bitboard bb = board_bb(board, color, PT_KNIGHT);
    while (bb != 0) {
        int from = ctz64(bb);
        extract_moves(from, knight_attacks(from) & ~mine, opp, moves);
        bb &= bb - 1;
    }
}

static void gen_bishops(const Board *board, Color color, Bitboard occ, Bitboard mine, Bitboard opp, MoveList *moves) {
    Bitboard bb = board_bb(board, color, PT_BISHOP);
    while (bb != 0) {
        int from = ctz64(bb);
        extract_moves(from, bishop_attacks(from, occ) & ~mine, opp, moves);
        bb &= bb - 1;
    }
}

static void gen_rooks(const Board *board, Color color, Bitboard occ, Bitboard mine, Bitboard opp, MoveList *moves) {
    Bitboard bb = board_bb(board, color, PT_ROOK);
    while (bb != 0) {
        int from = ctz64(bb);
        extract_moves(from, rook_attacks(from, occ) & ~mine, opp, moves);
        bb &= bb - 1;
    }
}

static void gen_queens(const Board *board, Color color, Bitboard occ, Bitboard mine, Bitboard opp, MoveList *moves) {
    Bitboard bb = board_bb(board, color, PT_QUEEN);
    while (bb != 0) {
        int from = ctz64(bb);
        Bitboard atk = (bishop_attacks(from, occ) | rook_attacks(from, occ)) & ~mine;
        extract_moves(from, atk, opp, moves);
        bb &= bb - 1;
    }
}

static void gen_king(const Board *board, Color color, Bitboard occ, Bitboard mine, Bitboard opp, MoveList *moves) {
    Bitboard king_bb = board_bb(board, color, PT_KING);
    if (king_bb == 0) return;
    int from = ctz64(king_bb);
    extract_moves(from, king_attacks(from) & ~mine, opp, moves);

    Color opp_color = color_flip(color);
    if (color == COLOR_WHITE) {
        if ((board->castling_rights & 0x8) != 0
            && (occ & 0x0000000000000060ULL) == 0
            && !square_attacked_by(board, 4, opp_color)
            && !square_attacked_by(board, 5, opp_color)
            && !square_attacked_by(board, 6, opp_color))
        {
            ml_push(moves, move_new(4, 6, 2, 0));
        }
        if ((board->castling_rights & 0x4) != 0
            && (occ & 0x000000000000000EULL) == 0
            && !square_attacked_by(board, 4, opp_color)
            && !square_attacked_by(board, 3, opp_color)
            && !square_attacked_by(board, 2, opp_color))
        {
            ml_push(moves, move_new(4, 2, 2, 0));
        }
    } else {
        if ((board->castling_rights & 0x2) != 0
            && (occ & 0x6000000000000000ULL) == 0
            && !square_attacked_by(board, 60, opp_color)
            && !square_attacked_by(board, 61, opp_color)
            && !square_attacked_by(board, 62, opp_color))
        {
            ml_push(moves, move_new(60, 62, 2, 0));
        }
        if ((board->castling_rights & 0x1) != 0
            && (occ & 0x0E00000000000000ULL) == 0
            && !square_attacked_by(board, 60, opp_color)
            && !square_attacked_by(board, 59, opp_color)
            && !square_attacked_by(board, 58, opp_color))
        {
            ml_push(moves, move_new(60, 58, 2, 0));
        }
    }
}

static void generate_pseudo_legal(const Board *board, MoveList *moves) {
    Color stm = board->side_to_move;
    Bitboard occ  = board_occupied(board);
    Bitboard mine = board_occupied_by(board, stm);
    Bitboard opp  = occ & ~mine;

    gen_pawns(board,   stm, occ, opp, moves);
    gen_knights(board, stm, mine, opp, moves);
    gen_bishops(board, stm, occ, mine, opp, moves);
    gen_rooks(board,   stm, occ, mine, opp, moves);
    gen_queens(board,  stm, occ, mine, opp, moves);
    gen_king(board,    stm, occ, mine, opp, moves);
}

/* ── Public generators: pseudo-legal + in-place legality filter ────────── */

static void retain_legal(Board *board, MoveList *moves) {
    int write = 0;
    for (int read = 0; read < moves->len; read++) {
        Move mv = moves->moves[read];
        if (is_legal(board, mv)) {
            moves->moves[write] = mv;
            write++;
        }
    }
    moves->len = write;
}

void generate_legal_moves(Board *board, MoveList *out) {
    ml_init(out);
    generate_pseudo_legal(board, out);
    retain_legal(board, out);
}

void generate_legal_captures(Board *board, MoveList *out) {
    ml_init(out);
    generate_pseudo_captures(board, out);
    retain_legal(board, out);
}
