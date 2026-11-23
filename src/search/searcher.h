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

/* search/searcher.h — the Searcher state shared by all search routines. */
#ifndef SNAPMIND_SEARCH_SEARCHER_H
#define SNAPMIND_SEARCH_SEARCHER_H

#include <stdint.h>
#include <stddef.h>
#include "../board/bitboard.h"
#include "history.h"
#include "lmr.h"
#include "tt.h"

/* Number of per-ply slots in prev_moves / static_evals (MAX_PLY in alphabeta). */
#define PLY_ARRAY_SIZE 128

/* Growable array of 64-bit hashes (Vec<u64>). */
typedef struct {
    uint64_t *data;
    size_t    len;
    size_t    cap;
} U64Vec;

void u64vec_init(U64Vec *v, size_t capacity);
void u64vec_free(U64Vec *v);
void u64vec_push(U64Vec *v, uint64_t x);
static inline void u64vec_pop(U64Vec *v)   { if (v->len > 0) v->len--; }
static inline void u64vec_clear(U64Vec *v) { v->len = 0; }
void u64vec_clone_from(U64Vec *dst, const U64Vec *src);

typedef struct {
    TranspositionTable tt;
    OpeningCache       opening_cache;

    /* Stop flags.  `stop` is the hard limit; `soft_stop` is set when the base
     * time budget expires.  The search loop uses soft_stop to exit early when
     * the best move has been stable for several iterations; if the best move is
     * still changing it ignores soft_stop and keeps going until `stop` fires
     * (the hard limit, 1.5x the base budget).
     *
     * There is no timer thread: the search polls the clock every few hundred
     * nodes and raises the flags itself once the deadlines below have passed
     * (UINT64_MAX = no deadline). */
    int      stop;
    int      soft_stop;
    uint64_t soft_deadline_us;
    uint64_t hard_deadline_us;
    uint32_t poll_counter;

    uint64_t nodes;
    HistoryTable history;      /* ~2 MB: this struct must live in static/heap memory */
    LmrTable     lmr;
    U64Vec       rep_stack;

    /* Hashes of positions played in the current game *before* the search root.
     * Prepended to rep_stack at the start of every search so that repetitions
     * involving pre-root game positions are correctly detected. */
    U64Vec       game_history;

    /* Per-ply record of the move played to reach ply N from ply N-1, used by
     * continuation history.  MOVE_NONE = no context. */
    Move         prev_moves[PLY_ARRAY_SIZE];

    /* Remaining time on the clock in milliseconds, set by UCI before each
     * search.  Used by Adaptive IIR to disable depth reduction under time
     * pressure (< 2 s).  Defaults to a huge value so IIR fires normally when no
     * time info is available (e.g. "go infinite" or "go depth N"). */
    uint64_t     time_remaining_ms;

    /* Static evaluation recorded at each ply during the current search (used
     * by the RFP "improving" heuristic). */
    int32_t      static_evals[PLY_ARRAY_SIZE];
} Searcher;

/* Initialises *s in place (it is too big to return by value). */
void searcher_init(Searcher *s, size_t tt_mb, size_t oc_mb);

/* Polls the clock every 256 calls and raises stop / soft_stop when a deadline
 * has passed.  Cheap enough to call at every node. */
void searcher_poll_clock(Searcher *s);

/* Same, but checks the clock immediately. */
void searcher_check_clock_now(Searcher *s);

#endif /* SNAPMIND_SEARCH_SEARCHER_H */
