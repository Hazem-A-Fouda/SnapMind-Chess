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

#include <string.h>

#include "history.h"

/* Every butterfly / continuation entry is kept inside [-HISTORY_LIMIT, +HISTORY_LIMIT].
 * Kept at the historical value (1 000 000) so the ordering tiers in movepick.c
 * and the LMR divisor in lmr.c behave exactly as before; see the note on
 * rescaling at the bottom of this file. */
#define HISTORY_LIMIT 1000000

void history_init(HistoryTable *h) {
    memset(h, 0, sizeof *h);
}

/* Called once at the start of every search (alphabeta.c: searcher_search). */
void history_age(HistoryTable *h) {
    for (int c = 0; c < 2; c++)
        for (int f = 0; f < 64; f++)
            for (int t = 0; t < 64; t++)
                h->table[c][f][t] /= 2;

    for (int a = 0; a < 64; a++)
        for (int b = 0; b < 64; b++)
            for (int v = 0; v < 64; v++) {
                h->cont_hist_1[a][b][v] /= 2;
                h->cont_hist_2[a][b][v] /= 2;
            }

    for (int a = 0; a < 6; a++)
        for (int b = 0; b < 64; b++)
            for (int v = 0; v < 6; v++)
                h->capture_hist[a][b][v] /= 2;

    /* Correction history decays slowly — halve each search so old observations
     * don't completely dominate after positional shifts. */
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < CORR_HIST_SIZE; i++)
            h->pawn_corr_hist[s][i] /= 2;

    /* Killers are ply-indexed and only meaningful inside one search tree; a new
     * search starts from a different root, so last search's killers are noise.
     * (Countermoves are keyed by the previous move, not the ply, so they are kept.) */
    memset(h->killers, 0, sizeof h->killers);
}

/* ── arithmetic helpers ── */

static inline int32_t iabs32(int32_t x) { return x < 0 ? (int32_t)(0u - (uint32_t)x) : x; }

/* Standard "history gravity" update, done in 64-bit so it cannot overflow:

 * entry += bonus - entry * |bonus| / limit        (|bonus| <= limit)

 *  - The result always stays inside [-limit, +limit].
 *  - Mean-reverting: an update that opposes the current sign of the entry is
 * *amplified* (up to 2x at saturation), so a saturated entry can always be
 * unlearned.  The previous formula, delta - delta*|entry|/limit, ignored the
 * sign of the entry, so saturated entries were frozen in both directions.
 * - bonus > 0 rewards a move, bonus < 0 penalises it. */
static inline int32_t history_apply(int32_t entry, int32_t bonus, int32_t limit) {
    int64_t b = bonus;
    if (b >  limit) b =  limit;
    if (b < -limit) b = -limit;
    int64_t ab = b < 0 ? -b : b;
    int64_t e  = (int64_t)entry;
    e += b - e * ab / limit;
    if (e >  limit) e =  limit;
    if (e < -limit) e = -limit;
    return (int32_t)e;
}

static inline int32_t depth_sq(uint8_t depth) {
    return (int32_t)depth * (int32_t)depth;      /* <= 255*255 = 65 025: fits easily */
}

void history_update_killers(HistoryTable *h, Move mv, uint16_t ply) {
    size_t i = (size_t)ply;
    if (i > HISTORY_MAX_PLY - 1) i = HISTORY_MAX_PLY - 1;
    Move *k = h->killers[i];
    /* Always shift the current primary killer to slot 1 before writing the new
     * move into slot 0 (unless it is already the primary killer). */
    if (k[0] != mv) {
        k[1] = k[0];
        k[0] = mv;
    }
}

void history_update_countermove(HistoryTable *h, Move prev_mv, Move response) {
    h->countermove[move_from(prev_mv)][move_to(prev_mv)] = response;
}

/* Depth-squared bonus with butterfly (negative) history for non-cutting moves,
 * bounded to +-HISTORY_LIMIT. */
void history_update(HistoryTable *h, Move mv, Color color, uint8_t depth) {
    int32_t *entry = &h->table[color_index(color)][move_from(mv)][move_to(mv)];
    *entry = history_apply(*entry, depth_sq(depth), HISTORY_LIMIT);
}

/* Negative history bonus for all quiet moves that did NOT cause a cutoff. */
void history_penalise(HistoryTable *h, Move mv, Color color, uint8_t depth) {
    int32_t *entry = &h->table[color_index(color)][move_from(mv)][move_to(mv)];
    *entry = history_apply(*entry, -depth_sq(depth), HISTORY_LIMIT);
}

/* ── Continuation history ── */

void history_update_cont1(HistoryTable *h, Move ctx, Move mv, uint8_t depth) {
    if (ctx != MOVE_NONE) {
        int32_t *e = &h->cont_hist_1[move_to(ctx)][move_from(mv)][move_to(mv)];
        *e = history_apply(*e, depth_sq(depth), HISTORY_LIMIT);
    }
}

void history_update_cont2(HistoryTable *h, Move ctx, Move mv, uint8_t depth) {
    if (ctx != MOVE_NONE) {
        int32_t *e = &h->cont_hist_2[move_to(ctx)][move_from(mv)][move_to(mv)];
        *e = history_apply(*e, depth_sq(depth), HISTORY_LIMIT);
    }
}

void history_penalise_cont1(HistoryTable *h, Move ctx, Move mv, uint8_t depth) {
    if (ctx != MOVE_NONE) {
        int32_t *e = &h->cont_hist_1[move_to(ctx)][move_from(mv)][move_to(mv)];
        *e = history_apply(*e, -depth_sq(depth), HISTORY_LIMIT);
    }
}

void history_penalise_cont2(HistoryTable *h, Move ctx, Move mv, uint8_t depth) {
    if (ctx != MOVE_NONE) {
        int32_t *e = &h->cont_hist_2[move_to(ctx)][move_from(mv)][move_to(mv)];
        *e = history_apply(*e, -depth_sq(depth), HISTORY_LIMIT);
    }
}

/* ── Capture history ── */

/* Reward a capture that caused a beta cutoff. */
void history_update_capture(HistoryTable *h, PieceType mover, int to, PieceType captured, uint8_t depth) {
    int32_t *e = &h->capture_hist[piece_type_index(mover)][to][piece_type_index(captured)];
    *e = history_apply(*e, depth_sq(depth), CAPTURE_HIST_MAX);
}

/* Penalise a capture that was searched before the beta-cutoff move. */
void history_penalise_capture(HistoryTable *h, PieceType mover, int to, PieceType captured, uint8_t depth) {
    int32_t *e = &h->capture_hist[piece_type_index(mover)][to][piece_type_index(captured)];
    *e = history_apply(*e, -depth_sq(depth), CAPTURE_HIST_MAX);
}

/* ── Pawn correction history ──
 * Exponential moving average of (search_score - raw_static_eval), keyed by
 * pawn Zobrist hash.  Applied as a small corrective offset to static eval
 * before pruning decisions.

 * raw_eval — static eval *before* correction
 * score    — best score found by the search
 * depth    — remaining depth at this node (controls the learning rate)
 * bound    — only update when the score is meaningful relative to eval
 * (Exact always; Lower only if score > raw_eval; Upper only if
 * score < raw_eval) */
void history_update_correction(HistoryTable *h, int side, uint64_t pawn_hash,
                               int raw_eval, int score, uint8_t depth, Bound bound) {
    /* Skip mate-range scores. */
    if (iabs32(score) >= CORR_MATE_THRESHOLD) return;

    int meaningful;
    switch (bound) {
        case BOUND_EXACT: meaningful = 1; break;
        case BOUND_LOWER: meaningful = score > raw_eval; break;
        default:          meaningful = score < raw_eval; break;   /* BOUND_UPPER */
    }
    if (!meaningful) return;

    size_t idx = (size_t)pawn_hash & (CORR_HIST_SIZE - 1);
    int64_t diff = (int64_t)score - (int64_t)raw_eval;
    /* Learning rate: higher depth → more weight on the new observation. */
    int w = (int)depth + 1;
    if (w > 16) w = 16;
    int32_t *e = &h->pawn_corr_hist[side][idx];
    /* Exponential moving average stored scaled by CORR_SCALE (64-bit: no overflow
     * regardless of how large |score - raw_eval| gets). */
    int64_t v = ((int64_t)*e * (CORR_SCALE - w) + diff * CORR_SCALE * w) / CORR_SCALE;
    if (v < -CORR_MAX) v = -CORR_MAX;
    if (v >  CORR_MAX) v =  CORR_MAX;
    *e = (int32_t)v;
}