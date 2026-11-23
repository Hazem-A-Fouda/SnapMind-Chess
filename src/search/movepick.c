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

#include "movepick.h"
#include "see.h"

/* Returns the priority of a move and stores its SEE value in *see_out. */
static int move_priority(Move mv, const Board *board, uint16_t ply, Move tt_move,
                         const HistoryTable *history, Move prev_move, Move cont2_ctx,
                         int *see_out) {
    if (mv == tt_move) { *see_out = 0; return 1000000; }

    int score   = 0;
    int see_val = 0;

    int promo = move_promo_piece(mv);
    if (promo >= 0) {
        int promo_bonus;
        switch (promo) {
            case PT_QUEEN: promo_bonus = 90000; break;
            case PT_ROOK:  promo_bonus = 80000; break;
            default:       promo_bonus = 70000; break;
        }
        if (move_is_capture(mv)) {
            see_val = see(board, move_from(mv), move_to(mv));
            score += promo_bonus + 200 + see_val;
        } else {
            score += promo_bonus;
        }
        *see_out = see_val;
        return score;
    }

    if (move_is_capture(mv)) {
        see_val = see(board, move_from(mv), move_to(mv));
        /* Blend capture history with SEE for finer-grained capture ordering.
         * Cap the bonus at +-2000 so it nudges ordering without overriding SEE. */
        int cap_hist_bonus = 0;   /* en-passant: captured pawn not on to_sq → 0 */
        PieceOn mover    = piece_on_fast(board, move_from(mv));
        PieceOn captured = piece_on_fast(board, move_to(mv));
        if (mover.some && captured.some) {
            cap_hist_bonus = history_get_capture_hist(history, mover.piece, move_to(mv), captured.piece) / 512;
            if (cap_hist_bonus < -2000) cap_hist_bonus = -2000;
            if (cap_hist_bonus >  2000) cap_hist_bonus =  2000;
        }
        score += (see_val >= 0)
            ? 100000 + see_val + cap_hist_bonus
            : 50000  + see_val + cap_hist_bonus;
    }

    const Move *k = history_killers_at(history, ply);
    if (k[0] == mv) score += 9000;
    if (k[1] == mv) score += 8000;

    if (prev_move != MOVE_NONE) {
        if (history_get_countermove(history, prev_move) == mv) score += 7500;
    }

    /* Butterfly history + continuation history blended with decreasing weights. */
    score += history_get(history, mv, board->side_to_move);
    score += history_get_cont1(history, prev_move, mv) / 2;
    score += history_get_cont2(history, cont2_ctx,  mv) / 4;

    *see_out = see_val;
    return score;
}

void score_moves(ScoredList *out, const Move *moves, int n, const Board *board,
                 uint16_t ply, Move tt_move, const HistoryTable *history,
                 Move prev_move, Move cont2_ctx) {
    out->len = 0;
    for (int i = 0; i < n; i++) {
        int see_val;
        int score = move_priority(moves[i], board, ply, tt_move, history, prev_move, cont2_ctx, &see_val);
        ScoredMove sm;
        sm.mv      = moves[i];
        sm.score   = score;
        sm.see_val = see_val;
        out->moves[out->len++] = sm;
    }

    /* Stable insertion sort, descending by score.  Move lists are short
     * (typically 20-40), where insertion sort is as fast as anything else. */
    for (int i = 1; i < out->len; i++) {
        ScoredMove key = out->moves[i];
        int j = i - 1;
        while (j >= 0 && out->moves[j].score < key.score) {
            out->moves[j + 1] = out->moves[j];
            j--;
        }
        out->moves[j + 1] = key;
    }
}
