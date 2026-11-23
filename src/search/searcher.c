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

#include "../platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "searcher.h"

void u64vec_init(U64Vec *v, size_t capacity) {
    if (capacity < 4) capacity = 4;
    v->data = (uint64_t *)malloc(capacity * sizeof(uint64_t));
    if (!v->data) { fprintf(stderr, "out of memory\n"); exit(1); }
    v->len = 0;
    v->cap = capacity;
}

void u64vec_free(U64Vec *v) {
    free(v->data);
    v->data = NULL;
    v->len = v->cap = 0;
}

void u64vec_push(U64Vec *v, uint64_t x) {
    if (v->len == v->cap) {
        size_t ncap = v->cap * 2;
        uint64_t *nd = (uint64_t *)realloc(v->data, ncap * sizeof(uint64_t));
        if (!nd) { fprintf(stderr, "out of memory\n"); exit(1); }
        v->data = nd;
        v->cap  = ncap;
    }
    v->data[v->len++] = x;
}

void u64vec_clone_from(U64Vec *dst, const U64Vec *src) {
    dst->len = 0;
    for (size_t i = 0; i < src->len; i++) u64vec_push(dst, src->data[i]);
}

void searcher_init(Searcher *s, size_t tt_mb, size_t oc_mb) {
    tt_init(&s->tt, tt_mb);
    oc_init(&s->opening_cache, oc_mb);
    s->stop             = 0;
    s->soft_stop        = 0;
    s->soft_deadline_us = UINT64_MAX;
    s->hard_deadline_us = UINT64_MAX;
    s->poll_counter     = 0;
    s->nodes            = 0;
    history_init(&s->history);
    lmr_init(&s->lmr);
    u64vec_init(&s->rep_stack, 256);
    u64vec_init(&s->game_history, 256);
    for (int i = 0; i < PLY_ARRAY_SIZE; i++) {
        s->prev_moves[i]   = MOVE_NONE;
        s->static_evals[i] = 0;
    }
    s->time_remaining_ms = UINT64_MAX;
}

void searcher_check_clock_now(Searcher *s) {
    if (s->soft_deadline_us == UINT64_MAX && s->hard_deadline_us == UINT64_MAX) return;
    uint64_t now = now_us();
    if (now >= s->soft_deadline_us) s->soft_stop = 1;
    if (now >= s->hard_deadline_us) s->stop = 1;
}

void searcher_poll_clock(Searcher *s) {
    if ((++s->poll_counter & 255u) == 0) searcher_check_clock_now(s);
}
