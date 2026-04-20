#ifndef CANDIDATE_H
#define CANDIDATE_H

#include "positions.h"

/* ── Candidate record — one entry per registered candidate ───────
 * Stored in candidates.dat as fixed-size structs.
 * vote_count is incremented in-place on every valid cast_vote(),
 * so GET_RESULTS requires only a scan + sort — no counting pass.
 */
typedef struct {
    int  candidate_id;              /* Auto-increment unique ID (1-based)  */
    char full_name[MAX_NAME_LEN];   /* Candidate's full name               */
    char username[MAX_NAME_LEN];    /* Login username (unique)             */
    char password[MAX_PASS_LEN];    /* Plain-text password (per spec)      */
    int  position_id;               /* Which position (POS_* constant)     */
    int  vote_count;                /* Live running total                  */
    int  is_registered;             /* 1 = active, 0 = removed             */
} Candidate;

/* ── Public API ─────────────────────────────────────────────────── */

/* register_candidate
 *   Validates position range and username uniqueness, then appends
 *   a new Candidate record to candidates.dat.
 *   Returns: candidate_id (> 0) on success,
 *            -1 if username is taken or I/O error,
 *            -2 if position_id is out of range.
 */
int register_candidate(const char *full_name,
                       const char *username,
                       const char *password,
                       int         position_id);

/* login_candidate
 *   Scans candidates.dat for a matching username/password pair.
 *   Returns: candidate_id (> 0) on success, -1 on invalid credentials.
 */
int login_candidate(const char *username, const char *password);

/* find_candidate  — Security Check 2 (used by cast_vote)
 *   Loads the candidate by candidate_id AND verifies that
 *   candidate.position_id == the requested position_id.
 *   Returns: 1 on success (fills *out), 0 if not found, inactive,
 *            or position mismatch (ERR|INVALID_CANDIDATE).
 */
int find_candidate(int candidate_id, int position_id, Candidate *out);

/* get_candidate_by_id
 *   Load any active candidate by ID, no position check.
 *   Returns: 1 on success, 0 on failure.
 */
int get_candidate_by_id(int candidate_id, Candidate *out);

/* get_candidates_for_position
 *   Scan candidates.dat and fill out_array with all active candidates
 *   whose position_id matches.
 *   Returns: number of candidates found.
 */
int get_candidates_for_position(int position_id,
                                Candidate *out_array,
                                int        max_count);

/* increment_vote_count
 *   Direct-seek to candidate_id's record and increment vote_count.
 *   Returns: 1 on success, 0 on failure.
 *   NOTE: In Assignment 3B this must be wrapped in a pthread mutex.
 */
int increment_vote_count(int candidate_id);

#endif /* CANDIDATE_H */
