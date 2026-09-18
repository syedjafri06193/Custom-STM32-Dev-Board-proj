/* FDCAN1: bit timing from the solver in core/, and enough of a mailbox to
 * run the three loopback stages of the bring-up procedure.
 *
 * The bit timing here is not computed in this file.  can_timing_solve() runs
 * on the host under test, produces the register fields, and this file writes
 * them.  That split is the point: bit timing is where CAN boards die, and a
 * number that only exists inside a target-only driver is a number nobody ever
 * checks.  can_timing_nbtp() and can_timing_dbtp() do the register packing,
 * and the host tests assert the encodings are value-minus-one.
 *
 * ---------------------------------------------------------------------------
 * MESSAGE RAM
 *
 * ST's variant of the Bosch M_CAN on the G4 has *fixed* message RAM
 * addresses -- unlike the H7, there is no SIDFC/RXF0C/TXBC address
 * programming, and RXGFC replaces the filter and element-size registers.
 * Each instance owns a 0x350-byte block, laid out as below.  This is the one
 * table in this file that cannot be derived from anything else and must be
 * checked against RM0440 for the exact part before first silicon; it is
 * called out in docs/notes-on-the-spec.md for that reason.
 *
 *   offset  size    contents
 *   0x0000  0x0070  28 standard filters, 1 word each
 *   0x0070  0x0040   8 extended filters, 2 words each
 *   0x00B0  0x00D8   Rx FIFO 0, 3 elements x 18 words
 *   0x0188  0x00D8   Rx FIFO 1, 3 elements x 18 words
 *   0x0260  0x0018   Tx event FIFO, 3 elements x 2 words
 *   0x0278  0x00D8   Tx buffers, 3 elements x 18 words
 *                    ------
 *                    0x0350
 * ---------------------------------------------------------------------------
 */

#include <string.h>

#include "board.h"
#include "can_timing.h"
#include "gpio.h"
#include "hal.h"
#include "stm32g474.h"

#define FDCAN1_RAM_OFFSET 0x0000u
#define RAM_RXF0_OFFSET 0x00B0u
#define RAM_TXBUF_OFFSET 0x0278u
#define ELEMENT_WORDS 18u

#define FDCAN1_RAM ((volatile uint32_t *)(FDCAN_RAM_BASE + FDCAN1_RAM_OFFSET))

/* DLC 0-8 are the byte count; 9-15 encode 12,16,20,24,32,48,64. */
static const uint8_t k_dlc_to_len[16] = {0, 1, 2,  3,  4,  5,  6,  7,
                                         8, 12, 16, 20, 24, 32, 48, 64};

static uint8_t len_to_dlc(uint8_t len) {
    for (uint8_t d = 0; d < 16; d++) {
        if (k_dlc_to_len[d] >= len) {
            return d;
        }
    }
    return 15;
}

static int enter_init(void) {
    FDCAN1->CCCR |= FDCAN_CCCR_INIT;
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if (FDCAN1->CCCR & FDCAN_CCCR_INIT) {
            FDCAN1->CCCR |= FDCAN_CCCR_CCE;
            return 0;
        }
    }
    return -1;
}

int can_init(const can_timing_t *nominal, const can_timing_t *data,
             can_mode_t mode) {
    if (nominal == NULL || !nominal->valid) {
        return -1;
    }

    RCC->APB1ENR1 |= RCC_APB1ENR1_FDCANEN;
    (void)RCC->APB1ENR1;

    /* Leave sleep before anything else; a peripheral still in clock-stop
     * accepts writes and ignores them. */
    FDCAN1->CCCR &= ~FDCAN_CCCR_CSR;
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if ((FDCAN1->CCCR & FDCAN_CCCR_CSA) == 0u) {
            break;
        }
    }

    if (enter_init() != 0) {
        return -2;
    }

    /* Clear this instance's message RAM.  The RAM is not reset by a system
     * reset, so a warm restart otherwise inherits whatever the previous run
     * left in the Tx buffers -- including a frame that is about to be
     * transmitted onto a live bus. */
    for (uint32_t i = 0; i < 0x350u / 4u; i++) {
        FDCAN1_RAM[i] = 0;
    }

    FDCAN1->NBTP = can_timing_nbtp(nominal);

    const bool want_fd = (data != NULL && data->valid);
    if (want_fd) {
        /* Transmitter delay compensation is not optional above about
         * 1 Mbit/s: the transceiver's loop delay (110 ns for a TCAN1042) is a
         * meaningful fraction of a 500 ns data bit, and without TDC the
         * controller samples its own transmission at the wrong moment and
         * reports bit errors on a bus that is working perfectly. */
        const uint32_t tdco =
            can_timing_tdco(data, BOARD_CAN_TRANSCEIVER_LOOP_NS,
                            BOARD_FDCAN_CLK_HZ);
        FDCAN1->TDCR = (tdco << FDCAN_TDCR_TDCO_Pos);
        FDCAN1->DBTP = can_timing_dbtp(data, true);
        FDCAN1->CCCR |= FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE;
    } else {
        FDCAN1->CCCR &= ~(FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE);
    }

    /* Accept everything into FIFO 0 and configure no filters.  A bring-up
     * board that silently drops frames because a filter list is empty is a
     * day lost; filtering is a later concern than "does the peripheral
     * work at all". */
    FDCAN1->RXGFC = (0u << FDCAN_RXGFC_ANFS_Pos) | (0u << FDCAN_RXGFC_ANFE_Pos) |
                    (0u << FDCAN_RXGFC_LSS_Pos) | (0u << FDCAN_RXGFC_LSE_Pos);

    FDCAN1->CCCR &= ~(FDCAN_CCCR_TEST | FDCAN_CCCR_MON);
    FDCAN1->TEST &= ~FDCAN_TEST_LBCK;

    switch (mode) {
        case CAN_MODE_INTERNAL_LOOPBACK:
            /* Bring-up step 17.  TEST+LBCK+MON: the transmitter is
             * internally routed to the receiver and the TX pin is held
             * recessive, so nothing reaches the transceiver.  This proves the
             * peripheral and the bit timing with no bus, no transceiver and
             * no second node -- which is the only way to know which of those
             * is broken when the real thing does not work. */
            FDCAN1->CCCR |= FDCAN_CCCR_TEST | FDCAN_CCCR_MON;
            FDCAN1->TEST |= FDCAN_TEST_LBCK;
            break;
        case CAN_MODE_EXTERNAL_LOOPBACK:
            /* Step 18: same loopback, but MON off, so the frame goes out
             * through the transceiver and comes back in through it.  This is
             * what catches swapped TX/RX or a transceiver stuck in standby. */
            FDCAN1->CCCR |= FDCAN_CCCR_TEST;
            FDCAN1->TEST |= FDCAN_TEST_LBCK;
            break;
        case CAN_MODE_LISTEN_ONLY:
            FDCAN1->CCCR |= FDCAN_CCCR_MON;
            break;
        case CAN_MODE_NORMAL:
        default:
            break;
    }

    /* Leave INIT; the peripheral starts synchronising to the bus. */
    FDCAN1->CCCR &= ~(FDCAN_CCCR_CCE | FDCAN_CCCR_INIT);
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if ((FDCAN1->CCCR & FDCAN_CCCR_INIT) == 0u) {
            return 0;
        }
    }
    return -3;
}

int can_send(const can_frame_t *f) {
    if (f == NULL) {
        return -1;
    }
    /* Tx FIFO full? */
    if (FDCAN1->TXFQS & (1u << 21)) {
        return -2;
    }

    const uint32_t put_index = (FDCAN1->TXFQS >> 16) & 0x3u;
    volatile uint32_t *el =
        &FDCAN1_RAM[(RAM_TXBUF_OFFSET / 4u) + put_index * ELEMENT_WORDS];

    const uint8_t dlc = len_to_dlc(f->dlc);
    const uint8_t len = k_dlc_to_len[dlc];

    /* T0: a standard ID sits in bits 28:18, not 10:0.  Writing it
     * right-aligned is the single most common M_CAN mistake and produces a
     * node that transmits with an ID nobody filters for. */
    el[0] = f->extended ? ((1u << 30) | (f->id & 0x1FFFFFFFu))
                        : ((f->id & 0x7FFu) << 18);

    el[1] = ((uint32_t)dlc << 16) | (f->fd ? (1u << 21) : 0u) |
            (f->brs ? (1u << 20) : 0u);

    for (uint8_t i = 0; i < len; i += 4) {
        uint32_t w = 0;
        for (uint8_t b = 0; b < 4 && (i + b) < len; b++) {
            const uint8_t src = (i + b) < f->dlc ? f->data[i + b] : 0u;
            w |= (uint32_t)src << (8 * b);
        }
        el[2 + (i / 4)] = w;
    }

    FDCAN1->TXBAR = (1u << put_index);
    return 0;
}

int can_recv(can_frame_t *f) {
    if (f == NULL) {
        return -1;
    }
    const uint32_t status = FDCAN1->RXF0S;
    if ((status & 0xFu) == 0u) {
        return -1; /* fill level zero: nothing pending */
    }

    const uint32_t get_index = (status >> 8) & 0x3u;
    volatile uint32_t *el =
        &FDCAN1_RAM[(RAM_RXF0_OFFSET / 4u) + get_index * ELEMENT_WORDS];

    const uint32_t r0 = el[0];
    const uint32_t r1 = el[1];

    memset(f, 0, sizeof(*f));
    f->extended = (r0 & (1u << 30)) != 0u;
    f->id = f->extended ? (r0 & 0x1FFFFFFFu) : ((r0 >> 18) & 0x7FFu);
    f->fd = (r1 & (1u << 21)) != 0u;
    f->brs = (r1 & (1u << 20)) != 0u;

    const uint8_t dlc = (uint8_t)((r1 >> 16) & 0xFu);
    const uint8_t len = k_dlc_to_len[dlc];
    f->dlc = len;

    for (uint8_t i = 0; i < len; i++) {
        f->data[i] = (uint8_t)(el[2 + (i / 4)] >> (8 * (i % 4)));
    }

    /* Acknowledge, which is what frees the element.  Forgetting this gives a
     * FIFO that fills up once and never receives again. */
    FDCAN1->RXF0A = get_index;
    return 0;
}

void can_error_counters(uint8_t *tx_errors, uint8_t *rx_errors, bool *bus_off) {
    const uint32_t ecr = FDCAN1->ECR;
    if (tx_errors != NULL) {
        *tx_errors = (uint8_t)(ecr & 0xFFu);
    }
    if (rx_errors != NULL) {
        *rx_errors = (uint8_t)((ecr >> 8) & 0x7Fu);
    }
    if (bus_off != NULL) {
        *bus_off = (FDCAN1->PSR & FDCAN_PSR_BO) != 0u;
    }
}

uint8_t can_last_error_code(void) {
    /* LEC is read-clearing in the sense that it latches 7 ("no change") after
     * a read, so this is the value since the last call. */
    return (uint8_t)(FDCAN1->PSR & FDCAN_PSR_LEC_Msk);
}
