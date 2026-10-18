#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 512
#define LINE_LEN  512

/* If line sets key (optionally commented out with '#'), return 1. */
static int line_has_key(const char *line, const char *key, int *commented)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    *commented = 0;
    if (*p == '#') {
        *commented = 1;
        p++;
        while (*p == ' ' || *p == '\t') p++;
    }
    size_t n = strlen(key);
    if (strncmp(p, key, n) != 0)
        return 0;
    p += n;
    while (*p == ' ' || *p == '\t') p++;
    return *p == '=';
}

int config_set(const char *path, const char *key, const char *value,
               const char *const *also_comment_out)
{
    static char lines[MAX_LINES][LINE_LEN];
    int count = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        while (count < MAX_LINES && fgets(lines[count], LINE_LEN, f)) {
            size_t len = strlen(lines[count]);
            if (len && lines[count][len - 1] == '\n')
                lines[count][--len] = 0;
            if (len && lines[count][len - 1] == '\r')
                lines[count][--len] = 0;
            count++;
        }
        fclose(f);
    }

    int active = -1, commented_at = -1;
    for (int i = 0; i < count; i++) {
        int commented;
        if (line_has_key(lines[i], key, &commented)) {
            if (!commented && active < 0)
                active = i;
            else if (commented && commented_at < 0)
                commented_at = i;
        }
    }
    int target = active >= 0 ? active : commented_at;
    if (target < 0) {
        if (count >= MAX_LINES)
            return -1;
        target = count++;
    }
    snprintf(lines[target], LINE_LEN, "%s = %s", key, value);

    /* Comment out other active lines for the same key, and any superseded keys. */
    for (int i = 0; i < count; i++) {
        if (i == target)
            continue;
        int commented, hit = line_has_key(lines[i], key, &commented) && !commented;
        for (int k = 0; !hit && also_comment_out && also_comment_out[k]; k++)
            hit = line_has_key(lines[i], also_comment_out[k], &commented) && !commented;
        if (hit) {
            char tmp[LINE_LEN];
            snprintf(tmp, sizeof(tmp), "# %s", lines[i]);
            memcpy(lines[i], tmp, LINE_LEN);
        }
    }

    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    f = fopen(tmp_path, "w");
    if (!f)
        return -1;
    for (int i = 0; i < count; i++)
        fprintf(f, "%s\n", lines[i]);
    if (fclose(f) != 0 || rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return -1;
    }
    return 0;
}
