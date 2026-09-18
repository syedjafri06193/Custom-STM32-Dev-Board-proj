/* See pd_policy.h. */

#include "pd_policy.h"

#include <stddef.h>

/* PD fixed-supply PDOs encode voltage in 50 mV units and current in 10 mA. */
#define PDO_VOLT_UNIT_MV 50u
#define PDO_CURR_UNIT_MA 10u

bool pd_policy_allows_epr(void) {
    /* The board's input stage is rated for 20 V plus transient margin.  EPR
     * would put up to 48 V on VBUS, and the only way that happens is if this
     * firmware asks for it.  It does not, and this function is here so a test
     * can say so out loud. */
    return false;
}

bool pd_policy_bulk_enable_allowed(bool contract_established,
                                   uint32_t precontract_uf) {
    if (contract_established) {
        return true;
    }
    /* No contract: only the small always-present bypass capacitance is legal. */
    return precontract_uf <= PD_PRECONTRACT_MAX_UF;
}

static uint32_t required_current_ma(const pd_sink_profile_t *p) {
    uint32_t need = p->board_current_ma;
    if (p->need == PD_NEED_LOOP_SUPPLY) {
        need += p->loop_supply_current_ma;
    }
    return need;
}

static bool usable(const pd_source_pdo_t *pdo) {
    if (pdo->epr) {
        return false; /* invariant 1 */
    }
    if (pdo->type != PD_SUPPLY_FIXED) {
        /* Battery, variable and APDO supplies are all legitimate PD, and all
         * three need policy this board does not have: a variable supply does
         * not guarantee a voltage, and PPS needs periodic re-requests or the
         * contract times out.  Declining them is a decision, not an omission. */
        return false;
    }
    if (pdo->voltage_mv == 0 || pdo->voltage_mv > PD_SPR_MAX_MV) {
        return false;
    }
    return true;
}

pd_request_t pd_policy_select(const pd_source_pdo_t *pdos, int count,
                              const pd_sink_profile_t *profile) {
    pd_request_t out = {
        .result = PD_RESULT_NO_MATCH,
        .object_position = -1,
        .voltage_mv = 0,
        .current_ma = 0,
        .capability_mismatch = false,
        .reason = "no source capabilities",
    };

    if (pdos == NULL || profile == NULL || count <= 0) {
        out.result = PD_RESULT_INVALID_SOURCE;
        out.reason = "empty or malformed source capabilities";
        return out;
    }

    /* The PD specification requires the first PDO to be 5 V fixed.  A source
     * that violates that is either broken or not a source, and trusting the
     * rest of its list is not worth the risk. */
    if (pdos[0].type != PD_SUPPLY_FIXED || pdos[0].voltage_mv != 5000) {
        out.result = PD_RESULT_INVALID_SOURCE;
        out.reason = "first PDO is not 5 V fixed";
        return out;
    }

    const uint32_t need_ma = required_current_ma(profile);

    int best_idx = -1;
    int five_volt_idx = -1;
    uint32_t best_score = 0;

    for (int i = 0; i < count; i++) {
        const pd_source_pdo_t *pdo = &pdos[i];

        if (pdo->type == PD_SUPPLY_FIXED && pdo->voltage_mv == 5000 &&
            five_volt_idx < 0) {
            five_volt_idx = i;
        }
        if (!usable(pdo)) {
            continue;
        }
        if (pdo->max_current_ma < need_ma) {
            continue; /* cannot supply what the board draws */
        }

        /* Closest to the preferred voltage without exceeding it; among equals,
         * more current headroom wins.  For PD_NEED_MINIMUM the preference is
         * 5 V, so this naturally picks the simplest contract available. */
        uint32_t target = (profile->need == PD_NEED_LOOP_SUPPLY)
                              ? profile->preferred_mv
                              : 5000u;
        if (target > PD_SPR_MAX_MV) {
            target = PD_SPR_MAX_MV;
        }
        if (pdo->voltage_mv > target) {
            continue;
        }

        uint32_t score = pdo->voltage_mv * 1000u + pdo->max_current_ma;
        if (best_idx < 0 || score > best_score) {
            best_idx = i;
            best_score = score;
        }
    }

    if (best_idx >= 0) {
        const pd_source_pdo_t *pdo = &pdos[best_idx];
        out.object_position = best_idx + 1;
        out.voltage_mv = pdo->voltage_mv;
        out.current_ma = need_ma;
        out.capability_mismatch = false;

        bool wanted_more = (profile->need == PD_NEED_LOOP_SUPPLY) &&
                           (pdo->voltage_mv < profile->preferred_mv);
        if (wanted_more) {
            out.result = PD_RESULT_FALLBACK_5V;
            out.capability_mismatch = true;
            out.reason = "below preferred voltage: loop supply will be limited";
        } else {
            out.result = PD_RESULT_OK;
            out.reason = "matched";
        }
        return out;
    }

    /* Nothing matched the profile.  5 V is always acceptable -- taking a
     * reduced contract beats refusing to power on (section 6.5). */
    if (five_volt_idx >= 0) {
        out.result = PD_RESULT_FALLBACK_5V;
        out.object_position = five_volt_idx + 1;
        out.voltage_mv = 5000;
        out.current_ma = (pdos[five_volt_idx].max_current_ma < need_ma)
                             ? pdos[five_volt_idx].max_current_ma
                             : need_ma;
        out.capability_mismatch = true;
        out.reason = "fell back to 5 V";
        return out;
    }

    out.reason = "no usable SPR fixed supply offered";
    return out;
}

uint32_t pd_policy_encode_rdo(const pd_request_t *req, uint32_t max_current_ma,
                              bool give_back) {
    if (req == NULL || req->object_position < 1) {
        return 0;
    }

    const uint32_t op_current = req->current_ma / PDO_CURR_UNIT_MA;
    const uint32_t max_current = max_current_ma / PDO_CURR_UNIT_MA;

    uint32_t rdo = 0;
    rdo |= ((uint32_t)(req->object_position & 0x7) << 28);
    if (give_back) {
        rdo |= (1u << 27);
    }
    if (req->capability_mismatch) {
        rdo |= (1u << 26);
    }
    rdo |= (1u << 25); /* USB communications capable */
    rdo |= ((op_current & 0x3FF) << 10);
    rdo |= (max_current & 0x3FF);
    return rdo;
}

pd_source_pdo_t pd_policy_decode_pdo(uint32_t raw) {
    pd_source_pdo_t pdo = {0};
    pdo.type = (pd_supply_type_t)((raw >> 30) & 0x3);

    if (pdo.type == PD_SUPPLY_FIXED) {
        pdo.voltage_mv = ((raw >> 10) & 0x3FF) * PDO_VOLT_UNIT_MV;
        pdo.max_current_ma = (raw & 0x3FF) * PDO_CURR_UNIT_MA;
    }
    /* Anything above the SPR ceiling can only have come from an EPR source
     * capabilities message, so mark it and let the policy refuse it. */
    pdo.epr = (pdo.voltage_mv > PD_SPR_MAX_MV);
    return pdo;
}
