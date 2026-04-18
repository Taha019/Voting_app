#ifndef ELECTION_H
#define ELECTION_H

#include "voter.h"
#include "candidate.h"

/* ── Return codes for cast_vote() ───────────────────────────────── */
#define VOTE_OK             0    /* Vote accepted and committed            */
#define VOTE_ERR_NOT_FOUND (-1)  /* Voter not found / not registered       */
#define VOTE_ERR_ALREADY   (-2)  /* Voter already voted for this position  */
#define VOTE_ERR_INVALID   (-3)  /* Candidate not found or position mismatch */

/* cast_vote
 *   The core election action.  Performs TWO sequential security checks
 *   before writing anything to disk:
 *
 *   Security Check 1: voter exists, is active, AND
 *                     voted_positions[position_id] == 0.
 *   Security Check 2: candidate exists AND
 *                     candidate.position_id == position_id.
 *                     (prevents submitting a candidate_id from a different
 *                     position — closes a logical gap in naive designs.)
 *
 *   On success: increments candidate.vote_count and sets
 *               voter.voted_positions[position_id] = 1.
 *
 *   Returns: VOTE_OK, VOTE_ERR_NOT_FOUND, VOTE_ERR_ALREADY, or
 *            VOTE_ERR_INVALID.
 *
 *   NOTE (Assignment 3B): The two file-write calls inside must be
 *   wrapped in pthread_mutex_lock / pthread_mutex_unlock to prevent
 *   race conditions when multiple threads vote concurrently.
 */
int cast_vote(int voter_id, int candidate_id, int position_id);

/* generate_results
 *   Fill out_array with all active candidates for position_id, sorted
 *   by vote_count descending (live tally — no re-counting required).
 *   Returns: number of candidates written to out_array.
 */
int generate_results(int position_id, Candidate *out_array, int max_count);

/* generate_all_results
 *   Print a formatted results table for every position to stdout.
 *   Internally calls generate_results() for each POS_* value.
 */
void generate_all_results(void);

#endif /* ELECTION_H */
