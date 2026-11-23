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

#include "platform.h"   /* keep first: defines _POSIX_C_SOURCE for <time.h> */

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uci.h"
#include "board/movegen.h"
#include "eval/eval.h"

#define MOVE_OVERHEAD_MS 50

/* ── Number parsing (same acceptance rules as Rust's str::parse) ─────────── */

/* Signed 64-bit: optional '+'/'-', digits only, must fit. */
static bool parse_i64(const char *s, int64_t *out) {
    bool neg = false;
    if (*s == '+') s++;
    else if (*s == '-') { neg = true; s++; }
    if (*s == '\0') return false;
    uint64_t v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        uint64_t d = (uint64_t)(*s - '0');
        if (v > (UINT64_MAX - d) / 10) return false;   /* overflow */
        v = v * 10 + d;
    }
    if (neg) {
        if (v > (uint64_t)INT64_MAX + 1) return false;
        *out = (v == (uint64_t)INT64_MAX + 1) ? INT64_MIN : -(int64_t)v;
    } else {
        if (v > (uint64_t)INT64_MAX) return false;
        *out = (int64_t)v;
    }
    return true;
}

/* Unsigned 64-bit: optional '+', digits only, must fit. */
static bool parse_u64(const char *s, uint64_t *out) {
    if (*s == '+') s++;
    if (*s == '\0') return false;
    uint64_t v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        uint64_t d = (uint64_t)(*s - '0');
        if (v > (UINT64_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    *out = v;
    return true;
}

/* Unsigned 8-bit (perft depth). */
static bool parse_u8(const char *s, uint8_t *out) {
    uint64_t v;
    if (!parse_u64(s, &v) || v > 255) return false;
    *out = (uint8_t)v;
    return true;
}

/* ── Engine construction ── */

void uci_engine_init(UciEngine *e) {
    board_startpos(&e->board);
    searcher_init(&e->searcher, 64, 16);
    u64vec_init(&e->game_history, 256);
}

/* ── Helpers ── */

/* Format a search score as a UCI `score` token.  Scores within MATE_THRESHOLD
 * of +-MATE_SCORE are emitted as `score mate N` (positive = we mate the
 * opponent in N, negative = the opponent mates us in N).  All other scores are
 * emitted as `score cp N`. */
static void format_score(int score, char *buf, size_t n) {
    const int MATE_THRESHOLD = 90000;
    if (score > MATE_THRESHOLD) {
        /* MATE_SCORE - score gives the ply distance; halve for full moves. */
        int moves = (MATE_SCORE - score + 1) / 2;
        snprintf(buf, n, "score mate %d", moves);
    } else if (score < -MATE_THRESHOLD) {
        int moves = (MATE_SCORE + score + 1) / 2;
        snprintf(buf, n, "score mate -%d", moves);
    } else {
        snprintf(buf, n, "score cp %d", score);
    }
}

/* Looks up `key` in args and parses the token after it as i64. */
static bool parse_arg(char **args, int nargs, const char *key, int64_t *out) {
    for (int i = 0; i < nargs; i++) {
        if (strcmp(args[i], key) == 0) {
            if (i + 1 >= nargs) return false;
            return parse_i64(args[i + 1], out);
        }
    }
    return false;
}

static bool parse_uci_move(const char *s, Board *board, Move *out) {
    size_t len = strlen(s);
    if (len < 4) return false;

    if (s[0] < 'a' || s[0] > 'h') return false;
    if (s[1] < '1' || s[1] > '8') return false;
    if (s[2] < 'a' || s[2] > 'h') return false;
    if (s[3] < '1' || s[3] > '8') return false;

    int from = (s[1] - '1') * 8 + (s[0] - 'a');
    int to   = (s[3] - '1') * 8 + (s[2] - 'a');
    int promo = 0;
    if (len >= 5) {
        switch (s[4]) {
            case 'n': promo = 1; break;
            case 'b': promo = 2; break;
            case 'r': promo = 3; break;
            case 'q': promo = 4; break;
            default:  promo = 0; break;
        }
    }

    MoveList legal;
    generate_legal_moves(board, &legal);
    for (int i = 0; i < legal.len; i++) {
        Move mv = legal.moves[i];
        if (move_from(mv) == from && move_to(mv) == to && move_promo(mv) == promo) {
            *out = mv;
            return true;
        }
    }
    return false;
}

static uint64_t perft(Board *board, uint8_t depth) {
    if (depth == 0) return 1;
    MoveList moves;
    generate_legal_moves(board, &moves);
    if (depth == 1) return (uint64_t)moves.len;
    uint64_t total = 0;
    for (int i = 0; i < moves.len; i++) {
        Move mv = moves.moves[i];
        UndoInfo undo = board_make_move(board, mv);
        total += perft(board, (uint8_t)(depth - 1));
        board_unmake_move(board, mv, undo);
    }
    return total;
}

/* Joins tokens [from, to) with single spaces into a freshly allocated string. */
static char *join_tokens(char **toks, int from, int to) {
    size_t total = 1;
    for (int i = from; i < to; i++) total += strlen(toks[i]) + 1;
    char *out = (char *)malloc(total);
    if (!out) { fprintf(stderr, "out of memory\n"); exit(1); }
    out[0] = '\0';
    for (int i = from; i < to; i++) {
        if (i > from) strcat(out, " ");
        strcat(out, toks[i]);
    }
    return out;
}

static int find_token(char **toks, int from, int to, const char *tok) {
    for (int i = from; i < to; i++) if (strcmp(toks[i], tok) == 0) return i;
    return -1;
}

static int find_token_nocase(char **toks, int from, int to, const char *tok) {
    for (int i = from; i < to; i++) {
        const char *a = toks[i];
        const char *b = tok;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (*a == '\0' && *b == '\0') return i;
    }
    return -1;
}

/* ── position ── */

/* Applies the moves toks[from..to) to the engine board.  The hash is pushed
 * only when the move actually parses: a failed parse must not leave an orphan
 * duplicate hash in game_history — duplicates trick the repetition detector
 * into declaring a false draw. */
static void apply_moves(UciEngine *e, char **toks, int from, int to) {
    for (int i = from; i < to; i++) {
        Move mv;
        if (parse_uci_move(toks[i], &e->board, &mv)) {
            u64vec_push(&e->game_history, e->board.hash);
            board_make_move(&e->board, mv);
        }
    }
}

/* toks[0..n) are the tokens after the "position" keyword. */
static void handle_position(UciEngine *e, char **toks, int n) {
    u64vec_clear(&e->game_history);

    int next = 0;   /* index of the first token not yet consumed */
    if (n > 0 && strcmp(toks[0], "startpos") == 0) {
        board_startpos(&e->board);
        next = 1;
    } else if (n > 0 && strcmp(toks[0], "fen") == 0) {
        int rest_from = 1;
        int moves_idx = find_token(toks, rest_from, n, "moves");
        int fen_end   = (moves_idx >= 0) ? moves_idx : n;
        char *fen = join_tokens(toks, rest_from, fen_end);
        if (!board_from_fen(&e->board, fen)) board_startpos(&e->board);
        free(fen);
        if (moves_idx >= 0) apply_moves(e, toks, moves_idx + 1, n);
        return;
    } else {
        /* An unrecognised position token: reset to startpos so the board and the
         * (just cleared) game history stay in sync — same as ucinewgame. */
        board_startpos(&e->board);
        next = (n > 0) ? 1 : 0;
    }

    int moves_idx = find_token(toks, next, n, "moves");
    if (moves_idx >= 0) apply_moves(e, toks, moves_idx + 1, n);
}

/* ── go ── */

/* now + ms (in microseconds), saturating to "no deadline". */
static uint64_t deadline_after(uint64_t t0_us, uint64_t ms) {
    if (ms > (UINT64_MAX - t0_us) / 1000u) return UINT64_MAX;
    return t0_us + ms * 1000u;
}

static void handle_go(UciEngine *e, char **args, int nargs) {
    int64_t wtime, btime, winc, binc, movetime = 0, depth_arg = 0, movestogo = 0;
    if (!parse_arg(args, nargs, "wtime", &wtime)) wtime = 10000;
    if (!parse_arg(args, nargs, "btime", &btime)) btime = 10000;
    if (!parse_arg(args, nargs, "winc",  &winc))  winc  = 0;
    if (!parse_arg(args, nargs, "binc",  &binc))  binc  = 0;
    bool has_movetime  = parse_arg(args, nargs, "movetime",  &movetime);
    bool has_depth     = parse_arg(args, nargs, "depth",     &depth_arg);
    bool has_movestogo = parse_arg(args, nargs, "movestogo", &movestogo);

    int64_t my_time, my_inc;
    if (e->board.side_to_move == COLOR_WHITE) { my_time = wtime; my_inc = winc; }
    else                                      { my_time = btime; my_inc = binc; }

    int64_t target_ms;
    if (has_movetime) {
        target_ms = movetime;
    } else {
        /* Adaptive moves-left estimate: 25 is a good middlegame/endgame baseline. */
        int64_t moves_left = has_movestogo ? movestogo : 25;
        if (moves_left < 1) moves_left = 1;
        int64_t base = my_time / moves_left + my_inc / 2;
        if (base < 50) base = 50;
        /* Low-time panic: when under 1 s spend at most 5% of remaining time. */
        if (my_time < 1000) {
            target_ms = my_time / 20;
            if (target_ms < 20) target_ms = 20;
        } else {
            target_ms = base > 10000 ? 10000 : base;
        }
        int64_t usable = my_time - MOVE_OVERHEAD_MS;
        if (usable < 1) usable= 1;
        if (target_ms > usable * 6 / 10) target_ms = usable * 6 / 10;
        if (target_ms < 1) target_ms = 1;
    }
    uint8_t max_depth = has_depth ? (uint8_t)depth_arg : (uint8_t)MAX_DEPTH;

    /* Determine whether to use a timer at all:
     *   - "go infinite"  → no timer
     *   - "go depth N"   → no timer; search runs until target depth
     *   - "go movetime"  → honour the exact ms requested (even with depth)
     *   - "go wtime ..." → use the calculated time budget */
    bool is_infinite = false;
    for (int i = 0; i < nargs; i++) if (strcmp(args[i], "infinite") == 0) is_infinite = true;
    bool is_movetime = has_movetime;
    bool use_timer   = !is_infinite && (!has_depth || is_movetime);

    Searcher *s = &e->searcher;
    s->stop      = 0;
    s->soft_stop = 0;
    s->soft_deadline_us = UINT64_MAX;
    s->hard_deadline_us = UINT64_MAX;

    if (use_timer) {
        /* ── Two-tier time management ──
         * Soft limit (target_ms): signals the search to stop early if the best
         * move has been stable for 3+ consecutive iterations.
         * Hard limit (target_ms * 3/2): absolute ceiling, used when the best
         * move is still changing.  "go movetime N" uses a single hard limit. */
        uint64_t soft_ms = (uint64_t)target_ms;
        uint64_t hard_ms = is_movetime ? (uint64_t)target_ms : (uint64_t)target_ms * 3u / 2u;
        if (!is_movetime) {
            int64_t hard_cap = (my_time - MOVE_OVERHEAD_MS) * 8 / 10;
            if (hard_cap < 1) hard_cap = 1;
            if (hard_ms > (uint64_t)hard_cap) hard_ms = (uint64_t)hard_cap;
        }
        uint64_t extra   = hard_ms > soft_ms ? hard_ms - soft_ms : 0;

        uint64_t t0 = now_us();
        s->soft_deadline_us = deadline_after(t0, soft_ms);
        s->hard_deadline_us = deadline_after(t0, soft_ms + extra);
    }

    uint64_t start_us = now_us();
    /* Give the searcher the full game history so it can detect repetitions that
     * span positions played before the search root. */
    u64vec_clone_from(&s->game_history, &e->game_history);
    s->time_remaining_ms = (uint64_t)my_time;
    SearchResult result = searcher_search(s, &e->board, max_depth);
    uint64_t elapsed = (now_us() - start_us) / 1000u;
    uint64_t nps = elapsed > 0 ? result.nodes * 1000u / elapsed : 0;

    char score_str[64];
    format_score(result.score, score_str, sizeof score_str);

    /* "(none)" is not a valid UCI move string; the conventional token for "no
     * legal move" is "bestmove 0000". */
    if (move_is_null(result.mv)) {
        printf("info depth %u %s nodes %" PRIu64 " nps %" PRIu64 " time %" PRIu64 " pv\n",
               (unsigned)result.depth, score_str, result.nodes, nps, elapsed);
        printf("bestmove 0000\n");
        return;
    }

    char pv_str[PV_CAP * 6 + 8];
    pv_str[0] = '\0';
    if (result.pv_len > 0) {
        size_t off = 0;
        for (int i = 0; i < result.pv_len; i++) {
            char m[6];
            move_to_uci(result.pv[i], m);
            if (i > 0) pv_str[off++] = ' ';
            size_t ml = strlen(m);
            memcpy(pv_str + off, m, ml);
            off += ml;
            pv_str[off] = '\0';
        }
    } else {
        move_to_uci(result.mv, pv_str);
    }

    printf("info depth %u %s nodes %" PRIu64 " nps %" PRIu64 " time %" PRIu64 " pv %s\n",
           (unsigned)result.depth, score_str, result.nodes, nps, elapsed, pv_str);
    if (result.is_forced) {
        printf("info string move is forced\n");
    }
    char best[6];
    move_to_uci(result.mv, best);
    printf("bestmove %s\n", best);
}

/* ── setoption ── */

static void handle_setoption(UciEngine *e, char **rest, int n) {
    int name_pos  = find_token_nocase(rest, 0, n, "name");
    int value_pos = find_token_nocase(rest, 0, n, "value");
    if (name_pos < 0 || value_pos < 0) return;
    if (value_pos < name_pos + 1) return;   /* malformed ("value" before "name") */

    char *name  = join_tokens(rest, name_pos + 1, value_pos);
    char *value = join_tokens(rest, value_pos + 1, n);

    for (char *p = name; *p; p++) *p = (char)tolower((unsigned char)*p);

    if (strcmp(name, "hash") == 0) {
        uint64_t mb;
        if (parse_u64(value, &mb)) {
            if (mb < 1)    mb = 1;
            if (mb > 1024) mb = 1024;
            tt_free(&e->searcher.tt);
            tt_init(&e->searcher.tt, (size_t)mb);
            oc_clear(&e->searcher.opening_cache);
        }
    }

    free(name);
    free(value);
}

/* ── command dispatch ── */

/* toks[0] is the command word, toks[1..n) its arguments. */
static void handle_command(UciEngine *e, char **toks, int n) {
    if (n == 0) return;
    const char *cmd = toks[0];

    if (strcmp(cmd, "uci") == 0) {
        printf("id name SnapMind\n");
        printf("id author Hazem Fouda\n");
        printf("option name Hash type spin default 64 min 1 max 1024\n");
        printf("uciok\n");
    }
    else if (strcmp(cmd, "isready") == 0) {
        printf("readyok\n");
    }
    else if (strcmp(cmd, "setoption") == 0) {
        handle_setoption(e, toks + 1, n - 1);
    }
    else if (strcmp(cmd, "ucinewgame") == 0) {
        board_startpos(&e->board);
        u64vec_clear(&e->game_history);
        tt_clear(&e->searcher.tt);
        oc_clear(&e->searcher.opening_cache);
        /* Reset history/killers/countermoves — stale scores from the previous
         * game produce garbage move-ordering in the next one. */
        history_init(&e->searcher.history);
        u64vec_clear(&e->searcher.game_history);
    }
    else if (strcmp(cmd, "position") == 0) {
        handle_position(e, toks + 1, n - 1);
    }
    else if (strcmp(cmd, "go") == 0) {
        handle_go(e, toks + 1, n - 1);
    }
    else if (strcmp(cmd, "stop") == 0) {
        e->searcher.stop      = 1;
        e->searcher.soft_stop = 1;
    }
    else if (strcmp(cmd, "quit") == 0) {
        fflush(stdout);
        exit(0);
    }
    else if (strcmp(cmd, "perft") == 0) {
        uint8_t d;
        if (n > 1 && parse_u8(toks[1], &d)) {
            uint64_t start = now_us();
            uint64_t nodes = perft(&e->board, d);
            uint64_t ms = (now_us() - start) / 1000u;
            if (ms < 1) ms = 1;
            printf("nodes %" PRIu64 " nps %" PRIu64 " time %" PRIu64 "\n", nodes, nodes * 1000u / ms, ms);
        }
    }
    /* anything else: ignored */
}

/* ── input loop ── */

static char  *g_line     = NULL;
static size_t g_line_cap = 0;

/* Reads one line of any length from stdin (without the trailing newline).
 * Returns NULL at end of input. */
static char *read_line(void) {
    if (g_line == NULL) {
        g_line_cap = 4096;
        g_line = (char *)malloc(g_line_cap);
        if (!g_line) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    size_t len = 0;
    for (;;) {
        if (g_line_cap - len < 2) {
            g_line_cap *= 2;
            char *nl = (char *)realloc(g_line, g_line_cap);
            if (!nl) { fprintf(stderr, "out of memory\n"); exit(1); }
            g_line = nl;
        }
        if (fgets(g_line + len, (int)(g_line_cap - len), stdin) == NULL) {
            if (len == 0) return NULL;   /* clean EOF */
            break;                       /* last line without a newline */
        }
        len += strlen(g_line + len);
        if (len > 0 && g_line[len - 1] == '\n') break;
    }
    while (len > 0 && (g_line[len - 1] == '\n' || g_line[len - 1] == '\r')) len--;
    g_line[len] = '\0';
    return g_line;
}

static char **g_toks     = NULL;
static size_t g_toks_cap = 0;

/* Splits `line` in place on whitespace; returns the token count. */
static int tokenize(char *line) {
    size_t len = strlen(line);
    size_t need = len / 2 + 2;
    if (g_toks_cap < need) {
        char **nt = (char **)realloc(g_toks, need * sizeof(char *));
        if (!nt) { fprintf(stderr, "out of memory\n"); exit(1); }
        g_toks = nt;
        g_toks_cap = need;
    }
    int n = 0;
    char *p = line;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        g_toks[n++] = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

void uci_engine_run(UciEngine *e) {
    char *line;
    while ((line = read_line()) != NULL) {
        int n = tokenize(line);
        if (n == 0) continue;      /* blank line */
        handle_command(e, g_toks, n);
        fflush(stdout);
    }
}
