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

/* eval/eval.c — static evaluation (C counterpart of eval/mod_.rs).
 *
 * Tapered evaluation: every term produces a (middlegame, endgame) pair and the
 * two are blended by game phase in evaluate().
 */
#include "eval.h"
#include "pawn_tt.h"
#include "../board/bitboard.h"
#include "../board/movegen.h"
#include "../board/ray_tables.h"

/* Pawn hash table.  Keyed by a Zobrist hash of pawn positions only.  Caches
 * eval_pawn_structure (doubled/isolated/backward/connected) which depends
 * solely on pawn configuration.  eval_passed_pawns is intentionally NOT cached
 * here because it uses square_distance to both kings and would return stale
 * values after any king move even when pawns are unchanged.
 * (The Rust version kept one of these per thread; the C engine is
 * single-threaded, so one global table is enough.) */
static PawnHashTable g_pawn_ht;

void eval_init(void) {
    pawn_ht_init(&g_pawn_ht, 1);
}

typedef struct { int mg; int eg; } Score2;

#define PHASE_KNIGHT 1
#define PHASE_BISHOP 1
#define PHASE_ROOK   2
#define PHASE_QUEEN  4
#define MAX_PHASE    24

#define TEMPO          18
#define RULE50_DIVISOR 199

#define KNIGHT_MOBILITY_MG 5
#define KNIGHT_MOBILITY_EG 4
#define BISHOP_MOBILITY_MG 4
#define BISHOP_MOBILITY_EG 3
#define ROOK_MOBILITY_MG   2
#define ROOK_MOBILITY_EG   3
#define QUEEN_MOBILITY_MG  1
#define QUEEN_MOBILITY_EG  2

#define DOUBLED_PAWN_MG  18
#define DOUBLED_PAWN_EG  28
#define ISOLATED_PAWN_MG 12
#define ISOLATED_PAWN_EG 20

#define ROOK_OPEN_MG 20
#define ROOK_OPEN_EG 10
#define ROOK_SEMI_MG 12
#define ROOK_SEMI_EG  6

#define BISHOP_PAIR_MG 25
#define BISHOP_PAIR_EG 50

#define PASSED_PAWN_BASE_EG 25
#define PASSED_PAWN_RANK_EG 18
#define PASSED_KING_PROX_EG 2
/* Passed pawns contribute in the middlegame too, with a rank-scaled bonus
 * smaller than the EG one. */
#define PASSED_PAWN_BASE_MG  8
#define PASSED_PAWN_RANK_MG  6

/* Backward pawn: no friendly pawn can come to its aid and the advance square
 * is controlled by an enemy pawn — a chronically weak square. */
#define BACKWARD_PAWN_MG 10
#define BACKWARD_PAWN_EG 15

/* Connected / supported pawn. */
#define CONNECTED_PAWN_MG 7
#define CONNECTED_PAWN_EG 5

/* Rook behind passed pawn. */
#define ROOK_BEHIND_PASSER_MG 15
#define ROOK_BEHIND_PASSER_EG 25

/* Outpost bonus: a knight or bishop on a square that no enemy pawn can ever
 * attack (by advancing).  Larger for knights, and larger when the piece is
 * also defended by a friendly pawn. */
#define KNIGHT_OUTPOST_MG      20
#define KNIGHT_OUTPOST_EG      12
#define KNIGHT_OUTPOST_DEF_MG  35
#define KNIGHT_OUTPOST_DEF_EG  20
#define BISHOP_OUTPOST_MG      12
#define BISHOP_OUTPOST_EG       8
#define BISHOP_OUTPOST_DEF_MG  22
#define BISHOP_OUTPOST_DEF_EG  12

/* Threat bonus: positional pressure from our pieces aiming at enemy material. */
#define THREAT_PAWN_VS_MINOR_MG    25
#define THREAT_PAWN_VS_MINOR_EG    20
#define THREAT_PAWN_VS_MAJOR_MG    35
#define THREAT_PAWN_VS_MAJOR_EG    25
#define THREAT_MINOR_VS_MAJOR_MG   18
#define THREAT_MINOR_VS_MAJOR_EG   12
#define THREAT_PIECE_UNDEFENDED_MG 20
#define THREAT_PIECE_UNDEFENDED_EG 25

/* King safety (MG only) */
#define KING_SHIELD_MG    8
#define KING_SHIELD2_MG   4
#define KING_OPEN_FILE_MG 25
#define KING_ATK_MINOR    20
#define KING_ATK_ROOK     35
#define KING_ATK_QUEEN    60

static const uint64_t FILES[8] = {
    0x0101010101010101ULL, 0x0202020202020202ULL, 0x0404040404040404ULL, 0x0808080808080808ULL,
    0x1010101010101010ULL, 0x2020202020202020ULL, 0x4040404040404040ULL, 0x8080808080808080ULL,
};

/* ── Piece-square tables (from White's point of view, a8 = index 0) ───────── */

static const int PAWN_MG[64] = {
       0,    0,    0,    0,    0,    0,    0,    0,
      50,   50,   50,   50,   50,   50,   50,   50,
      10,   10,   20,   30,   30,   20,   10,   10,
       5,    5,   10,   25,   25,   10,    5,    5,
       0,    0,    0,   20,   20,    0,    0,    0,
       5,   -5,  -10,    0,    0,  -10,   -5,    5,
       5,   10,   10,  -20,  -20,   10,   10,    5,
       0,    0,    0,    0,    0,    0,    0,    0,
};

static const int PAWN_EG[64] = {
       0,    0,    0,    0,    0,    0,    0,    0,
     100,  100,  100,  100,  100,  100,  100,  100,
      50,   50,   60,   65,   65,   60,   50,   50,
      25,   25,   30,   40,   40,   30,   25,   25,
      10,   10,   15,   25,   25,   15,   10,   10,
       0,    0,    5,   10,   10,    5,    0,    0,
      -5,   -5,   -5,  -10,  -10,   -5,   -5,   -5,
       0,    0,    0,    0,    0,    0,    0,    0,
};

static const int KNIGHT_MG[64] = {
     -50,  -40,  -30,  -30,  -30,  -30,  -40,  -50,
     -40,  -20,    0,    0,    0,    0,  -20,  -40,
     -30,    0,   10,   15,   15,   10,    0,  -30,
     -30,    5,   15,   20,   20,   15,    5,  -30,
     -30,    0,   15,   20,   20,   15,    0,  -30,
     -30,    5,   10,   15,   15,   10,    5,  -30,
     -40,  -20,    0,    5,    5,    0,  -20,  -40,
     -50,  -40,  -30,  -30,  -30,  -30,  -40,  -50,
};

static const int KNIGHT_EG[64] = {
     -60,  -50,  -40,  -35,  -35,  -40,  -50,  -60,
     -50,  -30,  -10,  -10,  -10,  -10,  -30,  -50,
     -40,  -10,   10,   15,   15,   10,  -10,  -40,
     -35,  -10,   15,   25,   25,   15,  -10,  -35,
     -35,  -10,   15,   25,   25,   15,  -10,  -35,
     -40,  -10,   10,   15,   15,   10,  -10,  -40,
     -50,  -30,  -10,  -10,  -10,  -10,  -30,  -50,
     -60,  -50,  -40,  -35,  -35,  -40,  -50,  -60,
};

static const int BISHOP_MG[64] = {
     -20,  -10,  -10,  -10,  -10,  -10,  -10,  -20,
     -10,    0,    0,    0,    0,    0,    0,  -10,
     -10,    0,    5,   10,   10,    5,    0,  -10,
     -10,    5,    5,   10,   10,    5,    5,  -10,
     -10,    0,   10,   10,   10,   10,    0,  -10,
     -10,   10,   10,   10,   10,   10,   10,  -10,
     -10,    5,    0,    0,    0,    0,    5,  -10,
     -20,  -10,  -10,  -10,  -10,  -10,  -10,  -20,
};

static const int BISHOP_EG[64] = {
     -20,  -15,  -10,  -10,  -10,  -10,  -15,  -20,
     -15,   -5,    0,    5,    5,    0,   -5,  -15,
     -10,    0,    5,   10,   10,    5,    0,  -10,
     -10,    5,   10,   15,   15,   10,    5,  -10,
     -10,    5,   10,   15,   15,   10,    5,  -10,
     -10,    0,    5,   10,   10,    5,    0,  -10,
     -15,   -5,    0,    5,    5,    0,   -5,  -15,
     -20,  -15,  -10,  -10,  -10,  -10,  -15,  -20,
};

static const int ROOK_MG[64] = {
       0,    0,    0,    0,    0,    0,    0,    0,
       5,   10,   10,   10,   10,   10,   10,    5,
      -5,    0,    0,    0,    0,    0,    0,   -5,
      -5,    0,    0,    0,    0,    0,    0,   -5,
      -5,    0,    0,    0,    0,    0,    0,   -5,
      -5,    0,    0,    0,    0,    0,    0,   -5,
      -5,    0,    0,    0,    0,    0,    0,   -5,
       0,    0,    0,    5,    5,    0,    0,    0,
};

static const int ROOK_EG[64] = {
       5,    5,    5,    5,    5,    5,    5,    5,
       5,   10,   10,   10,   10,   10,   10,    5,
       0,    5,    5,    5,    5,    5,    5,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
       0,    5,    5,    5,    5,    5,    5,    0,
       5,   10,   10,   10,   10,   10,   10,    5,
       5,    5,    5,    5,    5,    5,    5,    5,
};

static const int QUEEN_MG[64] = {
     -20,  -10,  -10,   -5,   -5,  -10,  -10,  -20,
     -10,    0,    0,    0,    0,    0,    0,  -10,
     -10,    0,    5,    5,    5,    5,    0,  -10,
      -5,    0,    5,    5,    5,    5,    0,   -5,
       0,    0,    5,    5,    5,    5,    0,   -5,
     -10,    5,    5,    5,    5,    5,    0,  -10,
     -10,    0,    5,    0,    0,    0,    0,  -10,
     -20,  -10,  -10,   -5,   -5,  -10,  -10,  -20,
};

static const int QUEEN_EG[64] = {
     -30,  -20,  -10,   -5,   -5,  -10,  -20,  -30,
     -20,  -10,    0,    5,    5,    0,  -10,  -20,
     -10,    0,   10,   15,   15,   10,    0,  -10,
      -5,    5,   15,   20,   20,   15,    5,   -5,
      -5,    5,   15,   20,   20,   15,    5,   -5,
     -10,    0,   10,   15,   15,   10,    0,  -10,
     -20,  -10,    0,    5,    5,    0,  -10,  -20,
     -30,  -20,  -10,   -5,   -5,  -10,  -20,  -30,
};

static const int KING_MG[64] = {
     -30,  -40,  -40,  -50,  -50,  -40,  -40,  -30,
     -30,  -40,  -40,  -50,  -50,  -40,  -40,  -30,
     -30,  -40,  -40,  -50,  -50,  -40,  -40,  -30,
     -30,  -40,  -40,  -50,  -50,  -40,  -40,  -30,
     -20,  -30,  -30,  -40,  -40,  -30,  -30,  -20,
     -10,  -20,  -20,  -20,  -20,  -20,  -20,  -10,
      20,   20,    0,    0,    0,    0,   20,   20,
      20,   30,   10,    0,    0,   10,   30,   20,
};

static const int KING_EG[64] = {
     -50,  -40,  -30,  -20,  -20,  -30,  -40,  -50,
     -30,  -20,  -10,    0,    0,  -10,  -20,  -30,
     -30,  -10,   20,   30,   30,   20,  -10,  -30,
     -30,  -10,   30,   40,   40,   30,  -10,  -30,
     -30,  -10,   30,   40,   40,   30,  -10,  -30,
     -30,  -10,   20,   30,   30,   20,  -10,  -30,
     -30,  -30,    0,    0,    0,    0,  -30,  -30,
     -50,  -30,  -30,  -30,  -30,  -30,  -30,  -50,
};



static uint64_t adjacent_files(int file) {
    uint64_t mask = 0;
    if (file > 0) mask |= FILES[file - 1];
    if (file < 7) mask |= FILES[file + 1];
    return mask;
}

static int material_side(const Board *board, Color color) {
    return PAWN_VAL   * popcount64(board_bb(board, color, PT_PAWN))
         + KNIGHT_VAL * popcount64(board_bb(board, color, PT_KNIGHT))
         + BISHOP_VAL * popcount64(board_bb(board, color, PT_BISHOP))
         + ROOK_VAL   * popcount64(board_bb(board, color, PT_ROOK))
         + QUEEN_VAL  * popcount64(board_bb(board, color, PT_QUEEN));
}

int simple_eval(const Board *board) {
    int score = material_side(board, COLOR_WHITE) - material_side(board, COLOR_BLACK);
    return board->side_to_move == COLOR_WHITE ? score : -score;
}

static int compute_phase(const Board *board) {
    int phase = 0;
    for (int c = 0; c < 2; c++) {
        Color color = (Color)c;
        phase += popcount64(board_bb(board, color, PT_KNIGHT)) * PHASE_KNIGHT;
        phase += popcount64(board_bb(board, color, PT_BISHOP)) * PHASE_BISHOP;
        phase += popcount64(board_bb(board, color, PT_ROOK))   * PHASE_ROOK;
        phase += popcount64(board_bb(board, color, PT_QUEEN))  * PHASE_QUEEN;
    }
    return phase < MAX_PHASE ? phase : MAX_PHASE;
}

static void add_piece(int *mg, int *eg, const Board *board, Color color, PieceType piece,
                      int value, const int *pst_mg, const int *pst_eg) {
    Bitboard bb = board_bb(board, color, piece);
    while (bb != 0) {
        int sq = ctz64(bb);
        int pst_sq = (color == COLOR_WHITE) ? (7 - sq / 8) * 8 + (sq % 8) : sq;
        *mg += value + pst_mg[pst_sq];
        *eg += value + pst_eg[pst_sq];
        bb &= bb - 1;
    }
}

/* ── King safety: pawn shield + open files + attacker weight. MG only. ───── */
static Score2 eval_king_safety(const Board *board, Color color, int phase) {
    Score2 zero = {0, 0};
    if (phase == 0) return zero;

    Bitboard king_bb = board_bb(board, color, PT_KING);
    if (king_bb == 0) return zero;
    int king_sq   = ctz64(king_bb);
    int king_file = file_of(king_sq);
    int king_rank = rank_of(king_sq);

    Color    opp       = color_flip(color);
    Bitboard occ       = board_occupied(board);
    Bitboard our_pawns = board_bb(board, color, PT_PAWN);

    /* When the king is on rank 6 (White) or rank 1 (Black), both shield tiers
     * would resolve to the same rank; use the sentinel value 8 (off-board) for
     * the second tier when it would collide with the first. */
    int shield_rank, shield2_rank;
    if (color == COLOR_WHITE) {
        int r1 = king_rank + 1; if (r1 > 7) r1 = 7;
        int r2 = king_rank + 2; if (r2 > 7) r2 = 7;
        shield_rank  = r1;
        shield2_rank = (r2 == r1) ? 8 : r2;
    } else {
        int r1 = king_rank > 0 ? king_rank - 1 : 0;   /* saturating_sub(1) */
        int r2 = king_rank > 1 ? king_rank - 2 : 0;   /* saturating_sub(2) */
        shield_rank  = r1;
        shield2_rank = (r2 == r1) ? 8 : r2;
    }

    int shield_score = 0;
    for (int df = 0; df <= 2; df++) {
        int f = king_file - 1 + df;
        if (f < 0 || f > 7) continue;

        int sq1 = shield_rank * 8 + f;
        int sq2_valid = shield2_rank < 8;
        int sq2 = sq2_valid ? shield2_rank * 8 + f : 0;

        if ((our_pawns & bit(sq1)) != 0) {
            shield_score += KING_SHIELD_MG;
        } else if (sq2_valid && (our_pawns & bit(sq2)) != 0) {
            shield_score += KING_SHIELD2_MG;
        } else if ((our_pawns & FILES[f]) == 0) {
            shield_score -= KING_OPEN_FILE_MG;
        }
    }

    Bitboard king_zone = king_attacks(king_sq) | king_bb;
    int total_weight   = 0;
    int attacker_count = 0;

    Bitboard opp_knights = board_bb(board, opp, PT_KNIGHT);
    while (opp_knights != 0) {
        int sq = ctz64(opp_knights);
        if ((knight_attacks(sq) & king_zone) != 0) {
            total_weight += KING_ATK_MINOR; attacker_count += 1;
        }
        opp_knights &= opp_knights - 1;
    }

    Bitboard opp_bishops = board_bb(board, opp, PT_BISHOP);
    while (opp_bishops != 0) {
        int sq = ctz64(opp_bishops);
        if ((bishop_attacks(sq, occ) & king_zone) != 0) {
            total_weight += KING_ATK_MINOR; attacker_count += 1;
        }
        opp_bishops &= opp_bishops - 1;
    }

    Bitboard opp_rooks = board_bb(board, opp, PT_ROOK);
    while (opp_rooks != 0) {
        int sq = ctz64(opp_rooks);
        if ((rook_attacks(sq, occ) & king_zone) != 0) {
            total_weight += KING_ATK_ROOK; attacker_count += 1;
        }
        opp_rooks &= opp_rooks - 1;
    }

    Bitboard opp_queens = board_bb(board, opp, PT_QUEEN);
    while (opp_queens != 0) {
        int sq = ctz64(opp_queens);
        if (((bishop_attacks(sq, occ) | rook_attacks(sq, occ)) & king_zone) != 0) {
            total_weight += KING_ATK_QUEEN; attacker_count += 1;
        }
        opp_queens &= opp_queens - 1;
    }

    /* A lone attacker gets a partial (1/4 weight) penalty. */
    int attack_penalty;
    if (attacker_count >= 2)       attack_penalty = total_weight * attacker_count / 2;
    else if (attacker_count == 1)  attack_penalty = total_weight / 4;
    else                           attack_penalty = 0;

    /* Return the raw score; tapering happens exactly once in evaluate(). */
    Score2 r = { shield_score - attack_penalty, 0 };
    return r;
}

static Score2 eval_bishop_pair(const Board *board, Color color) {
    /* A genuine bishop pair requires one bishop on light squares and one on
     * dark squares.  Two same-colour bishops (possible after promotion) do NOT
     * cover both square colours and get no bonus. */
    const uint64_t DARK_SQUARES  = 0xAA55AA55AA55AA55ULL;
    const uint64_t LIGHT_SQUARES = ~DARK_SQUARES;
    Bitboard bishops = board_bb(board, color, PT_BISHOP);
    Score2 r = {0, 0};
    if ((bishops & DARK_SQUARES) != 0 && (bishops & LIGHT_SQUARES) != 0) {
        r.mg = BISHOP_PAIR_MG;
        r.eg = BISHOP_PAIR_EG;
    }
    return r;
}

/* Bitboard of all passed pawns for `color`.  A pawn is passed only if
 *   (a) no enemy pawn occupies the same or adjacent files ahead of it, AND
 *   (b) no FRIENDLY pawn blocks the same file ahead of it. */
static uint64_t passed_pawns_bb(uint64_t our_pawns, uint64_t opp_pawns, Color color) {
    uint64_t passed = 0;
    uint64_t bb     = our_pawns;
    while (bb != 0) {
        int sq   = ctz64(bb);
        int file = file_of(sq);
        int rank = rank_of(sq);

        /* "Passer mask": all squares on the same and adjacent files strictly
         * ahead of this pawn (in the direction of promotion). */
        uint64_t mask = 0;
        int r_lo, r_hi;
        if (color == COLOR_WHITE) {
            r_lo = rank + 1;
            r_hi = 7;
        } else {
            r_lo = 0;
            r_hi = rank > 0 ? rank - 1 : 0;   /* 0 ..= rank.saturating_sub(1) */
        }
        for (int r = r_lo; r <= r_hi; r++) {
            if (file > 0) mask |= bit(r * 8 + file - 1);
            mask |= bit(r * 8 + file);
            if (file < 7) mask |= bit(r * 8 + file + 1);
        }

        uint64_t own_file_ahead = our_pawns & mask & FILES[file];
        if ((opp_pawns & mask) == 0 && own_file_ahead == 0) {
            passed |= bit(sq);
        }
        bb &= bb - 1;
    }
    return passed;
}

static Score2 eval_passed_pawns(const Board *board, Color color) {
    Color opp = color_flip(color);
    uint64_t our_pawns = board_bb(board, color, PT_PAWN);
    uint64_t opp_pawns = board_bb(board, opp,   PT_PAWN);

    int our_king_sq, opp_king_sq;
    {
        uint64_t bb = board_bb(board, color, PT_KING);
        our_king_sq = (bb == 0) ? 0 : ctz64(bb);
    }
    {
        uint64_t bb = board_bb(board, opp, PT_KING);
        opp_king_sq = (bb == 0) ? 0 : ctz64(bb);
    }

    int mg_score = 0;
    int eg_score = 0;
    uint64_t passed = passed_pawns_bb(our_pawns, opp_pawns, color);

    while (passed != 0) {
        int sq   = ctz64(passed);
        int rank = rank_of(sq);

        /* Advancement relative to the pawn's own starting side. */
        int advance = (color == COLOR_WHITE) ? rank : 7 - rank;

        int base_eg = PASSED_PAWN_BASE_EG + advance * PASSED_PAWN_RANK_EG;
        int base_mg = PASSED_PAWN_BASE_MG + advance * PASSED_PAWN_RANK_MG;

        int stop = (color == COLOR_WHITE) ? sq + 8 : sq - 8;
        if (stop >= 0 && stop < 64 && (board_occupied(board) & bit(stop))) {
            base_mg -= base_mg / 4;
            base_eg -= base_eg / 4;
        }

        int our_dist = square_distance(our_king_sq, sq);
        int opp_dist = square_distance(opp_king_sq, sq);
        /* King proximity bonus: our king close to the passer helps escort it;
         * enemy king close penalises. */
        int prox = (opp_dist - our_dist) * PASSED_KING_PROX_EG * advance;

        mg_score += base_mg;
        eg_score += base_eg + prox;

        passed &= passed - 1;
    }
    Score2 r = { mg_score, eg_score };
    return r;
}

/* Squares attacked by pawns of `pawn_color` given their bitboard. */
static inline uint64_t pawn_attack_map(uint64_t pawns, Color pawn_color) {
    if (pawn_color == COLOR_WHITE) return ((pawns << 9) & ~FILE_A) | ((pawns << 7) & ~FILE_H);
    return ((pawns >> 7) & ~FILE_A) | ((pawns >> 9) & ~FILE_H);
}

static Score2 eval_mobility(const Board *board, Color color) {
    uint64_t occ  = board_occupied(board);
    uint64_t mine = board_occupied_by(board, color);

    /* Safe-mobility mask: squares not controlled by enemy pawns (applied only
     * to knights and bishops). */
    uint64_t enemy_pawn_atk;
    {
        Color opp = color_flip(color);
        uint64_t pawns = board_bb(board, opp, PT_PAWN);
        enemy_pawn_atk = pawn_attack_map(pawns, opp);
    }

    /* PIN AWARENESS: absolutely-pinned pieces contribute 0 mobility. */
    uint64_t pinned = pinned_pieces(board, color);

    int mg = 0;
    int eg = 0;

    uint64_t knights = board_bb(board, color, PT_KNIGHT);
    while (knights != 0) {
        int sq = ctz64(knights);
        if ((bit(sq) & pinned) == 0) {
            uint64_t safe = knight_attacks(sq) & ~mine & ~enemy_pawn_atk;
            mg += popcount64(safe) * KNIGHT_MOBILITY_MG;
            eg += popcount64(safe) * KNIGHT_MOBILITY_EG;
        }
        knights &= knights - 1;
    }

    uint64_t bishops = board_bb(board, color, PT_BISHOP);
    while (bishops != 0) {
        int sq = ctz64(bishops);
        if ((bit(sq) & pinned) == 0) {
            uint64_t safe = bishop_attacks(sq, occ) & ~mine & ~enemy_pawn_atk;
            mg += popcount64(safe) * BISHOP_MOBILITY_MG;
            eg += popcount64(safe) * BISHOP_MOBILITY_EG;
        }
        bishops &= bishops - 1;
    }

    /* Rooks and queens: full mobility. */
    uint64_t rooks = board_bb(board, color, PT_ROOK);
    while (rooks != 0) {
        int sq = ctz64(rooks);
        if ((bit(sq) & pinned) == 0) {
            int n = popcount64(rook_attacks(sq, occ) & ~mine);
            mg += n * ROOK_MOBILITY_MG;
            eg += n * ROOK_MOBILITY_EG;
        }
        rooks &= rooks - 1;
    }

    uint64_t queens = board_bb(board, color, PT_QUEEN);
    while (queens != 0) {
        int sq = ctz64(queens);
        if ((bit(sq) & pinned) == 0) {
            uint64_t atk = (bishop_attacks(sq, occ) | rook_attacks(sq, occ)) & ~mine;
            int n = popcount64(atk);
            mg += n * QUEEN_MOBILITY_MG;
            eg += n * QUEEN_MOBILITY_EG;
        }
        queens &= queens - 1;
    }

    Score2 r = { mg, eg };
    return r;
}

static Score2 eval_pawn_structure(const Board *board, Color color) {
    /* Pawn structure depends only on pawn positions — cache in the pawn hash
     * table.  All terms below (doubled, isolated, backward, connected) are
     * purely pawn-position-dependent, so a cache hit is always valid. */
    uint64_t raw_key    = pawn_hash(board);
    uint64_t color_salt = (color == COLOR_WHITE) ? 0ULL : ~0ULL;
    uint64_t key        = raw_key ^ color_salt;

    const PawnEntry *hit = pawn_ht_probe(&g_pawn_ht, key);
    if (hit != NULL) {
        Score2 r = { hit->mg, hit->eg };
        return r;
    }

    Color    opp       = color_flip(color);
    uint64_t our_pawns = board_bb(board, color, PT_PAWN);
    uint64_t opp_pawns = board_bb(board, opp,   PT_PAWN);
    int mg = 0;
    int eg = 0;

    /* --- Doubled and isolated penalties --- */
    for (int file = 0; file < 8; file++) {
        int count = popcount64(our_pawns & FILES[file]);
        if (count == 0) continue;

        if (count > 1) {
            int extra = count - 1;
            mg -= DOUBLED_PAWN_MG * extra;
            eg -= DOUBLED_PAWN_EG * extra;
        }

        if ((our_pawns & adjacent_files(file)) == 0) {
            mg -= ISOLATED_PAWN_MG * count;
            eg -= ISOLATED_PAWN_EG * count;
        }
    }

    /* --- Backward pawn penalty ---
     * A pawn is backward when (1) no friendly pawn on adjacent files is at the
     * same rank or behind it, AND (2) the stop square is controlled by an enemy
     * pawn. */
    uint64_t opp_pawn_atk;
    if (color == COLOR_WHITE) {
        /* Our side is White → enemy is Black → Black pawns attack downward (>>) */
        opp_pawn_atk = ((opp_pawns >> 7) & ~FILE_A) | ((opp_pawns >> 9) & ~FILE_H);
    } else {
        /* Our side is Black → enemy is White → White pawns attack upward (<<) */
        opp_pawn_atk = ((opp_pawns << 9) & ~FILE_A) | ((opp_pawns << 7) & ~FILE_H);
    }

    uint64_t bb = our_pawns;
    while (bb != 0) {
        int sq   = ctz64(bb);
        int file = file_of(sq);
        int rank = rank_of(sq);

        /* Rank mask covering this pawn's rank and all ranks behind it. */
        uint64_t ranks_at_or_behind;
        if (color == COLOR_WHITE) {
            /* ranks 0 ..= rank  →  bits 0 .. (rank+1)*8 */
            if (rank == 7) ranks_at_or_behind = ~0ULL;
            else           ranks_at_or_behind = (1ULL << ((rank + 1) * 8)) - 1;
        } else {
            /* ranks rank ..= 7  →  bits rank*8 .. 63 */
            ranks_at_or_behind = ~0ULL << (rank * 8);
        }

        uint64_t support_span = adjacent_files(file) & ranks_at_or_behind;
        if ((our_pawns & support_span) == 0) {
            /* No friendly pawn can advance to protect this pawn.  Penalise if
             * the stop square is controlled by an enemy pawn. */
            int stop = -1;
            if (color == COLOR_WHITE) { if (rank < 7) stop = (rank + 1) * 8 + file; }
            else                      { if (rank > 0) stop = (rank - 1) * 8 + file; }
            if (stop >= 0) {
                if ((bit(stop) & opp_pawn_atk) != 0) {
                    mg -= BACKWARD_PAWN_MG;
                    eg -= BACKWARD_PAWN_EG;
                }
            }
        }
        bb &= bb - 1;
    }

    /* --- Connected / supported pawn bonus --- */
    bb = our_pawns;
    while (bb != 0) {
        int sq   = ctz64(bb);
        int file = file_of(sq);
        int rank = rank_of(sq);

        uint64_t connected_mask = 0;

        /* Same rank, adjacent files (pawn chain). */
        if (file > 0) connected_mask |= bit(rank * 8 + file - 1);
        if (file < 7) connected_mask |= bit(rank * 8 + file + 1);

        /* One rank behind on adjacent files (a pawn defending from the rear). */
        int behind_rank = -1;
        if (color == COLOR_WHITE) { if (rank > 0) behind_rank = rank - 1; }
        else                      { if (rank < 7) behind_rank = rank + 1; }
        if (behind_rank >= 0) {
            if (file > 0) connected_mask |= bit(behind_rank * 8 + file - 1);
            if (file < 7) connected_mask |= bit(behind_rank * 8 + file + 1);
        }

        if ((our_pawns & connected_mask) != 0) {
            mg += CONNECTED_PAWN_MG;
            eg += CONNECTED_PAWN_EG;
        }
        bb &= bb - 1;
    }

    pawn_ht_store(&g_pawn_ht, key, mg, eg);
    Score2 r = { mg, eg };
    return r;
}

static Score2 eval_rook_files(const Board *board, Color color) {
    Color    opp       = color_flip(color);
    uint64_t our_pawns = board_bb(board, color, PT_PAWN);
    uint64_t opp_pawns = board_bb(board, opp,   PT_PAWN);
    uint64_t rooks     = board_bb(board, color, PT_ROOK);
    int mg = 0;
    int eg = 0;

    /* Pre-compute passed pawns once for the rook-behind-passer check. */
    uint64_t passed = passed_pawns_bb(our_pawns, opp_pawns, color);

    while (rooks != 0) {
        int sq        = ctz64(rooks);
        int file      = file_of(sq);
        int rook_rank = rank_of(sq);
        int no_own    = (our_pawns & FILES[file]) == 0;
        int no_opp    = (opp_pawns & FILES[file]) == 0;

        if (no_own && no_opp)  { mg += ROOK_OPEN_MG; eg += ROOK_OPEN_EG; }
        else if (no_own)       { mg += ROOK_SEMI_MG; eg += ROOK_SEMI_EG; }

        /* Rook behind a passed pawn. */
        uint64_t passers_on_file = passed & FILES[file];
        if (passers_on_file != 0) {
            int is_behind;
            if (color == COLOR_WHITE) {
                /* Any passed pawn on this file at a rank above the rook. */
                if (rook_rank >= 7) is_behind = 0;
                else {
                    uint64_t above = ~0ULL << ((rook_rank + 1) * 8);
                    is_behind = (passers_on_file & above) != 0;
                }
            } else {
                /* Any passed pawn on this file at a rank below the rook. */
                if (rook_rank == 0) is_behind = 0;
                else {
                    uint64_t below = (1ULL << (rook_rank * 8)) - 1;
                    is_behind = (passers_on_file & below) != 0;
                }
            }
            if (is_behind) {
                mg += ROOK_BEHIND_PASSER_MG;
                eg += ROOK_BEHIND_PASSER_EG;
            }
        }

        rooks &= rooks - 1;
    }

    Score2 r = { mg, eg };
    return r;
}

/* ── Outpost evaluation ──
 * Rewards knights and bishops that sit on squares which no enemy pawn can ever
 * attack by advancing, in the opponent's half of the board (rank >= 4 for
 * White, rank <= 3 for Black).  A friendly pawn defending the square earns a
 * larger bonus. */
static int enemy_can_attack_square(uint64_t opp_pawns, Color color, int rank, int file) {
    uint64_t adj = adjacent_files(file);
    if (color == COLOR_WHITE) {
        /* Any Black pawn on adj file at a rank strictly above sq can
         * eventually descend to attack this square. */
        uint64_t ranks_above = (rank >= 7) ? 0ULL : (~0ULL << ((rank + 1) * 8));
        return (opp_pawns & adj & ranks_above) != 0;
    } else {
        /* Any White pawn on adj file at a rank strictly below sq can
         * eventually ascend to attack this square. */
        uint64_t ranks_below = (1ULL << (rank * 8)) - 1;
        return (opp_pawns & adj & ranks_below) != 0;
    }
}

static Score2 eval_outposts(const Board *board, Color color) {
    Color    opp       = color_flip(color);
    uint64_t opp_pawns = board_bb(board, opp,   PT_PAWN);
    uint64_t our_pawns = board_bb(board, color, PT_PAWN);

    /* Squares currently attacked (defended) by our own pawns. */
    uint64_t our_pawn_atk = pawn_attack_map(our_pawns, color);

    int mg = 0;
    int eg = 0;

    /* ── Knights ── */
    uint64_t knights = board_bb(board, color, PT_KNIGHT);
    while (knights != 0) {
        int sq   = ctz64(knights);
        int rank = rank_of(sq);
        int file = file_of(sq);

        int in_opp_half = (color == COLOR_WHITE) ? (rank >= 4) : (rank <= 3);
        if (in_opp_half) {
            if (!enemy_can_attack_square(opp_pawns, color, rank, file)) {
                int defended = (bit(sq) & our_pawn_atk) != 0;
                if (defended) { mg += KNIGHT_OUTPOST_DEF_MG; eg += KNIGHT_OUTPOST_DEF_EG; }
                else          { mg += KNIGHT_OUTPOST_MG;     eg += KNIGHT_OUTPOST_EG;     }
            }
        }
        knights &= knights - 1;
    }

    /* ── Bishops ── */
    uint64_t bishops = board_bb(board, color, PT_BISHOP);
    while (bishops != 0) {
        int sq   = ctz64(bishops);
        int rank = rank_of(sq);
        int file = file_of(sq);

        int in_opp_half = (color == COLOR_WHITE) ? (rank >= 4) : (rank <= 3);
        if (in_opp_half) {
            if (!enemy_can_attack_square(opp_pawns, color, rank, file)) {
                int defended = (bit(sq) & our_pawn_atk) != 0;
                if (defended) { mg += BISHOP_OUTPOST_DEF_MG; eg += BISHOP_OUTPOST_DEF_EG; }
                else          { mg += BISHOP_OUTPOST_MG;     eg += BISHOP_OUTPOST_EG;     }
            }
        }
        bishops &= bishops - 1;
    }

    Score2 r = { mg, eg };
    return r;
}

/* ── Threat evaluation ──
 * Four terms: pawn attacks minor, pawn attacks major, minor attacks major, and
 * "hanging" (attacked by us and not pawn-defended) enemy pieces. */
static Score2 eval_threats(const Board *board, Color color) {
    Color    opp = color_flip(color);
    uint64_t occ = board_occupied(board);

    /* Our pawn attack map */
    uint64_t our_pawns    = board_bb(board, color, PT_PAWN);
    uint64_t our_pawn_atk = pawn_attack_map(our_pawns, color);

    /* Our minor-piece attack map (knights + bishops) */
    uint64_t our_minor_atk = 0;
    uint64_t kn = board_bb(board, color, PT_KNIGHT);
    while (kn != 0) {
        int sq = ctz64(kn);
        our_minor_atk |= knight_attacks(sq);
        kn &= kn - 1;
    }
    uint64_t bi = board_bb(board, color, PT_BISHOP);
    while (bi != 0) {
        int sq = ctz64(bi);
        our_minor_atk |= bishop_attacks(sq, occ);
        bi &= bi - 1;
    }

    /* Enemy pawn defense map */
    uint64_t opp_pawns    = board_bb(board, opp, PT_PAWN);
    uint64_t opp_pawn_def = pawn_attack_map(opp_pawns, opp);

    /* Target bitboards */
    uint64_t opp_minors = board_bb(board, opp, PT_KNIGHT) | board_bb(board, opp, PT_BISHOP);
    uint64_t opp_majors = board_bb(board, opp, PT_ROOK)   | board_bb(board, opp, PT_QUEEN);
    uint64_t opp_pieces = opp_minors | opp_majors;

    int mg = 0;
    int eg = 0;

    /* Term 1 & 2: our pawn attacks a non-pawn enemy piece. */
    int pawn_vs_minor = popcount64(our_pawn_atk & opp_minors);
    int pawn_vs_major = popcount64(our_pawn_atk & opp_majors);
    mg += pawn_vs_minor * THREAT_PAWN_VS_MINOR_MG;
    eg += pawn_vs_minor * THREAT_PAWN_VS_MINOR_EG;
    mg += pawn_vs_major * THREAT_PAWN_VS_MAJOR_MG;
    eg += pawn_vs_major * THREAT_PAWN_VS_MAJOR_EG;

    /* Term 3: our minor attacks an enemy major. */
    int minor_vs_major = popcount64(our_minor_atk & opp_majors);
    mg += minor_vs_major * THREAT_MINOR_VS_MAJOR_MG;
    eg += minor_vs_major * THREAT_MINOR_VS_MAJOR_EG;

    /* Term 4: any enemy non-pawn piece attacked by us but not pawn-defended. */
    uint64_t our_all_atk = our_pawn_atk | our_minor_atk;
    int undefended_targets = popcount64(opp_pieces & our_all_atk & ~opp_pawn_def);
    mg += undefended_targets * THREAT_PIECE_UNDEFENDED_MG;
    eg += undefended_targets * THREAT_PIECE_UNDEFENDED_EG;

    Score2 r = { mg, eg };
    return r;
}

static Score2 eval_side(const Board *board, Color color, int phase) {
    int mg = 0;
    int eg = 0;

    add_piece(&mg, &eg, board, color, PT_PAWN,   PAWN_VAL,   PAWN_MG,   PAWN_EG);
    add_piece(&mg, &eg, board, color, PT_KNIGHT, KNIGHT_VAL, KNIGHT_MG, KNIGHT_EG);
    add_piece(&mg, &eg, board, color, PT_BISHOP, BISHOP_VAL, BISHOP_MG, BISHOP_EG);
    add_piece(&mg, &eg, board, color, PT_ROOK,   ROOK_VAL,   ROOK_MG,   ROOK_EG);
    add_piece(&mg, &eg, board, color, PT_QUEEN,  QUEEN_VAL,  QUEEN_MG,  QUEEN_EG);
    add_piece(&mg, &eg, board, color, PT_KING,   KING_VAL,   KING_MG,   KING_EG);

    Score2 bp = eval_bishop_pair(board, color);
    mg += bp.mg; eg += bp.eg;

    /* Passed pawns contribute to both MG and EG. */
    Score2 pp = eval_passed_pawns(board, color);
    mg += pp.mg; eg += pp.eg;

    Score2 mob = eval_mobility(board, color);
    mg += mob.mg; eg += mob.eg;

    Score2 ps = eval_pawn_structure(board, color);
    mg += ps.mg; eg += ps.eg;

    Score2 rf = eval_rook_files(board, color);
    mg += rf.mg; eg += rf.eg;

    Score2 ks = eval_king_safety(board, color, phase);
    mg += ks.mg;

    Score2 op = eval_outposts(board, color);
    mg += op.mg; eg += op.eg;

    Score2 th = eval_threats(board, color);
    mg += th.mg; eg += th.eg;

    Score2 r = { mg, eg };
    return r;
}

int evaluate(const Board *board) {
    int phase = compute_phase(board);

    Score2 w = eval_side(board, COLOR_WHITE, phase);
    Score2 b = eval_side(board, COLOR_BLACK, phase);

    int mg = w.mg - b.mg;
    int eg = w.eg - b.eg;

    int v = (mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE;

    /* Apply the rule-50 decay BEFORE adding TEMPO: TEMPO is a fixed small bonus
     * for the right to move and must not be scaled down as the clock rises. */
    v -= v * (int)board->halfmove_clock / RULE50_DIVISOR;
    int tempo_bonus = (board->side_to_move == COLOR_WHITE) ? TEMPO : -TEMPO;
    v += tempo_bonus;

    return (board->side_to_move == COLOR_WHITE) ? v : -v;
}
