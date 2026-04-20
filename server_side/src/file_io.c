#include "../headers/file_io.h"
#include <stdlib.h>

FILE *open_voters_file(const char *mode)
{
    return fopen(VOTERS_FILE, mode);
}

FILE *open_candidates_file(const char *mode)
{
    return fopen(CANDIDATES_FILE, mode);
}

long get_record_count(const char *filename, size_t record_size)
{
    if (record_size == 0) return 0L;

    FILE *fp = fopen(filename, "rb");
    if (!fp) return 0L;          /* File does not exist yet */

    fseek(fp, 0L, SEEK_END);
    long bytes = ftell(fp);
    fclose(fp);

    return bytes / (long)record_size;
}
