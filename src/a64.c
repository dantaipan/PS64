#include "a64.h"

#include "c64api.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- JSON helpers (flat objects inside arrays, as the API returns) --------- */

int a64_json_next_object(const char **p, const char *end, const char **obj, size_t *len)
{
    const char *s = *p;
    while (s < end && *s != '{')
        s++;
    if (s >= end)
        return -1;
    int depth = 0, in_str = 0;
    for (const char *q = s; q < end; q++) {
        if (in_str) {
            if (*q == '\\' && q + 1 < end) q++;
            else if (*q == '"') in_str = 0;
            continue;
        }
        if (*q == '"') in_str = 1;
        else if (*q == '{' || *q == '[') depth++;
        else if (*q == '}' || *q == ']') {
            if (--depth == 0) {
                *obj = s;
                *len = (size_t)(q - s + 1);
                *p = q + 1;
                return 0;
            }
        }
    }
    return -1;
}

/* Position just after "key": at the top level of the object, or NULL. */
static const char *json_value(const char *obj, size_t len, const char *key)
{
    const char *end = obj + len;
    size_t kl = strlen(key);
    int depth = 0, in_str = 0;
    for (const char *q = obj; q < end; q++) {
        if (in_str) {
            if (*q == '\\' && q + 1 < end) q++;
            else if (*q == '"') in_str = 0;
            continue;
        }
        if (*q == '{' || *q == '[') { depth++; continue; }
        if (*q == '}' || *q == ']') { depth--; continue; }
        if (*q != '"')
            continue;
        if (depth == 1 && q + kl + 1 < end && !strncmp(q + 1, key, kl) && q[kl + 1] == '"') {
            const char *v = q + kl + 2;
            while (v < end && (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n')) v++;
            if (v < end && *v == ':') {
                v++;
                while (v < end && (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n')) v++;
                return v < end ? v : NULL;
            }
        }
        in_str = 1;
    }
    return NULL;
}

int a64_json_number(const char *obj, size_t len, const char *key, long long *out)
{
    const char *v = json_value(obj, len, key);
    if (!v || !(isdigit((unsigned char)*v) || *v == '-'))
        return -1;
    *out = strtoll(v, NULL, 10);
    return 0;
}

int a64_json_string(const char *obj, size_t len, const char *key, char *out, size_t cap)
{
    const char *v = json_value(obj, len, key);
    const char *end = obj + len;
    if (!v || *v != '"' || cap == 0)
        return -1;
    v++;
    size_t n = 0;
    while (v < end && *v != '"') {
        char c = *v++;
        if (c == '\\' && v < end) {
            c = *v++;
            if (c == 'n') c = ' ';
            else if (c == 't') c = ' ';
            else if (c == 'u') {                /* \uXXXX: keep ASCII, else '?' */
                unsigned code = 0;
                for (int i = 0; i < 4 && v < end; i++, v++)
                    code = code * 16 + (unsigned)(isdigit((unsigned char)*v) ? *v - '0'
                                                 : (tolower((unsigned char)*v) - 'a' + 10));
                c = code < 0x80 ? (char)code : '?';
            }
        }
        if (n + 1 < cap)
            out[n++] = c;
    }
    out[n] = 0;
    return 0;
}

void a64_decode_entities(char *s)
{
    static const struct { const char *code; char ch; } map[] = {
        { "&lt;", '<' }, { "&gt;", '>' }, { "&amp;", '&' }, { "&quot;", '"' },
        { "&#39;", '\'' }, { "&apos;", '\'' },
    };
    char *out = s;
    for (const char *p = s; *p; ) {
        size_t i, n = sizeof(map) / sizeof(map[0]);
        for (i = 0; i < n; i++)
            if (!strncmp(p, map[i].code, strlen(map[i].code)))
                break;
        if (i < n) {
            *out++ = map[i].ch;
            p += strlen(map[i].code);
        } else {
            *out++ = *p++;
        }
    }
    *out = 0;
}

int a64_parse_items(const char *json, size_t len, a64_item **out)
{
    int cap = 64, n = 0;
    a64_item *items = calloc((size_t)cap, sizeof(*items));
    const char *p = json, *end = json + len, *obj;
    size_t olen;
    while (items && a64_json_next_object(&p, end, &obj, &olen) == 0) {
        a64_item it;
        memset(&it, 0, sizeof(it));
        long long v;
        if (a64_json_string(obj, olen, "id", it.id, sizeof(it.id)) != 0 ||
            a64_json_number(obj, olen, "category", &v) != 0)
            continue;
        it.category = (int)v;
        a64_json_string(obj, olen, "name", it.name, sizeof(it.name));
        if (a64_json_string(obj, olen, "group", it.group, sizeof(it.group)) != 0 || !it.group[0])
            a64_json_string(obj, olen, "handle", it.group, sizeof(it.group));
        if (a64_json_number(obj, olen, "year", &v) == 0) it.year = (int)v;
        if (a64_json_number(obj, olen, "rating", &v) == 0) it.rating = (int)v;
        if (a64_json_string(obj, olen, "released", it.released, sizeof(it.released)) != 0)
            it.released[0] = 0;
        a64_decode_entities(it.name);           /* some names arrive as "Hello &lt;3" */
        a64_decode_entities(it.group);
        if (n == cap) {
            cap *= 2;
            a64_item *bigger = realloc(items, sizeof(*items) * (size_t)cap);
            if (!bigger) break;
            items = bigger;
        }
        items[n++] = it;
    }
    *out = items;
    return n;
}

int a64_parse_files(const char *json, size_t len, a64_file **out)
{
    /* {"contentEntry":[{...},...]}: walk the objects inside the array. */
    const char *arr = strstr(json, "\"contentEntry\"");
    const char *end = json + len;
    int cap = 16, n = 0;
    a64_file *files = calloc((size_t)cap, sizeof(*files));
    const char *p = arr ? strchr(arr, '[') : NULL, *obj;
    size_t olen;
    while (files && p && a64_json_next_object(&p, end, &obj, &olen) == 0) {
        a64_file f;
        memset(&f, 0, sizeof(f));
        long long v;
        if (a64_json_string(obj, olen, "path", f.path, sizeof(f.path)) != 0 ||
            a64_json_number(obj, olen, "id", &v) != 0)
            continue;
        f.id = (int)v;
        if (a64_json_number(obj, olen, "size", &v) == 0) f.size = (unsigned long long)v;
        if (n == cap) {
            cap *= 2;
            a64_file *bigger = realloc(files, sizeof(*files) * (size_t)cap);
            if (!bigger) break;
            files = bigger;
        }
        files[n++] = f;
    }
    *out = files;
    return n;
}

int a64_parse_categories(const char *json, size_t len, a64_categories *out)
{
    /* [{"type":"category","values":[{"id":..,"aqlKey":"games","name":"Games"},..]},..] */
    memset(out, 0, sizeof(*out));
    const char *p = json, *end = json + len, *obj;
    size_t olen;
    while (a64_json_next_object(&p, end, &obj, &olen) == 0) {
        char type[40];
        if (a64_json_string(obj, olen, "type", type, sizeof(type)) != 0)
            continue;
        const char *vals = json_value(obj, olen, "values");
        const char *vend = obj + olen, *e;
        size_t elen;
        if (!strcasecmp(type, "subcat")) {
            /* {"id":0,"aqlKey":"games","name":"CSDB games"}: CSDb's own category numbers. */
            out->have_subcats = 1;
            while (vals && a64_json_next_object(&vals, vend, &e, &elen) == 0) {
                char name[64];
                long long id;
                if (a64_json_string(e, elen, "name", name, sizeof(name)) == 0 &&
                    !strncasecmp(name, "CSDB", 4) && a64_json_number(e, elen, "id", &id) == 0 &&
                    out->ncsdb < A64_MAX_CATS)
                    out->csdb[out->ncsdb++] = (int)id;
            }
            continue;
        }
        if (strcasecmp(type, "category") || out->count)
            continue;
        while (vals && out->count < A64_MAX_CATS && a64_json_next_object(&vals, vend, &e, &elen) == 0) {
            if (a64_json_string(e, elen, "aqlKey", out->keys[out->count], sizeof(out->keys[0])) != 0)
                continue;
            if (a64_json_string(e, elen, "name", out->names[out->count], sizeof(out->names[0])) != 0)
                snprintf(out->names[out->count], sizeof(out->names[0]), "%s", out->keys[out->count]);
            long long id;
            out->ids[out->count] = a64_json_number(e, elen, "id", &id) == 0 ? (int)id : -1;
            out->count++;
        }
    }
    return out->count;
}

int a64_is_csdb(const a64_categories *c, int category)
{
    if (!c || !c->have_subcats)
        return category >= 0 && category <= 10;
    for (int i = 0; i < c->ncsdb; i++)
        if (c->csdb[i] == category)
            return 1;
    return 0;
}

const char *a64_category_name(const a64_categories *c, int id)
{
    for (int i = 0; i < c->count; i++)
        if (c->ids[i] == id)
            return c->names[i];
    return NULL;
}

void a64_build_query(const char *text, const char *category_key, char *out, size_t cap)
{
    char esc[256];
    size_t n = 0;
    for (const char *t = text; *t && n + 2 < sizeof(esc); t++) {
        if (*t == '"' || *t == '\\')
            esc[n++] = '\\';
        esc[n++] = *t;
    }
    esc[n] = 0;
    if (category_key && *category_key)
        snprintf(out, cap, "(name:\"%s\") & (category:%s)", esc, category_key);
    else
        snprintf(out, cap, "(name:\"%s\")", esc);
}

void a64_build_latest_query(const char *window, const char *category_key, char *out, size_t cap)
{
    if (category_key && *category_key)
        snprintf(out, cap, "(latest:%s) & (category:%s) & (sort:year) & (order:desc)", window,
                 category_key);
    else
        snprintf(out, cap, "(latest:%s) & (sort:year) & (order:desc)", window);
}

static int latest_order(const void *pa, const void *pb)
{
    const a64_item *a = pa, *b = pb;
    if (!a->released[0] != !b->released[0])
        return a->released[0] ? -1 : 1;                 /* dated first */
    int c = strcmp(b->released, a->released);           /* ISO dates: newest first */
    return c ? c : strcasecmp(a->name, b->name);
}

void a64_sort_latest(a64_item *items, int n)
{
    if (n > 1)
        qsort(items, (size_t)n, sizeof(*items), latest_order);
}

void a64_unwrap_multipart(const char *content_type, unsigned char **data, size_t *len)
{
    const char *b = content_type ? strstr(content_type, "boundary=") : NULL;
    if (!b || strncasecmp(content_type, "multipart/", 10))
        return;
    char boundary[128];
    b += 9;
    if (*b == '"') b++;
    size_t bl = strcspn(b, "\";\r\n");
    snprintf(boundary, sizeof(boundary), "--%.*s", (int)bl, b);
    size_t blen = strlen(boundary);
    unsigned char *d = *data, *end = d + *len;
    /* First part: after the boundary line and its headers' blank line. */
    unsigned char *start = NULL;
    for (unsigned char *q = d; q + blen <= end; q++)
        if (!memcmp(q, boundary, blen)) { start = q + blen; break; }
    if (!start)
        return;
    unsigned char *body = NULL;
    for (unsigned char *q = start; q + 4 <= end; q++)
        if (!memcmp(q, "\r\n\r\n", 4)) { body = q + 4; break; }
    if (!body)
        return;
    unsigned char *stop = end;
    for (unsigned char *q = body; q + blen + 2 <= end; q++)
        if (q[0] == '\r' && q[1] == '\n' && !memcmp(q + 2, boundary, blen)) { stop = q; break; }
    size_t n = (size_t)(stop - body);
    memmove(d, body, n);
    *len = n;
}

/* ---- worker ------------------------------------------------------------------ */

enum { REQ_NONE, REQ_SEARCH, REQ_ENTRIES, REQ_FILE, REQ_CATS, REQ_LATEST };

struct a64 {
    char            host[128], base[128], client_id[128];
    int             port;
    pthread_t       thread;
    pthread_mutex_t lock;
    pthread_cond_t  wake;
    int             quit, busy;
    int             req;
    char            req_text[128], req_cat[40], req_item[64], req_name[160];
    int             req_offset, req_category, req_file, req_action;
    a64_result      result;
    int             have_result;
};

static void describe_error(int status, a64_result *res)
{
    if (status < 0) {
        snprintf(res->message, sizeof(res->message), "Can't reach Assembly64");
    } else if (status == 401 || status == 403 || status == 464) {
        res->auth_error = 1;
        snprintf(res->message, sizeof(res->message),
                 "Assembly64 didn't accept the client ID (HTTP %d)", status);
    } else {
        snprintf(res->message, sizeof(res->message), "Assembly64 error (HTTP %d)", status);
    }
}

static int get(a64 *a, const char *path, char **body, size_t *len, char *type, size_t type_cap)
{
    c64api api = { .host = a->host, .port = a->port, .client_id = a->client_id, .timeout_s = 20 };
    char full[1200];
    snprintf(full, sizeof(full), "%s%s", a->base, path);
    return c64api_request_alloc(&api, "GET", full, NULL, NULL, 0, body, len, type, type_cap);
}

static void do_request(a64 *a, int req, a64_result *res)
{
    char path[1100], *body = NULL, type[128] = "";
    size_t len = 0;
    int status;
    switch (req) {
    case REQ_SEARCH: {
        char q[400], enc[1000];
        a64_build_query(a->req_text, a->req_cat, q, sizeof(q));
        c64api_url_encode(q, enc, sizeof(enc));
        snprintf(path, sizeof(path), "/search/aql/%d/%d?query=%s", a->req_offset, A64_PAGE, enc);
        status = get(a, path, &body, &len, NULL, 0);
        if (status == 200) {
            res->kind = A64R_SEARCH;
            res->offset = a->req_offset;
            res->count = a64_parse_items(body, len, &res->items);
        }
        break;
    }
    case REQ_ENTRIES: {
        char enc[200];
        c64api_url_encode(a->req_item, enc, sizeof(enc));
        snprintf(path, sizeof(path), "/search/entries/%s/%d", enc, a->req_category);
        status = get(a, path, &body, &len, NULL, 0);
        if (status == 200) {
            res->kind = A64R_ENTRIES;
            snprintf(res->item_id, sizeof(res->item_id), "%s", a->req_item);
            res->category = a->req_category;
            res->nfiles = a64_parse_files(body, len, &res->files);
        }
        break;
    }
    case REQ_FILE: {
        char enc[200];
        c64api_url_encode(a->req_item, enc, sizeof(enc));
        snprintf(path, sizeof(path), "/search/bin/%s/%d/%d", enc, a->req_category, a->req_file);
        status = get(a, path, &body, &len, type, sizeof(type));
        if (status == 200 && len > 0) {
            unsigned char *data = (unsigned char *)body;
            a64_unwrap_multipart(type, &data, &len);
            res->kind = A64R_FILE;
            res->data = data;
            res->len = len;
            res->action = a->req_action;
            snprintf(res->name, sizeof(res->name), "%s", a->req_name);
            body = NULL;                       /* handed to the result */
        } else if (status == 200) {
            status = 204;
        }
        break;
    }
    case REQ_LATEST: {
        /* What Assembly64 added recently, newest years first; then by release date. A window
         * holding fewer than A64_LATEST dated items is widened. */
        static const char *const windows[] = { "1month", "3months", "1year" };
        status = 0;
        for (size_t w = 0; w < sizeof(windows) / sizeof(windows[0]); w++) {
            char q[200], enc[600];
            a64_build_latest_query(windows[w], a->req_cat, q, sizeof(q));
            c64api_url_encode(q, enc, sizeof(enc));
            snprintf(path, sizeof(path), "/search/aql/0/200?query=%s", enc);
            free(body);
            body = NULL;
            status = get(a, path, &body, &len, NULL, 0);
            if (status != 200)
                break;
            free(res->items);
            res->items = NULL;
            res->count = a64_parse_items(body, len, &res->items);
            int dated = 0;
            for (int i = 0; i < res->count; i++)
                dated += res->items[i].released[0] != 0;
            if (dated >= A64_LATEST)
                break;
        }
        if (status == 200) {
            a64_sort_latest(res->items, res->count);
            if (res->count > A64_LATEST)
                res->count = A64_LATEST;
            res->kind = A64R_SEARCH;
            res->offset = 0;
            res->latest = 1;
        } else {
            free(res->items);
            res->items = NULL;
            res->count = 0;
        }
        break;
    }
    case REQ_CATS:
        status = get(a, "/search/aql/presets", &body, &len, NULL, 0);
        if (status == 200) {
            res->kind = A64R_CATEGORIES;
            a64_parse_categories(body, len, &res->cats);
        }
        break;
    default:
        return;
    }
    if (status != 200) {
        res->kind = A64R_ERROR;
        describe_error(status, res);
    }
    free(body);
}

static void *worker(void *arg)
{
    a64 *a = arg;
    for (;;) {
        pthread_mutex_lock(&a->lock);
        while (!a->quit && a->req == REQ_NONE)
            pthread_cond_wait(&a->wake, &a->lock);
        if (a->quit) {
            pthread_mutex_unlock(&a->lock);
            return NULL;
        }
        int req = a->req;
        a->req = REQ_NONE;
        a->busy = 1;
        pthread_mutex_unlock(&a->lock);

        a64_result res;
        memset(&res, 0, sizeof(res));
        do_request(a, req, &res);

        pthread_mutex_lock(&a->lock);
        if (a->have_result)
            a64_result_free(&a->result);      /* superseded, never collected */
        a->result = res;
        a->have_result = 1;
        a->busy = 0;
        pthread_mutex_unlock(&a->lock);
    }
}

a64 *a64_start(const char *base_url, const char *client_id)
{
    a64 *a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    if (c64api_parse_url(base_url ? base_url : A64_DEFAULT_URL, a->host, sizeof(a->host),
                         &a->port, a->base, sizeof(a->base)) != 0) {
        free(a);
        return NULL;
    }
    snprintf(a->client_id, sizeof(a->client_id), "%s", client_id);
    pthread_mutex_init(&a->lock, NULL);
    pthread_cond_init(&a->wake, NULL);
    if (pthread_create(&a->thread, NULL, worker, a) != 0) {
        free(a);
        return NULL;
    }
    return a;
}

void a64_stop(a64 *a)
{
    if (!a)
        return;
    pthread_mutex_lock(&a->lock);
    a->quit = 1;
    pthread_cond_signal(&a->wake);
    pthread_mutex_unlock(&a->lock);
    pthread_join(a->thread, NULL);
    if (a->have_result)
        a64_result_free(&a->result);
    pthread_mutex_destroy(&a->lock);
    pthread_cond_destroy(&a->wake);
    free(a);
}

static void request(a64 *a, int req)
{
    a->req = req;
    pthread_cond_signal(&a->wake);
    pthread_mutex_unlock(&a->lock);
}

void a64_search(a64 *a, const char *text, const char *category_key, int offset)
{
    pthread_mutex_lock(&a->lock);
    snprintf(a->req_text, sizeof(a->req_text), "%s", text);
    snprintf(a->req_cat, sizeof(a->req_cat), "%s", category_key ? category_key : "");
    a->req_offset = offset;
    request(a, REQ_SEARCH);
}

void a64_latest(a64 *a, const char *category_key)
{
    pthread_mutex_lock(&a->lock);
    snprintf(a->req_cat, sizeof(a->req_cat), "%s", category_key ? category_key : "");
    request(a, REQ_LATEST);
}

void a64_entries(a64 *a, const char *item_id, int category)
{
    pthread_mutex_lock(&a->lock);
    snprintf(a->req_item, sizeof(a->req_item), "%s", item_id);
    a->req_category = category;
    request(a, REQ_ENTRIES);
}

void a64_download(a64 *a, const char *item_id, int category, int file_id, const char *name,
                  int action)
{
    pthread_mutex_lock(&a->lock);
    snprintf(a->req_item, sizeof(a->req_item), "%s", item_id);
    snprintf(a->req_name, sizeof(a->req_name), "%s", name);
    a->req_category = category;
    a->req_file = file_id;
    a->req_action = action;
    request(a, REQ_FILE);
}

void a64_load_categories(a64 *a)
{
    pthread_mutex_lock(&a->lock);
    request(a, REQ_CATS);
}

int a64_busy(a64 *a)
{
    pthread_mutex_lock(&a->lock);
    int b = a->busy || a->req != REQ_NONE;
    pthread_mutex_unlock(&a->lock);
    return b;
}

int a64_poll(a64 *a, a64_result *res)
{
    pthread_mutex_lock(&a->lock);
    int have = a->have_result;
    if (have) {
        *res = a->result;
        a->have_result = 0;
    }
    pthread_mutex_unlock(&a->lock);
    return have;
}

void a64_result_free(a64_result *res)
{
    free(res->items);
    free(res->files);
    free(res->data);
    res->items = NULL;
    res->files = NULL;
    res->data = NULL;
}
