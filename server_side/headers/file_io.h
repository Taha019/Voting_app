#ifndef FILE_IO_H
#define FILE_IO_H

#include <stdio.h>

/* ── Data file paths ─────────────────────────────────────────── */
#define VOTERS_FILE     "data/voters.dat"
#define CANDIDATES_FILE "data/candidates.dat"

/* Open voters.dat with the given fopen mode.
 * Returns NULL on failure (e.g. file does not exist for "rb"). */
FILE *open_voters_file(const char *mode);

/* Open candidates.dat with the given fopen mode.
 * Returns NULL on failure. */
FILE *open_candidates_file(const char *mode);

/* Count the number of fixed-size records currently in a file.
 * Returns 0 if the file does not exist or record_size is 0.
 * Used to auto-generate the next sequential record ID. */
long get_record_count(const char *filename, size_t record_size);

#endif /* FILE_IO_H */
