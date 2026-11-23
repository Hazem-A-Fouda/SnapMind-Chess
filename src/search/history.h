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

/* search/history.h — history heuristics: butterfly history, killers,
 * countermoves, continuation history, capture history, pawn correction. */
#ifndef SNAPMIND_SEARCH_HISTORY_H
#define SNAPMIND_SEARCH_HISTORY_H

#include <stdint.h>
#include <stdbool.h>
#include "../board/bitboard.h"
#include "tt.h"

#define HISTORY_MAX_PLY 256

/* Correction history constants: the pawn correction history tracks the
 * divergence between raw static eval and the search score, keyed by the pawn
 * Zobrist hash.  Values are stored scaled by CORR_SCALE for integer precision;
 * the maximum correction applied is +-(CORR_MAX / CORR_SCALE) = +-32 cp. */
#define CORR_HIST_SIZE (1 << 14)   /* 16 384 entries per side */
#define CORR_SCALE     256
#define CORR_MAX       (CORR_SCALE * 32)

/* Skip correction updates when the score is in mate range. */
#define CORR_MATE_THRESHOLD 90000

#define CAPTURE_HIST_MAX 1000000

/* ~2.1 MB in total — keep instances in static storage / on the heap, never on
 * the stack. */
typedef struct {
    int32_t table[2][64][64];                 /* history[color][from][to]            */
    Move    killers[HISTORY_MAX_PLY][2];      /* killers[ply][0..2]                  */
    Move    countermove[64][64];              /* countermove[from][to]               */
    int32_t cont_hist_1[64][64][64];          /* [prev_to][cur_from][cur_to]         */
    int32_t cont_hist_2[64][64][64];          /* [prev_to][cur_from][cur_to]         */
    int32_t capture_hist[6][64][6];           /* [moving_piece][to_sq][captured]     */
    int32_t pawn_corr_hist[2][CORR_HIST_SIZE];/* [side][pawn_hash & (SIZE-1)]        */
} HistoryTable;

/* Resets every table to zero (HistoryTable::new()). */
void history_init(HistoryTable *h);

/* Halve all scores so stale bonuses decay without being discarded. */
void history_age(HistoryTable *h);

void history_update_killers(HistoryTable *h, Move mv, uint16_t ply);
void history_update_countermove(HistoryTable *h, Move prev_mv, Move response);

void history_update(HistoryTable *h, Move mv, Color color, uint8_t depth);
void history_penalise(HistoryTable *h, Move mv, Color color, uint8_t depth);

void history_update_cont1(HistoryTable *h, Move ctx, Move mv, uint8_t depth);
void history_update_cont2(HistoryTable *h, Move ctx, Move mv, uint8_t depth);
void history_penalise_cont1(HistoryTable *h, Move ctx, Move mv, uint8_t depth);
void history_penalise_cont2(HistoryTable *h, Move ctx, Move mv, uint8_t depth);

void history_update_capture(HistoryTable *h, PieceType mover, int to, PieceType captured, uint8_t depth);
void history_penalise_capture(HistoryTable *h, PieceType mover, int to, PieceType captured, uint8_t depth);

/* Update the correction history entry after a completed search at this node. */
void history_update_correction(HistoryTable *h, int side, uint64_t pawn_hash,
                               int raw_eval, int score, uint8_t depth, Bound bound);

/* ── inline getters ── */

static inline Move history_get_countermove(const HistoryTable *h, Move prev_mv) {
    return h->countermove[move_from(prev_mv)][move_to(prev_mv)];
}

static inline int history_get(const HistoryTable *h, Move mv, Color color) {
    return h->table[color_index(color)][move_from(mv)][move_to(mv)];
}

static inline const Move *history_killers_at(const HistoryTable *h, uint16_t ply) {
    size_t i = (size_t)ply;
    if (i > HISTORY_MAX_PLY - 1) i = HISTORY_MAX_PLY - 1;
    return h->killers[i];
}

/* Continuation history: indexed by [prev_move.to_sq()][mv.from_sq()][mv.to_sq()].
 * ctx == MOVE_NONE (e.g. root, or after a null move) → score 0, no update. */
static inline int history_get_cont1(const HistoryTable *h, Move ctx, Move mv) {
    if (ctx != MOVE_NONE) return h->cont_hist_1[move_to(ctx)][move_from(mv)][move_to(mv)];
    return 0;
}
static inline int history_get_cont2(const HistoryTable *h, Move ctx, Move mv) {
    if (ctx != MOVE_NONE) return h->cont_hist_2[move_to(ctx)][move_from(mv)][move_to(mv)];
    return 0;
}

static inline int history_get_capture_hist(const HistoryTable *h, PieceType mover, int to, PieceType captured) {
    return h->capture_hist[piece_type_index(mover)][to][piece_type_index(captured)];
}

/* Apply the stored correction to a raw static eval. */
static inline int history_correct_eval(const HistoryTable *h, int side, uint64_t pawn_hash, int raw_eval) {
    size_t idx = (size_t)pawn_hash & (CORR_HIST_SIZE - 1);
    return raw_eval + h->pawn_corr_hist[side][idx] / CORR_SCALE;
}

#endif /* SNAPMIND_SEARCH_HISTORY_H */
