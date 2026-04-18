#include "../headers/voter.h"
#include "../headers/file_io.h"
#include <stdio.h>
#include <string.h>

/* ── Internal helpers ────────────────────────────────────────────
 * All helpers are file-scoped (static) — not part of the public API.
 */

/* Returns 1 if username does NOT exist in voters.dat, 0 if it does. */
static int validate_voter_username_unique(const char *username)
{
    FILE *fp = open_voters_file("rb");
    if (!fp) return 1;          /* File absent → trivially unique */

    Voter v;
    while (fread(&v, sizeof(Voter), 1, fp) == 1) {
        if (v.is_registered && strcmp(v.username, username) == 0) {
            fclose(fp);
            return 0;           /* Duplicate found */
        }
    }
    fclose(fp);
    return 1;
}

/* Auto-increment: count existing records → next ID is count + 1.
 * Because records are never physically deleted (only flagged), the
 * file size gives us a monotonically increasing sequence. */
static int generate_voter_id(void)
{
    return (int)get_record_count(VOTERS_FILE, sizeof(Voter)) + 1;
}

/* Append a Voter struct to voters.dat.  Returns 1 on success. */
static int write_voter_record(const Voter *v)
{
    FILE *fp = open_voters_file("ab");
    if (!fp) return 0;
    size_t ok = fwrite(v, sizeof(Voter), 1, fp);
    fclose(fp);
    return (ok == 1);
}

/* Linear scan for a voter by username.
 * Fills *out on success.  Returns 1 on success, 0 on failure. */
static int find_voter_by_username(const char *username, Voter *out)
{
    FILE *fp = open_voters_file("rb");
    if (!fp) return 0;

    while (fread(out, sizeof(Voter), 1, fp) == 1) {
        if (out->is_registered && strcmp(out->username, username) == 0) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

/* O(1) direct seek using voter_id as a file index.
 * voter_id is 1-based → record offset = (voter_id − 1) × sizeof(Voter). */
static int seek_voter_record(int voter_id, Voter *out)
{
    if (voter_id <= 0) return 0;

    FILE *fp = open_voters_file("rb");
    if (!fp) return 0;

    long offset = (long)(voter_id - 1) * (long)sizeof(Voter);
    if (fseek(fp, offset, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    int ok = (fread(out, sizeof(Voter), 1, fp) == 1);
    fclose(fp);
    return ok;
}

/* ── Public API ──────────────────────────────────────────────────*/

int register_voter(const char *full_name,
                   const char *username,
                   const char *password)
{
    if (!validate_voter_username_unique(username))
        return -1;  /* ERR|USERNAME_TAKEN */

    Voter v;
    memset(&v, 0, sizeof(Voter));
    v.voter_id      = generate_voter_id();
    v.is_registered = 1;
    /* voted_positions[] zero-initialised by memset → all unvoted */

    strncpy(v.full_name, full_name, MAX_NAME_LEN - 1);
    strncpy(v.username,  username,  MAX_NAME_LEN - 1);
    strncpy(v.password,  password,  MAX_PASS_LEN - 1);

    if (!write_voter_record(&v))
        return -1;

    return v.voter_id;
}

int login_voter(const char *username, const char *password)
{
    Voter v;
    if (!find_voter_by_username(username, &v))
        return -1;                          /* user not found */
    if (strcmp(password, v.password) != 0)
        return -1;                          /* wrong password */
    return v.voter_id;
}

int has_voted_for_position(int voter_id, int position_id)
{
    if (position_id < 0 || position_id >= MAX_POSITIONS) return -1;

    Voter v;
    if (!seek_voter_record(voter_id, &v) || !v.is_registered) return -1;

    return v.voted_positions[position_id];
}

int get_voter_by_id(int voter_id, Voter *out)
{
    return seek_voter_record(voter_id, out);
}

int mark_position_voted(int voter_id, int position_id)
{
    if (voter_id <= 0 || position_id < 0 || position_id >= MAX_POSITIONS)
        return 0;

    /* Open for read+write without truncating */
    FILE *fp = fopen(VOTERS_FILE, "r+b");
    if (!fp) return 0;

    long offset = (long)(voter_id - 1) * (long)sizeof(Voter);

    if (fseek(fp, offset, SEEK_SET) != 0) { fclose(fp); return 0; }

    Voter v;
    if (fread(&v, sizeof(Voter), 1, fp) != 1) { fclose(fp); return 0; }

    v.voted_positions[position_id] = 1;

    /* Seek back to record start and overwrite */
    if (fseek(fp, offset, SEEK_SET) != 0) { fclose(fp); return 0; }
    fwrite(&v, sizeof(Voter), 1, fp);
    fclose(fp);
    return 1;
}
