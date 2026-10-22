/*
 * a64 - Assembly64 (hackerswithstyle.se) client: keyword search, an item's
 * file list and file downloads, on a worker thread. Every request carries
 * the user's client-id header; nothing works without one.
 */
#ifndef A64_H
#define A64_H

#include <stddef.h>

#define A64_DEFAULT_URL "http://hackerswithstyle.se/leet"
#define A64_PAGE        50
#define A64_LATEST      25    /* items in "latest releases" */
#define A64_MAX_CATS    32

typedef struct {
    char id[64];
    int  category;
    char name[96];
    char group[64];
    int  year;
    int  rating;
    char released[11];        /* "YYYY-MM-DD", or empty if unknown */
} a64_item;

typedef struct {
    int                id;
    char               path[160];
    unsigned long long size;
} a64_file;

typedef struct {
    int  count;
    char names[A64_MAX_CATS][40];     /* shown */
    char keys[A64_MAX_CATS][40];      /* aqlKey used in queries */
    int  ids[A64_MAX_CATS];           /* category number, as in ContentItem.category */
    /* Result category numbers that come from CSDb (their ids are CSDb release ids). */
    int  csdb[A64_MAX_CATS];
    int  ncsdb, have_subcats;
} a64_categories;

/* Whether a result's category number is a CSDb one (presets' "CSDB ..." subcategories;
 * 0..10 if the presets haven't said). */
int a64_is_csdb(const a64_categories *c, int category);

/* Display name of a category number, or NULL if unknown. */
const char *a64_category_name(const a64_categories *c, int id);

enum { A64R_SEARCH, A64R_ENTRIES, A64R_FILE, A64R_CATEGORIES, A64R_ERROR };

typedef struct {
    int       kind;
    char      message[160];
    int       auth_error;             /* the server rejected the client ID */

    /* A64R_SEARCH (items malloc'd; caller frees) */
    a64_item *items;
    int       count, offset;
    int       latest;                 /* the "latest releases" list (no more pages) */

    /* A64R_ENTRIES (files malloc'd; caller frees) */
    a64_file *files;
    int       nfiles;
    char      item_id[64];
    int       category;

    /* A64R_FILE (data malloc'd; caller frees or hands on) */
    char           name[160];
    unsigned char *data;
    size_t         len;
    int            action;            /* passed through from a64_download */

    /* A64R_CATEGORIES */
    a64_categories cats;
} a64_result;

typedef struct a64 a64;

a64 *a64_start(const char *base_url, const char *client_id);
void a64_stop(a64 *a);
void a64_search(a64 *a, const char *text, const char *category_key, int offset);
/*
 * The A64_LATEST newest releases (by release date) in a category: what Assembly64
 * added in the last month (widening to 3 months, then a year, if that holds fewer),
 * sorted by release date. Arrives as an A64R_SEARCH result with latest set.
 */
void a64_latest(a64 *a, const char *category_key);
void a64_entries(a64 *a, const char *item_id, int category);
void a64_download(a64 *a, const char *item_id, int category, int file_id, const char *name,
                  int action);
void a64_load_categories(a64 *a);
int  a64_busy(a64 *a);
/* Returns 1 and fills res when a request has finished. */
int  a64_poll(a64 *a, a64_result *res);
void a64_result_free(a64_result *res);

/* ---- pure helpers, exposed for tests ------------------------------------- */

/* AQL for a name search: (name:"text") [& (category:key)], quotes escaped. A key with a
 * colon is a whole filter: "type:sid" gives (name:"text") & (type:sid). */
void a64_build_query(const char *text, const char *category_key, char *out, size_t cap);
/* AQL for recent additions, newest years first: (latest:window) [& (category:key)] & sort. */
void a64_build_latest_query(const char *window, const char *category_key, char *out, size_t cap);
/* The HTML entities some names arrive with (&lt; &gt; &amp; &quot; &#39;), in place. */
void a64_decode_entities(char *s);
/* Newest release date first; items without a date last; then by name. */
void a64_sort_latest(a64_item *items, int n);

/* Find the next {...} object in a JSON array; returns 0 and sets obj/len. */
int  a64_json_next_object(const char **p, const char *end, const char **obj, size_t *len);
/* Integer value of "key" within [obj, obj+len); returns 0 if found. */
int  a64_json_number(const char *obj, size_t len, const char *key, long long *out);
/* String value of "key" within [obj, obj+len) (top level of that object). */
int  a64_json_string(const char *obj, size_t len, const char *key, char *out, size_t cap);

int  a64_parse_items(const char *json, size_t len, a64_item **out);
int  a64_parse_files(const char *json, size_t len, a64_file **out);
int  a64_parse_categories(const char *json, size_t len, a64_categories *out);

/* If body is multipart, point at its first part's content (in place). */
void a64_unwrap_multipart(const char *content_type, unsigned char **data, size_t *len);

#endif
