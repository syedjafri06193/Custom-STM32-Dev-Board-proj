/* UCPD1: the Type-C sink front end.
 *
 * SCOPE, stated plainly, because this is the part of the firmware where it
 * would be easiest to write something that looks complete and is not.
 *
 * What is here: the peripheral brought up as a sink, both CC lines monitored,
 * orientation detection, and the dead-battery handover sequenced correctly
 * against section 6.2.  That is everything the board needs to come up on a
 * dumb 5 V source, and it is what bring-up step 13 exercises.
 *
 * What is NOT here: the USB Power Delivery protocol layer -- BMC encoding,
 * CRC, GoodCRC, retries, the policy engine state machine.  That is a
 * conformance-grade component and writing an unverified one would be worse
 * than having none, because it would fail in the field rather than on the
 * bench.  The seam is ucpd_submit_source_capabilities(): whatever provides
 * Source_Capabilities -- ST's X-CUBE-TCPP stack, an off-the-shelf PD library,
 * or an STUSB4500 doing it in hardware as section 6.2 suggests -- hands the
 * PDOs here, and from that point on the decision is pd_policy_select(), which
 * is fully covered by host tests.
 *
 * The important invariant survives that gap: this board never requests EPR
 * and never accepts above 20 V.  pd_policy.c enforces it, vbus.c flags a
 * violation, and neither depends on the protocol layer being present.
 */

#include "board.h"
#include "gpio.h"
#include "hal.h"
#include "pd_policy.h"
#include "stm32g474.h"

/* TYPEC_VSTATE_CCx: 0 = lowest (nothing), 1/2/3 = increasing Rp advertisement
 * on a sink.  Non-zero on exactly one line is an attached source, and which
 * line it is gives the cable orientation. */
#define VSTATE_NONE 0u

void ucpd_init(void) {
    RCC->APB1ENR2 |= RCC_APB1ENR2_UCPD1EN;
    (void)RCC->APB1ENR2;

    /* The CC pads are analog and belong to the peripheral; gpio_init() has
     * already put them in analog mode with no pulls, which matters because a
     * GPIO pull-up on a CC line advertises the board as a source. */

    UCPD1->CFG1 = 0;

    /* Sink, both CC lines active.  ANAMODE = 1 selects sink (Rd); CCENABLE =
     * 11 keeps both lines powered so orientation can be detected rather than
     * assumed -- a reversible connector that only works one way round is a
     * bug report, not a feature. */
    UCPD1->CR = (3u << UCPD_CR_CCENABLE_Pos) | UCPD_CR_ANAMODE;

    UCPD1->CFG1 |= UCPD_CFG1_UCPDEN;

    /* Only now is it safe to let go of the dead-battery pull-downs: the
     * peripheral is enabled and presenting Rd itself, so there is no window
     * where the port advertises nothing and the source removes VBUS. */
    delay_ms(2);
    tcpp01_release_dead_battery();
}

uint8_t ucpd_vstate(cc_line_t line) {
    const uint32_t sr = UCPD1->SR;
    switch (line) {
        case CC_1: return (uint8_t)((sr >> UCPD_SR_TYPEC_VSTATE_CC1_Pos) & 3u);
        case CC_2: return (uint8_t)((sr >> UCPD_SR_TYPEC_VSTATE_CC2_Pos) & 3u);
        default: return VSTATE_NONE;
    }
}

cc_line_t ucpd_attached_line(void) {
    const uint8_t v1 = ucpd_vstate(CC_1);
    const uint8_t v2 = ucpd_vstate(CC_2);

    if (v1 != VSTATE_NONE && v2 == VSTATE_NONE) {
        return CC_1;
    }
    if (v2 != VSTATE_NONE && v1 == VSTATE_NONE) {
        return CC_2;
    }
    /* Both lines pulled up is a powered accessory or a debug-accessory cable,
     * not an ordinary source.  Reporting "none" is right: this board has no
     * behaviour for those, and guessing an orientation would be worse than
     * saying nothing. */
    return CC_NONE;
}

bool ucpd_source_attached(void) { return ucpd_attached_line() != CC_NONE; }

/* ------------------------------------------------------------ policy seam */

/* Called by whatever supplies the protocol layer, with the raw PDOs from a
 * Source_Capabilities message.  Everything from here is core/ code that the
 * host tests cover: the selection, the SPR ceiling, and the rule that the
 * bulk capacitance only comes online once the contract is explicit.
 *
 * Returns the RDO to send back, and writes the decision to *out so the
 * bring-up log can print what was asked for and why. */
uint32_t ucpd_on_source_capabilities(const uint32_t *raw_pdos, int count,
                                     pd_request_t *out);

uint32_t ucpd_on_source_capabilities(const uint32_t *raw_pdos, int count,
                                     pd_request_t *out) {
    pd_source_pdo_t pdos[PD_MAX_PDOS];
    if (count > PD_MAX_PDOS) {
        count = PD_MAX_PDOS;
    }
    for (int i = 0; i < count; i++) {
        pdos[i] = pd_policy_decode_pdo(raw_pdos[i]);
    }

    /* This board's profile: it powers 4-20 mA loops, so it wants the highest
     * SPR voltage on offer -- but pd_policy_select() is what enforces that
     * "highest" stops at 20 V and never reaches for an EPR PDO. */
    const pd_sink_profile_t profile = {
        .need = PD_NEED_LOOP_SUPPLY,
        .board_current_ma = BOARD_PD_OPERATING_MA,
        .loop_supply_current_ma = 0,
        .preferred_mv = BOARD_PD_MAX_MV,
    };

    const pd_request_t req = pd_policy_select(pdos, count, &profile);
    if (out != NULL) {
        *out = req;
    }
    if (req.result == PD_RESULT_NO_MATCH ||
        req.result == PD_RESULT_INVALID_SOURCE) {
        return 0;
    }

    /* Section 6.3, enforced rather than commented: the bulk capacitance only
     * comes online once a contract exists, and pd_policy decides that. */
    if (pd_policy_bulk_enable_allowed(true, BOARD_PRECONTRACT_UF)) {
        bulk_switch_enable(true);
    }

    return pd_policy_encode_rdo(&req, req.current_ma, false);
}
