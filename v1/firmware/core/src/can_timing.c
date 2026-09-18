/* See can_timing.h. */

#include "can_timing.h"

#include <stdlib.h>

static int abs_i(int v) { return v < 0 ? -v : v; }

/* Time quanta beyond this buy nothing, and below it resolution is the binding
 * constraint -- see the ranking comment in can_timing_solve(). */
#define RESOLUTION_ENOUGH_TQ 16

static uint32_t min_tq(uint32_t nbt) {
    return nbt < RESOLUTION_ENOUGH_TQ ? nbt : RESOLUTION_ENOUGH_TQ;
}

static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

int can_timing_max_tolerance_ppm(const can_timing_t *t) {
    if (t == NULL || !t->valid || t->nominal_bt_tq == 0) {
        return 0;
    }

    const int64_t nbt = (int64_t)t->nominal_bt_tq;
    const int64_t ps1 = (int64_t)t->tseg1;
    const int64_t ps2 = (int64_t)t->tseg2;
    const int64_t sjw = (int64_t)t->sjw;

    /* df <= SJW / (2 * 10 * NBT) */
    const int64_t term_sjw = (sjw * 1000000) / (20 * nbt);

    /* df <= min(PS1, PS2) / (2 * (13 * NBT - PS2)) */
    const int64_t denom = 2 * (13 * nbt - ps2);
    const int64_t smaller = (ps1 < ps2) ? ps1 : ps2;
    const int64_t term_phase = (denom > 0) ? (smaller * 1000000) / denom : 0;

    const int64_t worst = (term_sjw < term_phase) ? term_sjw : term_phase;
    return (int)worst;
}

bool can_timing_osc_ok(const can_timing_t *t, int osc_ppm) {
    return can_timing_max_tolerance_ppm(t) >= osc_ppm;
}

can_timing_t can_timing_solve(uint32_t clock_hz, uint32_t bitrate_bps,
                              int sample_point_permil, bool data_phase) {
    can_timing_t best = {0};
    best.valid = false;

    if (clock_hz == 0 || bitrate_bps == 0) {
        return best;
    }
    if (sample_point_permil <= 0 || sample_point_permil >= 1000) {
        sample_point_permil = 875; /* CiA's recommendation */
    }

    const uint32_t brp_max = data_phase ? FDCAN_DBRP_MAX : FDCAN_NBRP_MAX;
    const uint32_t tseg1_max = data_phase ? FDCAN_DTSEG1_MAX : FDCAN_NTSEG1_MAX;
    const uint32_t tseg2_max = data_phase ? FDCAN_DTSEG2_MAX : FDCAN_NTSEG2_MAX;
    const uint32_t sjw_max = data_phase ? FDCAN_DSJW_MAX : FDCAN_NSJW_MAX;

    int best_rate_err = 0;
    int best_sp_err = 0;
    int best_tol = 0;
    uint32_t best_res = 0;

    for (uint32_t brp = 1; brp <= brp_max; brp++) {
        const uint32_t floor_tq = clock_hz / (brp * bitrate_bps);
        if (floor_tq < 3) {
            break; /* prescaler is already too large; more will not help */
        }

        /* Both roundings of the time quanta count, not just the one that
         * truncating division happens to give.
         *
         * When the bit rate divides the kernel clock exactly there is only
         * one candidate and the second iteration is wasted work.  When it
         * does not -- 800 kbit/s at 170 MHz wants 212.5 tq -- rounding up is
         * sometimes the closer of the two, and a solver that only ever rounds
         * down silently returns the worse of two available answers.  The
         * cross-check against tools/can_timing.py's exhaustive search is what
         * caught this; it was costing about 10 ppm on inexact rates. */
        for (int round_up = 0; round_up <= 1; round_up++) {
            const uint32_t tq_per_bit = floor_tq + (uint32_t)round_up;
            if (tq_per_bit < 4 || tq_per_bit > 1 + tseg1_max + tseg2_max) {
                continue;
            }
            if (round_up && (clock_hz % (brp * bitrate_bps)) == 0) {
                continue; /* exact: the second rounding is the same candidate */
            }

            /* Sample point splits the bit: sync (1 tq) + tseg1, then tseg2. */
            uint32_t tseg1 =
                (uint32_t)(((uint64_t)tq_per_bit * sample_point_permil) /
                           1000) -
                1;
            if (tseg1 < 1) {
                tseg1 = 1;
            }
            if (tseg1 > tseg1_max) {
                tseg1 = tseg1_max;
            }

            /* Try the ideal split and its neighbours: rounding can put the true
             * best sample point one tq either side. */
            for (int delta = -1; delta <= 1; delta++) {
                const int64_t t1 = (int64_t)tseg1 + delta;
                if (t1 < 1 || t1 > (int64_t)tseg1_max) {
                    continue;
                }
                const int64_t t2 = (int64_t)tq_per_bit - 1 - t1;
                if (t2 < 1 || t2 > (int64_t)tseg2_max) {
                    continue;
                }

                can_timing_t cand = {0};
                cand.brp = brp;
                cand.tseg1 = (uint32_t)t1;
                cand.tseg2 = (uint32_t)t2;
                /* SJW can never exceed phase segment 2, and there is no benefit
                 * to making it smaller than it can be: a larger SJW directly
                 * buys oscillator tolerance. */
                cand.sjw = min_u32((uint32_t)t2, sjw_max);
                cand.nominal_bt_tq = 1 + cand.tseg1 + cand.tseg2;
                cand.bitrate_bps = clock_hz / (brp * cand.nominal_bt_tq);
                cand.sample_point_permil =
                    (int)(((uint64_t)(1 + cand.tseg1) * 1000) /
                          cand.nominal_bt_tq);
                cand.valid = true;

                const int64_t err =
                    ((int64_t)cand.bitrate_bps - (int64_t)bitrate_bps) *
                    1000000 / (int64_t)bitrate_bps;
                cand.bitrate_error_ppm = (int)err;
                cand.max_tolerance_ppm = can_timing_max_tolerance_ppm(&cand);

                const int rate_err = abs_i(cand.bitrate_error_ppm);
                const int sp_err =
                    abs_i(cand.sample_point_permil - sample_point_permil);
                const uint32_t res = min_tq(cand.nominal_bt_tq);

                /* Ranking, most important first.  tools/can_timing.py
                 * implements the same order and tools/tests cross-checks the
                 * two, so this comment is the shared specification rather than
                 * a description of one implementation.
                 *
                 *   1. bit rate error -- a rate that is merely close is a bus
                 *      that works until it does not;
                 *   2. resolution, up to RESOLUTION_ENOUGH_TQ and no further.
                 *      Five time quanta can express 80.0% exactly and still be
                 *      useless: SJW of 1, and no room for transmitter delay
                 *      compensation. Above the floor, extra tq are free and
                 *      should not outrank anything;
                 *   3. sample point, exactly as asked. An earlier sample
                 *      point changes how the bus behaves on a long cable, so
                 *      it is not somewhere to approximate. An earlier version
                 *      of this solver treated misses under 5% as ties and
                 *      quietly
                 *      returned 82.7% for a request of 87.5%, because the wider
                 *      phase segment 2 scored better on tolerance. That is a
                 *      trade for a person to make by asking for a different
                 *      sample point, not one for a solver to make unasked;
                 *   4. oscillator tolerance, among genuine equals;
                 *   5. more tq, purely so the answer is deterministic. */
                bool better;
                if (!best.valid) {
                    better = true;
                } else if (rate_err != best_rate_err) {
                    better = rate_err < best_rate_err;
                } else if (res != best_res) {
                    better = res > best_res;
                } else if (sp_err != best_sp_err) {
                    better = sp_err < best_sp_err;
                } else if (cand.max_tolerance_ppm != best_tol) {
                    better = cand.max_tolerance_ppm > best_tol;
                } else {
                    better = cand.nominal_bt_tq > best.nominal_bt_tq;
                }

                if (better) {
                    best = cand;
                    best_rate_err = rate_err;
                    best_sp_err = sp_err;
                    best_res = res;
                    best_tol = cand.max_tolerance_ppm;
                }
            }
        } /* round_up */
    }

    return best;
}

uint32_t can_timing_nbtp(const can_timing_t *t) {
    if (t == NULL || !t->valid) {
        return 0;
    }
    /* RM0440: NBTP = NSJW[31:25] | NBRP[24:16] | NTSEG1[15:8] | NTSEG2[6:0],
     * every field stored as value-1. */
    uint32_t reg = 0;
    reg |= ((t->sjw - 1) & 0x7Fu) << 25;
    reg |= ((t->brp - 1) & 0x1FFu) << 16;
    reg |= ((t->tseg1 - 1) & 0xFFu) << 8;
    reg |= ((t->tseg2 - 1) & 0x7Fu);
    return reg;
}

uint32_t can_timing_dbtp(const can_timing_t *t, bool tdc_enable) {
    if (t == NULL || !t->valid) {
        return 0;
    }
    /* RM0440: DBTP = DBRP[20:16] | DTSEG1[12:8] | DTSEG2[7:4] | DSJW[3:0],
     * plus TDC at bit 23. */
    uint32_t reg = 0;
    reg |= ((t->brp - 1) & 0x1Fu) << 16;
    reg |= ((t->tseg1 - 1) & 0x1Fu) << 8;
    reg |= ((t->tseg2 - 1) & 0xFu) << 4;
    reg |= ((t->sjw - 1) & 0xFu);
    if (tdc_enable) {
        reg |= (1u << 23);
    }
    return reg;
}

uint32_t can_timing_tdco(const can_timing_t *data_timing,
                         uint32_t transceiver_loop_ns, uint32_t clock_hz) {
    if (data_timing == NULL || !data_timing->valid || clock_hz == 0) {
        return 0;
    }
    /* Place the secondary sample point at the transceiver's loop delay plus
     * the data phase's own sample point, expressed in mtq (clock cycles before
     * the prescaler). */
    const uint64_t loop_cycles =
        ((uint64_t)transceiver_loop_ns * clock_hz) / 1000000000ull;
    const uint64_t offset =
        loop_cycles + (uint64_t)data_timing->brp * (1 + data_timing->tseg1);
    return (uint32_t)(offset > 127 ? 127 : offset);
}
