#include "favs.h"

#include "c64files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void fav_init(fav_list *l, int max)
{
    memset(l, 0, sizeof(*l));
    l->max = max;
}

void fav_free(fav_list *l)
{
    free(l->e);
    l->e = NULL;
    l->count = l->cap = 0;
}

static int same(const fav_entry *a, const fav_entry *b)
{
    if (a->kind != b->kind)
        return 0;
    if (a->kind == FAV_LOCAL_FILE || a->kind == FAV_LOCAL_FOLDER)
        return !strcmp(a->path, b->path);
    if (strcmp(a->item_id, b->item_id) || a->category != b->category)
        return 0;
    return a->kind == FAV_A64_ITEM || a->file_id == b->file_id;
}

int fav_find(const fav_list *l, const fav_entry *e)
{
    for (int i = 0; i < l->count; i++)
        if (same(&l->e[i], e))
            return i;
    return -1;
}

static int grow(fav_list *l)
{
    if (l->count < l->cap)
        return 0;
    int cap = l->cap ? l->cap * 2 : 16;
    fav_entry *e = realloc(l->e, sizeof(*e) * (size_t)cap);
    if (!e)
        return -1;
    l->e = e;
    l->cap = cap;
    return 0;
}

void fav_remove(fav_list *l, int i)
{
    if (i < 0 || i >= l->count)
        return;
    memmove(&l->e[i], &l->e[i + 1], sizeof(*l->e) * (size_t)(l->count - i - 1));
    l->count--;
}

int fav_toggle(fav_list *l, const fav_entry *e)
{
    int i = fav_find(l, e);
    if (i >= 0) {
        fav_remove(l, i);
        return 0;
    }
    if (grow(l) != 0)
        return 0;
    l->e[l->count++] = *e;
    return 1;
}

void fav_add_recent(fav_list *l, const fav_entry *e)
{
    fav_entry copy = *e;                     /* e may point into l */
    int i = fav_find(l, &copy);
    if (i >= 0)
        fav_remove(l, i);
    if (grow(l) != 0)
        return;
    memmove(&l->e[1], &l->e[0], sizeof(*l->e) * (size_t)l->count);
    l->e[0] = copy;
    l->count++;
    if (l->max && l->count > l->max)
        l->count = l->max;
}

int fav_count_group(const fav_list *l, int group)
{
    int n = 0;
    for (int i = 0; i < l->count; i++)
        n += l->e[i].group == group;
    return n;
}

int fav_nth_in_group(const fav_list *l, int group, int n)
{
    for (int i = 0; i < l->count; i++)
        if (l->e[i].group == group && n-- == 0)
            return i;
    return -1;
}

fav_entry fav_local(const char *path, int is_dir)
{
    fav_entry e;
    memset(&e, 0, sizeof(e));
    e.kind = is_dir ? FAV_LOCAL_FOLDER : FAV_LOCAL_FILE;
    e.group = is_dir ? FAV_GAMES : fav_group_for_name(path);
    e.action = FA_NONE;
    snprintf(e.path, sizeof(e.path), "%s", path);
    const char *last = strrchr(path, '/');
    const char *title = last ? last + 1 : path;
    if (!is_dir)
        for (const char *p = last ? last - 1 : NULL; p && p >= path; p--)
            if (*p == '/') {
                title = p + 1;
                break;
            }
    snprintf(e.name, sizeof(e.name), "%s", title);
    return e;
}

fav_entry fav_a64_file(const char *item_id, int category, int file_id, const char *file_name,
                       const char *item_name)
{
    fav_entry e;
    memset(&e, 0, sizeof(e));
    e.kind = FAV_A64_FILE;
    e.group = fav_group_for_name(file_name);
    e.action = FA_NONE;
    snprintf(e.item_id, sizeof(e.item_id), "%s", item_id);
    e.category = category;
    e.file_id = file_id;
    snprintf(e.path, sizeof(e.path), "%s", file_name);
    if (item_name && *item_name)
        snprintf(e.name, sizeof(e.name), "%s - %s", item_name, file_name);
    else
        snprintf(e.name, sizeof(e.name), "%s", file_name);
    return e;
}

int fav_group_for_name(const char *name)
{
    int t = files_type(name, 0);
    return t == FT_SID || t == FT_MOD ? FAV_SIDS : FAV_GAMES;
}

int fav_group_for_category(const char *key_or_name)
{
    if (!key_or_name)
        return FAV_GAMES;
    for (const char *p = key_or_name; *p; p++)
        if (!strncasecmp(p, "music", 5) || !strncasecmp(p, "sid", 3))
            return FAV_SIDS;
    return FAV_GAMES;
}

/* ---- file ----------------------------------------------------------------------
 * One entry per line: kind group action category file_id item_id name path,
 * separated by tabs (tabs and newlines in names are replaced by spaces). */

static void clean(char *s)
{
    for (; *s; s++)
        if (*s == '\t' || *s == '\n' || *s == '\r')
            *s = ' ';
}

int fav_save(const fav_list *l, const char *file)
{
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", file);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return -1;
    fprintf(f, "# c64view favourites v1\n");
    for (int i = 0; i < l->count; i++) {
        fav_entry e = l->e[i];
        clean(e.name);
        clean(e.path);
        clean(e.item_id);
        fprintf(f, "%d\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n", e.kind, e.group, e.action, e.category,
                e.file_id, e.item_id, e.name, e.path);
    }
    if (fclose(f) != 0 || rename(tmp, file) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

int fav_load(fav_list *l, const char *file)
{
    FILE *f = fopen(file, "r");
    if (!f)
        return -1;
    l->count = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#')
            continue;
        line[strcspn(line, "\r\n")] = 0;
        char *field[8];
        int n = 0;
        char *p = line;
        for (; n < 8; n++) {
            field[n] = p;
            char *tab = strchr(p, '\t');
            if (!tab) { n++; break; }
            *tab = 0;
            p = tab + 1;
        }
        if (n != 8)
            continue;
        fav_entry e;
        memset(&e, 0, sizeof(e));
        e.kind = atoi(field[0]);
        e.group = atoi(field[1]);
        e.action = atoi(field[2]);
        e.category = atoi(field[3]);
        e.file_id = atoi(field[4]);
        snprintf(e.item_id, sizeof(e.item_id), "%s", field[5]);
        snprintf(e.name, sizeof(e.name), "%s", field[6]);
        snprintf(e.path, sizeof(e.path), "%s", field[7]);
        if (e.kind < FAV_LOCAL_FILE || e.kind > FAV_A64_ITEM || grow(l) != 0)
            continue;
        l->e[l->count++] = e;
        if (l->max && l->count >= l->max)
            break;
    }
    fclose(f);
    return 0;
}
