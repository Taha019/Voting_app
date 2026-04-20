#ifndef POSITIONS_H
#define POSITIONS_H

/* ── Compile-time election configuration ─────────────────────────
 * This is the single source of truth for position IDs.
 * To add or rename a position, only this file needs to change.
 * All other modules consume the POS_* constants automatically.
 */

#define MAX_POSITIONS     5
#define MAX_POSITION_NAME 32

/* Shared field-size constants used by Voter and Candidate structs */
#define MAX_NAME_LEN      64
#define MAX_PASS_LEN      32

/* Position ID constants */
#define POS_CHAIRMAN      0
#define POS_VICE_CHAIRMAN 1
#define POS_SECRETARY     2
#define POS_TREASURER     3
#define POS_PRO           4

/* Human-readable names indexed by POS_* values.
 * Declared static so each translation unit gets its own copy; the
 * compiler will not emit an "unused variable" warning for -Wall. */
static const char * const POSITION_NAMES[MAX_POSITIONS] = {
    "Chairman",
    "Vice Chairman",
    "Secretary",
    "Treasurer",
    "PRO"
};

#endif /* POSITIONS_H */
