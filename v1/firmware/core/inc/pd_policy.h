/* USB Power Delivery sink policy (design.md sections 6.1, 6.3, 6.5).
 *
 * This is the part of PD worth writing yourself: which PDO to ask for, and
 * which requests must never be made.  The protocol layer underneath it --
 * BMC encoding, CRC, the message state machine -- is ST's X-CUBE-TCPP, and
 * re-implementing it would be a month of work to arrive somewhere worse.
 * `pd_policy_select()` is what that stack calls when a Source_Capabilities
 * message arrives; everything here runs on the host test bench too.
 *
 * Two invariants the design document asks for, enforced in code rather than
 * in a comment:
 *
 *   1. NEVER REQUEST EPR.  Extended Power Range reaches 48 V, but a sink only
 *      ever sees more than 20 V if it explicitly asks.  Staying in SPR caps
 *      the worst case at 20 V, which is what the input stage is rated for.
 *      pd_policy_select() refuses any PDO above PD_SPR_MAX_MV and
 *      pd_policy_allows_epr() exists so a test can assert it stays false.
 *
 *   2. 5 V IS ALWAYS ACCEPTABLE.  A dev board that only works with a PD
 *      charger is a dev board that does not work at the moment you need it.
 *      If nothing better is on offer, the policy takes 5 V and reports
 *      reduced capability rather than failing the contract.
 */

#ifndef PD_POLICY_H
#define PD_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/* Highest voltage reachable without entering EPR mode. */
#define PD_SPR_MAX_MV 20000
/* Where the input stage stops being rated, with margin (section 6.1). */
#define PD_ABSOLUTE_MAX_MV 21000
/* Sink bypass capacitance allowed before a contract exists (section 6.3). */
#define PD_PRECONTRACT_MAX_UF 10
/* A Source_Capabilities message carries at most 7 PDOs in SPR.  Fixing the
 * bound here means the caller can decode into a stack array instead of
 * trusting a length field that arrived over a cable. */
#define PD_MAX_PDOS 7

typedef enum {
    PD_SUPPLY_FIXED = 0,
    PD_SUPPLY_BATTERY = 1,
    PD_SUPPLY_VARIABLE = 2,
    PD_SUPPLY_APDO = 3, /* PPS, and the EPR AVS PDOs */
} pd_supply_type_t;

typedef struct {
    pd_supply_type_t type;
    uint32_t voltage_mv;     /* fixed supplies: the offered voltage */
    uint32_t max_current_ma; /* fixed supplies: the offered current */
    bool epr;                /* PDO is only reachable in EPR mode */
} pd_source_pdo_t;

typedef enum {
    PD_NEED_MINIMUM = 0, /* board logic only: 5 V is plenty */
    PD_NEED_LOOP_SUPPLY, /* 4-20 mA loop power: wants the highest SPR voltage */
} pd_power_need_t;

typedef struct {
    pd_power_need_t need;
    uint32_t board_current_ma;       /* what the board itself draws */
    uint32_t loop_supply_current_ma; /* extra, when driving current loops */
    uint32_t preferred_mv;           /* ideal voltage for the loop boost stage */
} pd_sink_profile_t;

typedef enum {
    PD_RESULT_OK = 0,
    PD_RESULT_FALLBACK_5V,   /* contract made, but below what we wanted */
    PD_RESULT_NO_MATCH,      /* nothing usable was offered */
    PD_RESULT_INVALID_SOURCE /* a malformed or out-of-spec capabilities list */
} pd_result_t;

typedef struct {
    pd_result_t result;
    int object_position; /* 1-based, as the RDO encodes it; -1 if none */
    uint32_t voltage_mv;
    uint32_t current_ma;
    bool capability_mismatch; /* set the RDO's Capability Mismatch bit */
    const char *reason;       /* human-readable, for the bring-up log */
} pd_request_t;

/* Pick a PDO.  Never selects an EPR PDO or anything above PD_SPR_MAX_MV. */
pd_request_t pd_policy_select(const pd_source_pdo_t *pdos, int count,
                              const pd_sink_profile_t *profile);

/* Always false.  Exists so the invariant is testable rather than aspirational. */
bool pd_policy_allows_epr(void);

/* May the bulk capacitance be switched in yet?  Before a contract exists the
 * Type-C specification bounds sink bypass capacitance, and hanging 220 uF on
 * VBUS makes sources declare a fault and drop out (section 6.3). */
bool pd_policy_bulk_enable_allowed(bool contract_established,
                                   uint32_t precontract_uf);

/* Encode a fixed-supply RDO.  Mirrors the PD specification's bit layout so the
 * protocol layer can send it directly. */
uint32_t pd_policy_encode_rdo(const pd_request_t *req, uint32_t max_current_ma,
                              bool give_back);

/* Decode a fixed-supply source PDO, as received. */
pd_source_pdo_t pd_policy_decode_pdo(uint32_t raw);

#endif /* PD_POLICY_H */
