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

#include "alphabeta.h"
#include "movepick.h"
#include "see.h"
#include "../board/movegen.h"
#include "../eval/eval.h"
#include "../eval/pawn_tt.h"

#define MATE_THRESHOLD 90000

/* ── Singular Extension constants ──
 * SE_DEPTH_MIN       : minimum remaining depth before we attempt the
 *                      verification search.
 * SE_TT_DEPTH_MARGIN : the TT entry must have been searched to at least
 *                      depth - SE_TT_DEPTH_MARGIN ply.
 * SE_DEPTH_SCALE     : singular_beta = tt_score - depth * SE_DEPTH_SCALE.
 * SE_DOUBLE_MARGIN   : if the reduced search comes back this many centipawns
 *                      below singular_beta, extend by 2 (double extension). */
#define SE_DEPTH_MIN 8
#define SE_TT_DEPTH_MARGIN 3
#define SE_DEPTH_SCALE 2
#define SE_DOUBLE_MARGIN 30
#define PV_NEG_MARGIN 25
#define ASP_DEPTH_MIN 4
#define ASP_BASE 20
#define ASP_SWING_CAP 30
#define ASP_FULL_AT 400

/* ── Reverse Futility Pruning (RFP) constants ──
 * If static_eval - margin >= beta we prune without generating moves.
 * RFP_DEPTH_MAX   : deepest ply at which we trust the static eval enough.
 * RFP_MARGIN_BASE : centipawns deducted per ply.
 * RFP_IMPROVING   : extra leniency when our eval has risen since 2 plies ago. */
#define RFP_DEPTH_MAX 7
#define RFP_MARGIN_BASE 80
#define RFP_IMPROVING 20

/* ── small helpers ── */

static inline uint8_t sat_add_u8(uint8_t a, uint8_t b) {
    unsigned r = (unsigned)a + (unsigned)b;
    return (uint8_t)(r > 255 ? 255 : r);
}
static inline uint8_t sat_sub_u8(uint8_t a, uint8_t b) {
    return (uint8_t)(a > b ? a - b : 0);
}
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int iabs(int x)        { return x < 0 ? -x : x; }

static int quiesce(Searcher *s, Board *board, int alpha, int beta, uint16_t ply);
static int negamax_inner(Searcher *s, Board *board, uint8_t depth, uint16_t ply,
                         int alpha, int beta, bool is_null, Move prev_move, Move excluded);

/* ── Draw score randomization ──
 * Instead of returning a flat 0 for draws, return a small pseudo-random value
 * derived from the node counter XOR'd with the board hash (range -1 … +2 cp).
 * It breaks ties between drawn lines without affecting material decisions. */
static inline int draw_score(const Searcher *s, const Board *board) {
    return (int)((s->nodes ^ board->hash) & 0x2) - 1;
}

static int tt_score_store(int score, uint16_t ply) {
    if      (score >  MATE_THRESHOLD) return score + (int)ply;
    else if (score < -MATE_THRESHOLD) return score - (int)ply;
    else                              return score;
}

static int tt_score_retrieve(int score, uint16_t ply) {
    if      (score >  MATE_THRESHOLD) return score - (int)ply;
    else if (score < -MATE_THRESHOLD) return score + (int)ply;
    else                              return score;
}

static bool has_non_pawn_material(const Board *board, Color color) {
    return board_bb(board, color, PT_KNIGHT) != 0
        || board_bb(board, color, PT_BISHOP) != 0
        || board_bb(board, color, PT_ROOK)   != 0
        || board_bb(board, color, PT_QUEEN)  != 0;
}

static bool repetition_draw(const Searcher *s, const Board *board) {
    size_t len = s->rep_stack.len;
    if (len < 5) return false;               /* need at least 4 plies back */

    size_t limit = (size_t)board->halfmove_clock;
    if (limit > len - 1) limit = len - 1;

    for (size_t d = 4; d <= limit; d += 2)
        if (s->rep_stack.data[len - 1 - d] == board->hash)
            return true;
    return false;
}

/* ── Root search ── */

static void result_init(SearchResult *r, Move mv, int score, uint8_t depth, bool forced) {
    r->mv        = mv;
    r->score     = score;
    r->depth     = depth;
    r->nodes     = 0;
    r->is_forced = forced;
    r->pv_len    = 0;
}

SearchResult searcher_search(Searcher *s, Board *board, uint8_t max_depth) {
    tt_new_search(&s->tt);
    s->nodes = 0;
    history_age(&s->history);
    u64vec_clone_from(&s->rep_stack, &s->game_history);
    u64vec_push(&s->rep_stack, board->hash);

    MoveList legal;
    generate_legal_moves(board, &legal);

    SearchResult best;

    if (legal.len == 1) {
        result_init(&best, legal.moves[0], evaluate(board), 0, true);
        best.pv[0]  = legal.moves[0];
        best.pv_len = 1;
        return best;
    }
    if (ml_is_empty(&legal)) {
        int score = is_in_check(board, board->side_to_move) ? -MATE_SCORE : 0;
        result_init(&best, MOVE_NULL, score, 0, false);
        return best;
    }

    if (board_game_phase(board) == PHASE_OPENING) {
        bool seen_before = false;
        for (size_t i = 0; i < s->game_history.len; i++) {
            if (s->game_history.data[i] == board->hash) { seen_before = true; break; }
        }
        if (!seen_before) {
            OpeningEntry entry;
            if (oc_probe(&s->opening_cache, board->hash, &entry)) {
                if (entry.depth >= 6 && ml_contains(&legal, entry.mv)) {
                    result_init(&best, entry.mv, entry.eval, entry.depth, false);
                    best.pv[0]  = entry.mv;
                    best.pv_len = 1;
                    return best;
                }
            }
        }
    }

    result_init(&best, legal.moves[0], -MATE_SCORE, 0, false);

    int     prev_score   = 0;
    int     prev2_score  = 0;
    Move    prev_best_mv = MOVE_NULL;
    uint8_t stable_iters = 0;

    for (int di = 1; di <= (int)max_depth; di++) {
        uint8_t depth = (uint8_t)di;
        searcher_check_clock_now(s);
        if (s->stop) break;

        int alpha, beta, window;
        if (depth < ASP_DEPTH_MIN || iabs(prev_score) >= MATE_THRESHOLD) {
            alpha = -MATE_SCORE; beta = MATE_SCORE; window = MATE_SCORE;
        } else {
            int swing = imin(iabs(prev_score - prev2_score), ASP_SWING_CAP);
            window = ASP_BASE + swing;
            alpha = imax(prev_score - window, -MATE_SCORE);
            beta = imin(prev_score + window, MATE_SCORE);
        }

        int score;
        for (;;) {
            int sv = negamax_inner(s, board, depth, 0, alpha, beta, false, MOVE_NONE, MOVE_NONE);
            if (s->stop) { score = sv; break; }
            if (sv <= alpha) {
                /* Fail low: tighten beta toward the midpoint, then widen alpha
                 * downward. */
                beta   = (alpha + beta) / 2;
                alpha  = imax(alpha - window, -MATE_SCORE);
                window = imin(window * 2, MATE_SCORE);
                if (window > ASP_FULL_AT) { alpha = -MATE_SCORE; beta = MATE_SCORE; }
            } else if (sv >= beta) {
                /* Fail high: widen beta, reset alpha symmetrically. */
                alpha  = (alpha + beta) / 2;
                beta   = imin(beta + window, MATE_SCORE);
                window = imin(window * 2, MATE_SCORE);
                if (window > ASP_FULL_AT) { alpha = -MATE_SCORE; beta = MATE_SCORE; }
            } else {
                score = sv;
                break;
            }
        }

        if (s->stop) break;

        best.score  = score;
        best.depth  = depth;
        best.nodes  = s->nodes;
        best.pv_len = searcher_extract_pv(s, board, depth, best.pv);
        /* Prefer the validated PV head over a raw TT probe — the PV has already
         * had each move checked against generate_legal_moves, so it cannot be a
         * hash-collision ghost.  Only fall back to the TT if the PV is empty. */
        if (best.pv_len > 0) {
            best.mv = best.pv[0];
        } else {
            const TTEntry *e = tt_probe(&s->tt, board->hash);
            if (e != NULL && !move_is_null(e->mv)) best.mv = e->mv;
        }
        prev2_score = prev_score;
        prev_score = score;

        if (board_game_phase(board) == PHASE_OPENING && depth >= 6 && depth <= 8) {
            oc_store(&s->opening_cache, board->hash, score, depth, best.mv);
        }

        if (best.mv == prev_best_mv) {
            if (stable_iters < 255) stable_iters++;
        } else {
            stable_iters = 0;
        }
        prev_best_mv = best.mv;

        /* The old timer thread raised soft_stop at a wall-clock instant; here we
         * look at the clock once per iteration so it is seen at the same point. */
        searcher_check_clock_now(s);
        if (s->soft_stop && stable_iters >= 3) {
            break;
        }
    }

    return best;
}

int searcher_extract_pv(const Searcher *s, const Board *board, uint8_t depth, Move *pv) {
    int n = 0;
    Board pos = *board;
    uint64_t seen[256];
    int n_seen = 0;
    for (int i = 0; i < (int)depth; i++) {
        /* seen.insert(pos.hash) — stop on a repeated position */
        bool dup = false;
        for (int j = 0; j < n_seen; j++) {
            if (seen[j] == pos.hash) { dup = true; break; }
        }
        if (dup) break;
        seen[n_seen++] = pos.hash;

        const TTEntry *entry = tt_probe(&s->tt, pos.hash);
        if (entry != NULL && !move_is_null(entry->mv)) {
            Move mv = entry->mv;
            /* Validate the move is legal before applying it — TT hash
             * collisions can produce garbage moves that would corrupt pos. */
            MoveList legal;
            generate_legal_moves(&pos, &legal);
            if (!ml_contains(&legal, mv)) break;
            UndoInfo undo = board_make_move(&pos, mv);
            Color prev_stm = color_flip(pos.side_to_move);
            if (is_in_check(&pos, prev_stm)) {
                board_unmake_move(&pos, mv, undo);
                break;
            }
            pv[n++] = mv;
        } else {
            break;
        }
    }
    return n;
}

/* ── Main search ── */

static int negamax_inner(Searcher *s, Board *board, uint8_t depth, uint16_t ply,
                         int alpha, int beta, bool is_null, Move prev_move,
                         /* Singular verification: when not MOVE_NONE, skip that
                          * move in the loop, suppress the TT cutoff, and do not
                          * write back to the TT. */
                         Move excluded)
{
    s->nodes += 1;

    bool is_pv = beta > alpha + 1;
    searcher_poll_clock(s);
    if (s->stop) return 0;

    if (ply >= MAX_PLY) return evaluate(board);

    if (board->halfmove_clock >= 100) return draw_score(s, board);

    if (ply > 0) {
        if (repetition_draw(s, board)) return draw_score(s, board);
    }

    /* ── Mate distance pruning ──
     * If a forced mate has already been found at a shallower ply, tighten the
     * alpha/beta window to the bounds implied by the current ply distance.
     * Only at ply > 0 — never shrink the root window. */
    if (ply > 0) {
        alpha = imax(alpha, -MATE_SCORE + (int)ply);
        beta  = imin(beta,   MATE_SCORE - (int)ply);
        if (alpha >= beta) return alpha;
    }

    bool in_check = is_in_check(board, board->side_to_move);
    if (in_check && ply < 16) depth = sat_add_u8(depth, 1);

    /* ── TT probe ──
     * Always collect the TT move/score for move ordering and SE.  Only apply
     * the early-return cutoff on normal (non-excluded) searches. */
    Move    tt_move  = MOVE_NONE;
    int     tt_score = 0;
    uint8_t tt_depth = 0;
    int     tt_bound = -1;       /* -1 = no TT hit (Option<Bound>::None) */

    const TTEntry *e = tt_probe(&s->tt, board->hash);
    if (e != NULL) {
        /* A stored null move means "no move known" (fail-low entry). */
        tt_move  = move_is_null(e->mv) ? MOVE_NONE : e->mv;
        tt_score = tt_score_retrieve(e->score, ply);
        tt_depth = e->depth;
        tt_bound = (int)tt_entry_bound(e);

        if (excluded == MOVE_NONE && e->depth >= depth) {
            switch (tt_entry_bound(e)) {
                case BOUND_EXACT:
                    return tt_score;
                case BOUND_LOWER:
                    alpha = imax(alpha, tt_score);
                    if (alpha >= beta) return tt_score;
                    break;
                case BOUND_UPPER:
                    beta = imin(beta, tt_score);
                    if (alpha >= beta) return tt_score;
                    break;
            }
        }
    }

    if (depth == 0) {
        return quiesce(s, board, alpha, beta, ply);
    }

    /* ── Internal Iterative Reduction (IIR) ──
     * Applied whenever this node has no TT move (never searched before, or its
     * entry was evicted).  Two tiers:
     *   • Non-PV nodes (depth >= 5): fire when no TT move AND > 2 s remain.
     *   • PV nodes    (depth >= 4):  also reduce by 1. */
    bool allow_iir_nonpv = tt_move == MOVE_NONE
        && depth >= 5
        && !is_pv
        && !in_check
        && s->time_remaining_ms > 2000;
    bool allow_iir_pv = tt_move == MOVE_NONE
        && depth >= 4
        && is_pv
        && !in_check;
    if (allow_iir_nonpv || allow_iir_pv) depth = (uint8_t)(depth - 1);

    int raw_static_eval = !in_check ? evaluate(board) : 0;
    /* Apply pawn correction history: adjust the raw eval by the running
     * divergence observed for this pawn structure. */
    uint64_t ph = pawn_hash(board);
    int static_eval = !in_check
        ? history_correct_eval(&s->history, color_index(board->side_to_move), ph, raw_static_eval)
        : 0;

    /* Record this ply's static eval for the "improving" heuristic. */
    s->static_evals[imin((int)ply, 127)] = static_eval;

    /* ── Reverse Futility Pruning (RFP) ──
     * If the static eval exceeds beta by a depth-scaled margin, the position is
     * almost certainly too good for us: return the static eval immediately. */
    if (!in_check
        && !is_pv
        && depth >= 1
        && depth <= RFP_DEPTH_MAX
        && excluded == MOVE_NONE
        && iabs(beta) < MATE_THRESHOLD
        && has_non_pawn_material(board, board->side_to_move))
    {
        /* Improving: our static eval has grown since our last move (2 plies
         * ago, same side to move). */
        bool improving = ply >= 2
            && static_eval > s->static_evals[imin((int)ply - 2, 127)];

        int margin = RFP_MARGIN_BASE * (int)depth + (improving ? RFP_IMPROVING : 0);

        if (static_eval - margin >= beta) {
            /* Fail-soft: return static_eval, not beta. */
            return static_eval;
        }
    }

    /* Razoring */
    if (depth == 1 && !in_check) {
        const int RAZOR_MARGIN = 300;
        if (static_eval + RAZOR_MARGIN < alpha) {
            int q = quiesce(s, board, alpha, beta, ply);
            if (q < alpha) return q;
        }
    }

    /* Null-move pruning */
    if (!in_check
        && !is_null
        && excluded == MOVE_NONE
        && depth >= 4
        && simple_eval(board) >= beta
        && has_non_pawn_material(board, board->side_to_move))
    {
        int r = 3 + depth / 4;
        if (r > 6) r = 6;
        UndoInfo null_undo = board_make_null_move(board);
        u64vec_push(&s->rep_stack, board->hash);
        int null_score = -negamax_inner(s, board, sat_sub_u8(depth, (uint8_t)(r + 1)),
                                        (uint16_t)(ply + 1), -beta, -beta + 1,
                                        true, MOVE_NONE, MOVE_NONE);
        u64vec_pop(&s->rep_stack);
        board_unmake_null_move(board, null_undo);
        if (null_score >= beta) return null_score;
    }

    /* ── Singular Extension ──
     * Check whether the TT move is the only good move in this position by
     * running a reduced-depth search *excluding* the TT move with a tighter
     * beta (singular_beta = tt_score - depth * SE_DEPTH_SCALE).
     * (a) se_score < singular_beta → TT move is singular: extend +1
     * (+2 on non-PV nodes if far below).
     * (b) se_score >= singular_beta && singular_beta >= beta (multicut)
     * → prune.
     * (c) otherwise → continue normally. */
    bool singular_extension = false;
    bool double_extension   = false;
    bool negative_extension = false;

    bool can_singular =
        depth >= SE_DEPTH_MIN
        && excluded == MOVE_NONE
        && ply > 0
        && !in_check
        && !is_null
        && tt_move != MOVE_NONE
        && tt_depth >= sat_sub_u8(depth, SE_TT_DEPTH_MARGIN)
        && tt_bound != -1 && tt_bound != (int)BOUND_UPPER
        && iabs(tt_score) < MATE_THRESHOLD;

    if (can_singular) {
        Move tt_mv         = tt_move;
        int  singular_beta = imax(tt_score - (int)depth * SE_DEPTH_SCALE, -MATE_SCORE + 1);
        uint8_t singular_depth = (uint8_t)(depth / 2);

        int se_score = negamax_inner(s, board, singular_depth, ply,
                                     singular_beta - 1, singular_beta,
                                     false, prev_move, tt_mv);

        if (se_score < singular_beta) {
            singular_extension = true;
            if (!is_pv && se_score < singular_beta - SE_DOUBLE_MARGIN) {
                double_extension = true;
            }
        } else if (singular_beta >= beta && !is_pv) {
            /* Multicut: alternatives already beat beta — prune this subtree. */
            return singular_beta;
        } else if (tt_score >= beta) {
            /*
            New: not singular, but the position is clearly good.
            the tt move is interchangeable. reduce it by 1. (negative extension)
            */
            if (!is_pv) {
                negative_extension = true; //no pv: always
            } else if (tt_score >= beta + PV_NEG_MARGIN) {
                negative_extension = true; //pv: only when very conf
            }
            
        }
    }

    /* Futility pruning flag (applied per-move below) */
    bool futile = !in_check
        && depth <= 2
        && static_eval + (int)depth * 200 < alpha;

    /* ── Late Move Pruning (LMP) ──
     * Once we have searched enough quiet moves at a given depth without raising
     * alpha, further quiet moves are very unlikely to be good.  Threshold =
     * LMP_BASE + depth^2. */
    const int LMP_DEPTH_MAX = 7;
    const int LMP_BASE      = 3;
    int lmp_threshold = LMP_BASE + (int)depth * (int)depth;
    bool allow_lmp = !is_pv
        && !in_check
        && depth <= LMP_DEPTH_MAX
        && excluded == MOVE_NONE;

    /* Clear this ply's prev_moves slot so null-move children see no context.
     * It is set properly per-move in the loop below. */
    s->prev_moves[imin((int)ply, 127)] = MOVE_NONE;

    MoveList moves;
    generate_legal_moves(board, &moves);
    if (ml_is_empty(&moves)) {
        return in_check ? -MATE_SCORE + (int)ply : 0;
    }

    /* Single-reply extension (only on normal searches, not excluded).  Use
     * `depth` (not depth - 1) so the forced reply gets a genuine +1 extension
     * over a normal search, then write the result to the TT before returning. */
    if (moves.len == 1 && excluded == MOVE_NONE && depth > 0) {
        Move mv = moves.moves[0];
        UndoInfo undo = board_make_move(board, mv);
        s->prev_moves[imin((int)ply, 127)] = mv;
        u64vec_push(&s->rep_stack, board->hash);
        int score = -negamax_inner(s, board, depth, (uint16_t)(ply + 1), -beta, -alpha, false, mv, MOVE_NONE);
        u64vec_pop(&s->rep_stack);
        board_unmake_move(board, mv, undo);
        if (!s->stop) {
            tt_store(&s->tt, board->hash, depth, tt_score_store(score, ply), mv, BOUND_EXACT, is_pv);
        }
        return score;
    }

    Move cont2_ctx = (ply >= 2) ? s->prev_moves[ply - 2] : MOVE_NONE;
    ScoredList ordered;
    score_moves(&ordered, moves.moves, moves.len, board, ply, tt_move, &s->history, prev_move, cont2_ctx);

    Move best_move  = ordered.moves[0].mv;
    int  best_score = -MATE_SCORE;
    Bound bound     = BOUND_UPPER;

    Move   quiets_tried[256];
    int    quiets_count = 0;

    /* Captures tried before a cutoff, so we can penalise them via capture
     * history (mirrors the butterfly-history treatment of quiets). */
    Move   captures_tried[64];
    int    captures_count = 0;

    for (int move_idx = 0; move_idx < ordered.len; move_idx++) {
        const ScoredMove *sm = &ordered.moves[move_idx];
        if (s->stop) break;

        Move mv = sm->mv;

        /* Skip the excluded move in a singular verification search. */
        if (excluded == mv) continue;

        bool is_capture = move_is_capture(mv);
        bool is_promo   = move_is_promo(mv);
        bool is_killer;
        {
            const Move *k = history_killers_at(&s->history, ply);
            is_killer = (k[0] == mv) || (k[1] == mv);
        }

        if (futile && !is_capture && !is_promo && !is_killer && move_idx > 0) {
            continue;
        }

        /* ── Late Move Pruning ──
         * quiets_count = number of quiet moves already searched this node. */
        if (allow_lmp && !is_capture && !is_promo && !is_killer && move_idx > 0
            && quiets_count >= lmp_threshold)
        {
            continue;
        }

        if (is_capture && move_idx > 0 && depth >= 2 && sm->see_val < -50) {
            continue;
        }

        /* ── Per-move extension ──
         * The singular extension applies only to the TT move. */
        int move_ext = 0;
        if (mv == tt_move) {
            if (singular_extension) move_ext = double_extension ? 2 : 1;
            else if (negative_extension) move_ext = -1;
        }

        /* Capture side-to-move BEFORE make_move — after make_move the board
         * flips to the opponent. */
        Color stm = board->side_to_move;
        UndoInfo undo = board_make_move(board, mv);
        s->prev_moves[imin((int)ply, 127)] = mv;
        u64vec_push(&s->rep_stack, board->hash);

        /* Check-extension probe: only for unreduced, non-extended later moves. */
        bool gives_check = false;
        if (depth >= 3
            && move_idx >= 3
            && move_ext == 0
            && !is_capture
            && !is_promo
            && !is_killer
            && !in_check)
        {
            gives_check = is_in_check(board, board->side_to_move);
        }

        /* child_depth = depth - 1 + extension. */
        uint8_t check_ext = gives_check ? 1 : 0;
        int cd = (int)depth - 1 + move_ext + (int)check_ext;
        if (cd < 0) cd = 0;
        uint8_t child_depth = (uint8_t)cd;

        int score;
        if (depth >= 3
            && move_idx >= 3
            && move_ext == 0 /* never LMR a singularly extended move */
            && check_ext == 0 /* never LMR a check-extended move */
            && !is_capture
            && !is_promo
            && !is_killer
            && !in_check
            && !gives_check)
        {
            int history_score = history_get(&s->history, mv, stm);
            int cont1_score   = history_get_cont1(&s->history, prev_move, mv);
            int cont2_score   = history_get_cont2(&s->history, cont2_ctx,  mv);
            int combined      = history_score + cont1_score / 2 + cont2_score / 4;
            uint8_t reduction = lmr_get_adjusted(&s->lmr, depth, (size_t)move_idx, combined);
            uint8_t reduced_depth = sat_sub_u8(child_depth, reduction);
            if (reduced_depth < 1) reduced_depth = 1;
            int sv = -negamax_inner(s, board, reduced_depth, (uint16_t)(ply + 1),
                                    -alpha - 1, -alpha, false, mv, MOVE_NONE);
            if (sv > alpha && !s->stop) {
                score = -negamax_inner(s, board, child_depth, (uint16_t)(ply + 1), -beta, -alpha, false, mv, MOVE_NONE);
            } else {
                score = sv;
            }
        } else {
            score = -negamax_inner(s, board, child_depth, (uint16_t)(ply + 1), -beta, -alpha, false, mv, MOVE_NONE);
        }

        u64vec_pop(&s->rep_stack);
        board_unmake_move(board, mv, undo);

        if (!is_capture && !is_promo) {
            quiets_tried[quiets_count] = mv;
            quiets_count++;
        } else if (is_capture && !is_promo && captures_count < 64) {
            captures_tried[captures_count] = mv;
            captures_count++;
        }

        if (score > best_score) { best_score = score; best_move = mv; }
        if (score > alpha)      { alpha = score; bound = BOUND_EXACT; }

        if (alpha >= beta) {
            if (!is_capture && !is_promo) {
                history_update_killers(&s->history, mv, ply);
                history_update(&s->history, mv, board->side_to_move, depth);
                if (prev_move != MOVE_NONE) {
                    history_update_countermove(&s->history, prev_move, mv);
                }
                /* Continuation history: 1-ply context = prev_move, 2-ply
                 * context = move played two plies ago. */
                Move cont1_ctx = prev_move;
                Move cont2_cut = (ply >= 2) ? s->prev_moves[ply - 2] : MOVE_NONE;
                history_update_cont1(&s->history, cont1_ctx, mv, depth);
                history_update_cont2(&s->history, cont2_cut, mv, depth);
                for (int qi = 0; qi < quiets_count; qi++) {
                    Move q = quiets_tried[qi];
                    if (q != mv) {
                        history_penalise(&s->history, q, board->side_to_move, depth);
                        history_penalise_cont1(&s->history, cont1_ctx, q, depth);
                        history_penalise_cont2(&s->history, cont2_cut, q, depth);
                    }
                }
            } else if (is_capture && !is_promo) {
                /* Update capture history: reward the cutter, penalise captures
                 * that were searched first without causing a cutoff. */
                PieceOn mover = piece_on_fast(board, move_from(mv));
                if (mover.some) {
                    PieceOn captured = piece_on_fast(board, move_to(mv));
                    if (captured.some) {
                        history_update_capture(&s->history, mover.piece, move_to(mv), captured.piece, depth);
                        for (int ci = 0; ci < captures_count; ci++) {
                            Move cap = captures_tried[ci];
                            if (cap != mv) {
                                PieceOn mp = piece_on_fast(board, move_from(cap));
                                PieceOn cp = piece_on_fast(board, move_to(cap));
                                if (mp.some && cp.some) {
                                    history_penalise_capture(&s->history, mp.piece, move_to(cap), cp.piece, depth);
                                }
                            }
                        }
                    }
                }
            }
            bound = BOUND_LOWER;
            /* Clear this ply's slot before returning so any sibling node that
             * re-enters at this ply does not inherit the cutting move as stale
             * context. */
            s->prev_moves[imin((int)ply, 127)] = MOVE_NONE;
            break;
        }
    }

    /* Do NOT write to the TT from a singular verification search — the score is
     * for the position-minus-one-move, not the full position. */
    if (excluded == MOVE_NONE && !s->stop) {
        tt_store(&s->tt, board->hash, depth, tt_score_store(best_score, ply),
                 bound == BOUND_UPPER ? MOVE_NULL : best_move, bound, is_pv);
        /* Update pawn correction history using the *raw* static eval (before
         * correction).  Only meaningful when we have a real eval (not in check)
         * and the score is not in mate range. */
        if (!in_check) {
            history_update_correction(&s->history, color_index(board->side_to_move),
                                      ph, raw_static_eval, best_score, depth, bound);
        }
    }
    return best_score;
}

/* ── Quiescence search ── */

static int quiesce_inner(Searcher *s, Board *board, int alpha, int beta, uint16_t ply, uint8_t qdepth);

static int quiesce(Searcher *s, Board *board, int alpha, int beta, uint16_t ply) {
    /* Safety cap of 32 — delta pruning terminates quiet positions long before
     * this, and repetition detection handles perpetual checks. */
    return quiesce_inner(s, board, alpha, beta, ply, 32);
}

static int quiesce_inner(Searcher *s, Board *board, int alpha, int beta, uint16_t ply, uint8_t qdepth) {
    s->nodes += 1;
    if (ply >= MAX_PLY) return evaluate(board);

    searcher_poll_clock(s);

    if (ply > 0) {
        if (repetition_draw(s, board)) return draw_score(s, board);
    }

    bool in_check  = is_in_check(board, board->side_to_move);
    int  stand_pat = evaluate(board);

    int best_score = in_check ? -MATE_SCORE : stand_pat;

    if (!in_check) {
        if (stand_pat >= beta) return stand_pat;

        /* ── Delta pruning ──
         * If even capturing the most valuable piece on the board cannot raise
         * alpha, standing pat is correct.  Disabled near mate. */
        const int DELTA_MARGIN = 900;   /* queen value — safest upper bound */
        if (stand_pat + DELTA_MARGIN < alpha && alpha < MATE_THRESHOLD) {
            return stand_pat;
        }

        if (stand_pat > alpha) alpha = stand_pat;
    }

    if (qdepth == 0 && !in_check) return best_score;

    MoveList moves;
    if (in_check) generate_legal_moves(board, &moves);
    else          generate_legal_captures(board, &moves);

    if (ml_is_empty(&moves)) {
        return in_check ? -MATE_SCORE + (int)ply : best_score;
    }

    /* Free check evasions: when in check we do NOT decrement qdepth, so forced
     * check sequences resolve fully.  Quiet nodes consume one qdepth unit. */
    uint8_t next_qdepth = in_check ? qdepth : sat_sub_u8(qdepth, 1);

    for (int i = 0; i < moves.len; i++) {
        Move mv = moves.moves[i];
        if (s->stop) break;

        if (!in_check && move_is_capture(mv)) {
            int see_score = see(board, move_from(mv), move_to(mv));
            if (stand_pat + see_score + 200 < alpha) continue;
            if (see_score < -100) continue;
        }

        UndoInfo undo = board_make_move(board, mv);
        u64vec_push(&s->rep_stack, board->hash);
        int score = -quiesce_inner(s, board, -beta, -alpha, (uint16_t)(ply + 1), next_qdepth);
        u64vec_pop(&s->rep_stack);
        board_unmake_move(board, mv, undo);

        if (score > best_score) best_score = score;
        if (score >= beta) return best_score;
        if (score > alpha) alpha = score;
    }

    return best_score;
}