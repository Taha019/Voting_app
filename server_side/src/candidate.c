#include "../headers/candidate.h"
#include "../headers/file_io.h"
#include <stdio.h>
#include <string.h>

/* ── Internal helpers ────────────────────────────────────────────*/

/* Returns 1 if username is not present in candidates.dat, 0 if it is. */
static int validate_candidate_username_unique(const char *username)
{
    FILE *fp = open_candidates_file("rb");
    if (!fp) return 1;

    Candidate c;
    while (fread(&c, sizeof(Candidate), 1, fp) == 1) {
        if (c.is_registered && strcmp(c.username, username) == 0) {
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return 1;
}

static int validate_position_id(int position_id)
{
    return (position_id >= 0 && position_id < MAX_POSITIONS);
}

static int generate_candidate_id(void)
{
    return (int)get_record_count(CANDIDATES_FILE, sizeof(Candidate)) + 1;
}

static int write_candidate_record(const Candidate *c)
{
    FILE *fp = open_candidates_file("ab");
    if (!fp) return 0;
    size_t ok = fwrite(c, sizeof(Candidate), 1, fp);
    fclose(fp);
    return (ok == 1);
}

/* O(1) direct seek by candidate_id (1-based). */
static int seek_candidate_record(int candidate_id, Candidate *out)
{
    if (candidate_id <= 0) return 0;

    FILE *fp = open_candidates_file("rb");
    if (!fp) return 0;

    long offset = (long)(candidate_id - 1) * (long)sizeof(Candidate);
    if (fseek(fp, offset, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    int ok = (fread(out, sizeof(Candidate), 1, fp) == 1);
    fclose(fp);
    return ok;
}

/* ── Public API ──────────────────────────────────────────────────*/

int register_candidate(const char *full_name,
                       const char *username,
                       const char *password,
                       int         position_id)
{
    if (!validate_position_id(position_id))            return -2; /* ERR|INVALID_POSITION */
    if (!validate_candidate_username_unique(username)) return -1; /* ERR|USERNAME_TAKEN   */

    Candidate c;
    memset(&c, 0, sizeof(Candidate));
    c.candidate_id  = generate_candidate_id();
    c.position_id   = position_id;
    c.vote_count    = 0;
    c.is_registered = 1;

    strncpy(c.full_name, full_name, MAX_NAME_LEN - 1);
    strncpy(c.username,  username,  MAX_NAME_LEN - 1);
    strncpy(c.password,  password,  MAX_PASS_LEN - 1);

    if (!write_candidate_record(&c)) return -1;
    return c.candidate_id;
}

int login_candidate(const char *username, const char *password)
{
    FILE *fp = open_candidates_file("rb");
    if (!fp) return -1;

    Candidate c;
    while (fread(&c, sizeof(Candidate), 1, fp) == 1) {
        if (c.is_registered && strcmp(c.username, username) == 0) {
            fclose(fp);
            return (strcmp(password, c.password) == 0) ? c.candidate_id : -1;
        }
    }
    fclose(fp);
    return -1;
}

int find_candidate(int candidate_id, int position_id, Candidate *out)
{
    if (!seek_candidate_record(candidate_id, out)) return 0;
    if (!out->is_registered)             return 0;
    if (out->position_id != position_id) return 0; /* Security Check 2 */
    return 1;
}

int get_candidate_by_id(int candidate_id, Candidate *out)
{
    if (!seek_candidate_record(candidate_id, out)) return 0;
    return out->is_registered;
}

int get_candidates_for_position(int position_id,
                                Candidate *out_array,
                                int        max_count)
{
    if (!validate_position_id(position_id)) return 0;

    FILE *fp = open_candidates_file("rb");
    if (!fp) return 0;

    Candidate c;
    int count = 0;
    while (fread(&c, sizeof(Candidate), 1, fp) == 1 && count < max_count) {
        if (c.is_registered && c.position_id == position_id)
            out_array[count++] = c;
    }
    fclose(fp);
    return count;
}

int increment_vote_count(int candidate_id)
{
    if (candidate_id <= 0) return 0;

    FILE *fp = fopen(CANDIDATES_FILE, "r+b");
    if (!fp) return 0;

    long offset = (long)(candidate_id - 1) * (long)sizeof(Candidate);
    if (fseek(fp, offset, SEEK_SET) != 0) { fclose(fp); return 0; }

    Candidate c;
    if (fread(&c, sizeof(Candidate), 1, fp) != 1) { fclose(fp); return 0; }

    c.vote_count++;

    if (fseek(fp, offset, SEEK_SET) != 0) { fclose(fp); return 0; }
    fwrite(&c, sizeof(Candidate), 1, fp);
    fclose(fp);
    return 1;
}
