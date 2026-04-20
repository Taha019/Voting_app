#ifndef VOTER_H
#define VOTER_H

#include "positions.h"

/* ── Voter record — one entry per registered voter in voters.dat ─ */
typedef struct {
    int  voter_id;                        /* Auto-increment unique ID (1-based)  */
    char full_name[MAX_NAME_LEN];         /* Voter's full name                   */
    char username[MAX_NAME_LEN];          /* Login username (unique)             */
    char password[MAX_PASS_LEN];          /* Plain-text password (per spec)      */
    int  voted_positions[MAX_POSITIONS];  /* 0 = not yet voted, 1 = voted        */
    int  is_registered;                   /* 1 = active,         0 = removed     */
} Voter;

/* ── Public API ─────────────────────────────────────────────────── */

/* register_voter
 *   Validates username uniqueness, assigns the next voter_id, and
 *   appends a new Voter record to voters.dat.
 *   Returns: voter_id (> 0) on success, -1 if username is taken or I/O error.
 */
int register_voter(const char *full_name,
                   const char *username,
                   const char *password);

/* login_voter
 *   Scans voters.dat for a matching username/password pair.
 *   Returns: voter_id (> 0) on success, -1 on invalid credentials.
 */
int login_voter(const char *username, const char *password);

/* has_voted_for_position
 *   Direct-seek lookup of voted_positions[position_id].
 *   Returns: 1 = already voted, 0 = not yet voted, -1 = voter not found.
 */
int has_voted_for_position(int voter_id, int position_id);

/* get_voter_by_id
 *   Load the Voter record with the given voter_id into *out.
 *   Returns: 1 on success, 0 on failure (not found or I/O error).
 */
int get_voter_by_id(int voter_id, Voter *out);

/* mark_position_voted
 *   Set voted_positions[position_id] = 1 and write the record back.
 *   Returns: 1 on success, 0 on failure.
 *   NOTE: In Assignment 3B wrap this in a pthread mutex.
 */
int mark_position_voted(int voter_id, int position_id);

#endif /* VOTER_H */
