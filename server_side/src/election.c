#include "../headers/election.h"
#include <stdio.h>

/* ── Internal helpers ────────────────────────────────────────────*/

/* Bubble sort descending by vote_count.
 * Sufficient for small sets; MAX candidates per position is unlikely
 * to exceed a few dozen in practice. */
static void sort_by_vote_count(Candidate *arr, int count)
{
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (arr[j].vote_count < arr[j + 1].vote_count) {
                Candidate tmp = arr[j];
                arr[j]        = arr[j + 1];
                arr[j + 1]    = tmp;
            }
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────*/

int cast_vote(int voter_id, int candidate_id, int position_id)
{
    /* Basic range guard */
    if (position_id < 0 || position_id >= MAX_POSITIONS)
        return VOTE_ERR_INVALID;

    /* ── Security Check 1 ───────────────────────────────────────
     * Voter must exist, be active, and not have voted for this
     * position yet.
     */
    Voter voter;
    if (!get_voter_by_id(voter_id, &voter) || !voter.is_registered)
        return VOTE_ERR_NOT_FOUND;

    if (voter.voted_positions[position_id] == 1)
        return VOTE_ERR_ALREADY;

    /* ── Security Check 2 ───────────────────────────────────────
     * Candidate must exist AND their stored position_id must match
     * the requested position_id.  This closes the logical gap where
     * a voter could pass a Chairman's candidate_id when voting for
     * Secretary.
     */
    Candidate cand;
    if (!find_candidate(candidate_id, position_id, &cand))
        return VOTE_ERR_INVALID;

    /* ── Commit to disk ─────────────────────────────────────────
     * Both writes are safe here (single-threaded, Assignment 1).
     * For Assignment 3B: wrap both calls in
     *   pthread_mutex_lock(&file_mutex);
     *   ...
     *   pthread_mutex_unlock(&file_mutex);
     */
    if (!increment_vote_count(candidate_id))
        return VOTE_ERR_INVALID;

    if (!mark_position_voted(voter_id, position_id))
        return VOTE_ERR_INVALID;

    return VOTE_OK;
}

int generate_results(int position_id, Candidate *out_array, int max_count)
{
    int count = get_candidates_for_position(position_id, out_array, max_count);
    sort_by_vote_count(out_array, count);
    return count;
}

void generate_all_results(void)
{
    for (int pos = 0; pos < MAX_POSITIONS; pos++) {
        Candidate results[64];
        int count = generate_results(pos, results, 64);

        printf("\n  %-28s\n", POSITION_NAMES[pos]);
        printf("  %.28s\n", "----------------------------");

        if (count == 0) {
            printf("  (no candidates registered)\n");
        } else {
            printf("  %-4s  %-28s  %s\n", "Rank", "Name", "Votes");
            printf("  %-4s  %-28s  %s\n", "----", "----------------------------", "-----");
            for (int i = 0; i < count; i++) {
                printf("  %-4d  %-28s  %d\n",
                       i + 1,
                       results[i].full_name,
                       results[i].vote_count);
            }
        }
    }
}
