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

#include "see.h"
#include "../board/movegen.h"
#include "../eval/eval.h"

static inline uint64_t pawn_attacks_toward(int sq, Color color) {
    uint64_t b = bit(sq);
    if (color == COLOR_WHITE) return ((b >> 9) & ~FILE_H) | ((b >> 7) & ~FILE_A);
    return ((b << 7) & ~FILE_H) | ((b << 9) & ~FILE_A);
}

int val_on(const Board *board, int sq) {
    uint64_t b = bit(sq);
    for (int c = 0; c < 2; c++) {
        Color col = (Color)c;
        if ((board_bb(board, col, PT_PAWN)   & b) != 0) return PAWN_VAL;
        if ((board_bb(board, col, PT_KNIGHT) & b) != 0) return KNIGHT_VAL;
        if ((board_bb(board, col, PT_BISHOP) & b) != 0) return BISHOP_VAL;
        if ((board_bb(board, col, PT_ROOK)   & b) != 0) return ROOK_VAL;
        if ((board_bb(board, col, PT_QUEEN)  & b) != 0) return QUEEN_VAL;
        if ((board_bb(board, col, PT_KING)   & b) != 0) return KING_VAL;
    }
    return 0;
}

bool lva(const Board *board, int sq, Color color, uint64_t occ, uint64_t pinned,
         int *att_sq, int *att_val) {
    uint64_t atk = pawn_attacks_toward(sq, color) & board_bb(board, color, PT_PAWN) & occ;
    if (atk != 0) {
        uint64_t unpinned = atk & ~pinned;
        if (unpinned != 0) { *att_sq = ctz64(unpinned); *att_val = PAWN_VAL; return true; }
        /* All pawn attackers are pinned — fall through to next piece type. */
    }

    atk = knight_attacks(sq) & board_bb(board, color, PT_KNIGHT) & occ;
    if (atk != 0) {
        uint64_t unpinned = atk & ~pinned;
        if (unpinned != 0) { *att_sq = ctz64(unpinned); *att_val = KNIGHT_VAL; return true; }
    }

    atk = bishop_attacks(sq, occ) & board_bb(board, color, PT_BISHOP) & occ;
    if (atk != 0) {
        uint64_t unpinned = atk & ~pinned;
        if (unpinned != 0) { *att_sq = ctz64(unpinned); *att_val = BISHOP_VAL; return true; }
    }

    atk = rook_attacks(sq, occ) & board_bb(board, color, PT_ROOK) & occ;
    if (atk != 0) {
        uint64_t unpinned = atk & ~pinned;
        if (unpinned != 0) { *att_sq = ctz64(unpinned); *att_val = ROOK_VAL; return true; }
    }

    atk = (bishop_attacks(sq, occ) | rook_attacks(sq, occ)) & board_bb(board, color, PT_QUEEN) & occ;
    if (atk != 0) {
        uint64_t unpinned = atk & ~pinned;
        if (unpinned != 0) { *att_sq = ctz64(unpinned); *att_val = QUEEN_VAL; return true; }
    }

    /* King can always recapture (it cannot be pinned). */
    atk = king_attacks(sq) & board_bb(board, color, PT_KING) & occ;
    if (atk != 0) { *att_sq = ctz64(atk); *att_val = KING_VAL; return true; }

    return false;
}

static inline int imax(int a, int b) { return a > b ? a : b; }

int see(const Board *board, int from, int to) {
    /* En passant: the target square `to` is empty — the captured pawn sits one
     * rank behind.  Detect en passant and seed gain[0] with PAWN_VAL. */
    int is_ep = board->en_passant == to
             && val_on(board, from) == PAWN_VAL
             && val_on(board, to)   == 0;
    int gain0 = is_ep ? PAWN_VAL : val_on(board, to);

    /* Remove the moving piece from occupancy, then also remove the en-passant
     * captured pawn if this is an EP capture. */
    uint64_t occ = board_occupied(board) ^ bit(from);
    if (is_ep) {
        int ep_cap_sq = (board->side_to_move == COLOR_WHITE) ? to - 8 : to + 8;
        occ ^= bit(ep_cap_sq);
    }
    Color stm = color_flip(board->side_to_move);

    /* If a pawn on the 7th/2nd rank is making this capture it promotes on `to`;
     * use queen value so the opponent's recapture risk is correctly modelled. */
    int is_promo = 0;
    if (val_on(board, from) == PAWN_VAL) {
        int r = rank_of(from);
        is_promo = (board->side_to_move == COLOR_WHITE && r == 6)
                || (board->side_to_move == COLOR_BLACK && r == 1);
    }
    int at_risk = is_promo ? QUEEN_VAL : val_on(board, from);

    /* Pin masks are recomputed on each iteration because pieces leave the board
     * as the exchange progresses.  Track both kings' squares. */
    int white_king_sq, black_king_sq;
    {
        uint64_t bb = board_bb(board, COLOR_WHITE, PT_KING);
        white_king_sq = (bb == 0) ? 64 : ctz64(bb);
    }
    {
        uint64_t bb = board_bb(board, COLOR_BLACK, PT_KING);
        black_king_sq = (bb == 0) ? 64 : ctz64(bb);
    }

    int gain[32];
    for (int i = 0; i < 32; i++) gain[i] = 0;
    gain[0] = gain0;
    int d = 0;

    for (;;) {
        /* Recompute pin mask for the side to move using current `occ`. */
        int king_sq = (stm == COLOR_WHITE) ? white_king_sq : black_king_sq;
        uint64_t pinned = 0;
        if (king_sq < 64) {
            Color opp = color_flip(stm);
            /* Intersect with current `occ` so pieces already removed from the
             * exchange are not counted as active pinners / pin candidates. */
            uint64_t diag_sliders = (board_bb(board, opp, PT_BISHOP) | board_bb(board, opp, PT_QUEEN)) & occ;
            uint64_t orth_sliders = (board_bb(board, opp, PT_ROOK)   | board_bb(board, opp, PT_QUEEN)) & occ;
            uint64_t mine = board_occupied_by(board, stm) & occ;
            /* Diagonal pins */
            if (diag_sliders != 0) {
                uint64_t candidates = bishop_attacks(king_sq, occ) & mine;
                uint64_t bb = candidates;
                while (bb != 0) {
                    int sq = ctz64(bb);
                    if ((bishop_attacks(king_sq, occ ^ bit(sq)) & diag_sliders) != 0) {
                        pinned |= bit(sq);
                    }
                    bb &= bb - 1;
                }
            }
            /* Orthogonal pins */
            if (orth_sliders != 0) {
                uint64_t candidates = rook_attacks(king_sq, occ) & mine;
                uint64_t bb = candidates;
                while (bb != 0) {
                    int sq = ctz64(bb);
                    if ((rook_attacks(king_sq, occ ^ bit(sq)) & orth_sliders) != 0) {
                        pinned |= bit(sq);
                    }
                    bb &= bb - 1;
                }
            }
        }

        int att_sq, att_val;
        if (!lva(board, to, stm, occ, pinned, &att_sq, &att_val)) break;

        d += 1;
        gain[d] = at_risk - gain[d - 1];

        if (imax(gain[d], -gain[d - 1]) < 0) break;

        occ    &= ~bit(att_sq);
        at_risk = att_val;
        stm     = color_flip(stm);

        if (d >= 30) break;
    }

    while (d > 0) {
        d -= 1;
        gain[d] = imax(gain[d], -gain[d + 1]);
    }

    return gain[0];
}
