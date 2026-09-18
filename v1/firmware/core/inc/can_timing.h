/* CAN / CAN FD bit timing and oscillator tolerance (design.md section 7.2).
 *
 * The design document's claim is that the STM32G4's internal HSI16 (about
 * +/-1%, worse over temperature) is not good enough for CAN, and that a
 * +/-30 ppm crystal is required.  That is exactly the kind of claim worth
 * turning into an executable check, because the failure it prevents -- a board
 * that works on a short bench cable at room temperature and throws error
 * frames in a warm cabinet -- is expensive to discover later.
 *
 * So this module does two things:
 *
 *   1. solves for FDCAN register values (BRP, TSEG1, TSEG2, SJW) given a clock
 *      and a target bit rate, hitting a requested sample point;
 *   2. computes the maximum oscillator tolerance that timing can survive, from
 *      the ISO 11898-1 formulas, so the crystal-versus-HSI question is answered
 *      with a number instead of an opinion.
 *
 * Tolerance, per ISO 11898-1:
 *
 *     df <= SJW / (2 * 10 * NBT)
 *     df <= min(PS1, PS2) / (2 * (13 * NBT - PS2))
 *
 * where NBT is the nominal bit time in time quanta.  The factor of two is
 * because both ends of a link drift, so the number below is the budget for a
 * single node.
 *
 * A caveat worth stating rather than hiding: the STM32 FDCAN register TSEG1
 * lumps the propagation segment together with phase segment 1, and the formula
 * above wants phase segment 1 alone.  Using TSEG1 for PS1 is therefore
 * optimistic in the second term.  In practice the first term (the SJW one)
 * dominates at ordinary sample points, and that one is exact.  Treat the
 * result as a design aid, not a certification.
 */

#ifndef CAN_TIMING_H
#define CAN_TIMING_H

#include <stdbool.h>
#include <stdint.h>

/* STM32G4 FDCAN register limits (RM0440, NBTP and DBTP). */
#define FDCAN_NBRP_MAX 512
#define FDCAN_NTSEG1_MAX 256
#define FDCAN_NTSEG2_MAX 128
#define FDCAN_NSJW_MAX 128
#define FDCAN_DBRP_MAX 32
#define FDCAN_DTSEG1_MAX 32
#define FDCAN_DTSEG2_MAX 16
#define FDCAN_DSJW_MAX 16

/* Common oscillator sources, in parts per million. */
#define OSC_PPM_HSI16 10000 /* +/-1%, and worse over temperature */
#define OSC_PPM_CRYSTAL_30 30
#define OSC_PPM_CRYSTAL_50 50

typedef struct {
    uint32_t brp;   /* prescaler, 1-based */
    uint32_t tseg1; /* propagation + phase segment 1, in tq */
    uint32_t tseg2; /* phase segment 2, in tq */
    uint32_t sjw;   /* resynchronisation jump width, in tq */

    uint32_t bitrate_bps;    /* what this configuration actually produces */
    uint32_t nominal_bt_tq;  /* 1 + tseg1 + tseg2 */
    int sample_point_permil; /* 0..1000 */
    int bitrate_error_ppm;   /* signed, relative to the requested rate */

    /* Maximum per-node oscillator tolerance this timing tolerates. */
    int max_tolerance_ppm;
    bool valid;
} can_timing_t;

typedef struct {
    can_timing_t nominal;
    can_timing_t data; /* CAN FD data phase; .valid is false for classic CAN */
    bool fd;
} can_config_t;

/* Solve for the closest achievable bit timing.
 *
 * `sample_point_permil` is the requested sample point, e.g. 875 for 87.5%,
 * which is what CiA recommends for rates up to 500 kbit/s and what most
 * industrial buses use.  The solver prefers an exact bit rate first and the
 * requested sample point second -- in that order, because a bit-rate error is
 * unrecoverable while a sample point a few percent off is merely suboptimal.
 */
can_timing_t can_timing_solve(uint32_t clock_hz, uint32_t bitrate_bps,
                              int sample_point_permil, bool data_phase);

/* Maximum per-node oscillator tolerance, in ppm, for a given timing. */
int can_timing_max_tolerance_ppm(const can_timing_t *t);

/* Would an oscillator of this accuracy work with this timing? */
bool can_timing_osc_ok(const can_timing_t *t, int osc_ppm);

/* Pack into the STM32G4 NBTP / DBTP register layouts (RM0440). */
uint32_t can_timing_nbtp(const can_timing_t *t);
uint32_t can_timing_dbtp(const can_timing_t *t, bool tdc_enable);

/* Transmitter delay compensation offset, in tq of the data phase.  CAN FD
 * above about 1 Mbit/s needs this or the transceiver's loop delay eats the
 * sample point. */
uint32_t can_timing_tdco(const can_timing_t *data_timing,
                         uint32_t transceiver_loop_ns, uint32_t clock_hz);

#endif /* CAN_TIMING_H */
