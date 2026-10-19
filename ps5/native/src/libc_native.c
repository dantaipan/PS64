/*
 * C library functions that the static libraries PS64 links (SDL, VICE and the
 * SDK's C++ runtime) expect, but that a native PS5 app's runtime doesn't
 * provide (or provides only through libScePosixForWebKit, which a native app
 * doesn't load: calling those would jump to address 0).
 *
 * PS64 only ever uses the "C" locale, so the *_l locale variants call the
 * plain functions and the locale objects are placeholders.
 *
 * The SDK's headers declare most of them, with locale_t from <xlocale.h>.
 */
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <ctype.h>
#include <locale.h>
#include <runetype.h>
#include <wctype.h>
#include <xlocale.h>

typedef locale_t ps64_locale;

/* ---- SDL's character-set code -------------------------------------------------- */

char *nl_langinfo(int item);
int ___mb_cur_max(void);
char *nl_langinfo(int item) { (void)item; return (char *)"UTF-8"; }
int ___mb_cur_max(void) { return 1; }

/* ---- locales: the "C" locale only ----------------------------------------------- */

static int c_locale;

ps64_locale newlocale(int mask, const char *name, ps64_locale base);
int freelocale(ps64_locale loc);
int ___mb_cur_max_l(ps64_locale loc);
unsigned long ___runetype_l(int c, ps64_locale loc);
int ___tolower_l(int c, ps64_locale loc);
int ___toupper_l(int c, ps64_locale loc);
_RuneLocale *__runes_for_locale(ps64_locale loc, int *mb_sb_limit);
struct lconv *localeconv_l(ps64_locale loc);

ps64_locale newlocale(int mask, const char *name, ps64_locale base)
{
    (void)mask; (void)name; (void)base;
    return (ps64_locale)&c_locale;
}
int freelocale(ps64_locale loc) { (void)loc; return 0; }
int ___mb_cur_max_l(ps64_locale loc) { (void)loc; return 1; }
/* Only asked for characters outside the cached 0-255 table: none in "C". */
unsigned long ___runetype_l(int c, ps64_locale loc) { (void)c; (void)loc; return 0; }
int ___tolower_l(int c, ps64_locale loc) { (void)loc; return c; }
int ___toupper_l(int c, ps64_locale loc) { (void)loc; return c; }

/* The system's "C" table. */
_RuneLocale *__runes_for_locale(ps64_locale loc, int *mb_sb_limit)
{
    (void)loc;
    if (mb_sb_limit)
        *mb_sb_limit = 256;
    return (_RuneLocale *)&_DefaultRuneLocale;
}
struct lconv *localeconv_l(ps64_locale loc) { (void)loc; return localeconv(); }

/* The current locale is always "C". */
unsigned long ___runetype(__ct_rune_t c) { (void)c; return 0; }
const _RuneLocale *__getCurrentRuneLocale(void) { return &_DefaultRuneLocale; }

/* Locale variants of standard functions. */
double strtod_l(const char *s, char **end, ps64_locale loc);
float strtof_l(const char *s, char **end, ps64_locale loc);
long double strtold_l(const char *s, char **end, ps64_locale loc);
long long strtoll_l(const char *s, char **end, int base, ps64_locale loc);
unsigned long long strtoull_l(const char *s, char **end, int base, ps64_locale loc);
size_t strftime_l(char *s, size_t max, const char *fmt, const struct tm *tm, ps64_locale loc);
int strcoll_l(const char *a, const char *b, ps64_locale loc);
size_t strxfrm_l(char *dst, const char *src, size_t n, ps64_locale loc);
int wcscoll_l(const wchar_t *a, const wchar_t *b, ps64_locale loc);
size_t wcsxfrm_l(wchar_t *dst, const wchar_t *src, size_t n, ps64_locale loc);
wint_t btowc_l(int c, ps64_locale loc);
int wctob_l(wint_t c, ps64_locale loc);
int iswctype_l(wint_t c, wctype_t type, ps64_locale loc);
size_t mbrlen_l(const char *s, size_t n, mbstate_t *ps, ps64_locale loc);
size_t mbrtowc_l(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps, ps64_locale loc);
int mbtowc_l(wchar_t *pwc, const char *s, size_t n, ps64_locale loc);
size_t wcrtomb_l(char *s, wchar_t wc, mbstate_t *ps, ps64_locale loc);
size_t mbsrtowcs_l(wchar_t *dst, const char **src, size_t len, mbstate_t *ps, ps64_locale loc);
size_t mbsnrtowcs_l(wchar_t *dst, const char **src, size_t nms, size_t len, mbstate_t *ps,
                    ps64_locale loc);
size_t wcsnrtombs_l(char *dst, const wchar_t **src, size_t nwc, size_t len, mbstate_t *ps,
                    ps64_locale loc);
int snprintf_l(char *s, size_t n, ps64_locale loc, const char *fmt, ...);
int asprintf_l(char **out, ps64_locale loc, const char *fmt, ...);
int sscanf_l(const char *s, ps64_locale loc, const char *fmt, ...);

double strtod_l(const char *s, char **end, ps64_locale loc) { (void)loc; return strtod(s, end); }
float strtof_l(const char *s, char **end, ps64_locale loc) { (void)loc; return strtof(s, end); }
long double strtold_l(const char *s, char **end, ps64_locale loc) { (void)loc; return strtold(s, end); }
long long strtoll_l(const char *s, char **end, int base, ps64_locale loc)
{
    (void)loc;
    return strtoll(s, end, base);
}
unsigned long long strtoull_l(const char *s, char **end, int base, ps64_locale loc)
{
    (void)loc;
    return strtoull(s, end, base);
}
size_t strftime_l(char *s, size_t max, const char *fmt, const struct tm *tm, ps64_locale loc)
{
    (void)loc;
    return strftime(s, max, fmt, tm);
}
int strcoll_l(const char *a, const char *b, ps64_locale loc) { (void)loc; return strcoll(a, b); }
size_t strxfrm_l(char *dst, const char *src, size_t n, ps64_locale loc)
{
    (void)loc;
    return strxfrm(dst, src, n);
}
int wcscoll_l(const wchar_t *a, const wchar_t *b, ps64_locale loc) { (void)loc; return wcscoll(a, b); }
size_t wcsxfrm_l(wchar_t *dst, const wchar_t *src, size_t n, ps64_locale loc)
{
    (void)loc;
    return wcsxfrm(dst, src, n);
}
wint_t btowc_l(int c, ps64_locale loc) { (void)loc; return btowc(c); }
int wctob_l(wint_t c, ps64_locale loc) { (void)loc; return wctob(c); }
int iswctype_l(wint_t c, wctype_t type, ps64_locale loc) { (void)loc; return iswctype(c, type); }
size_t mbrlen_l(const char *s, size_t n, mbstate_t *ps, ps64_locale loc)
{
    (void)loc;
    return mbrlen(s, n, ps);
}
size_t mbrtowc_l(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps, ps64_locale loc)
{
    (void)loc;
    return mbrtowc(pwc, s, n, ps);
}
int mbtowc_l(wchar_t *pwc, const char *s, size_t n, ps64_locale loc) { (void)loc; return mbtowc(pwc, s, n); }
size_t wcrtomb_l(char *s, wchar_t wc, mbstate_t *ps, ps64_locale loc)
{
    (void)loc;
    return wcrtomb(s, wc, ps);
}
size_t mbsrtowcs_l(wchar_t *dst, const char **src, size_t len, mbstate_t *ps, ps64_locale loc)
{
    (void)loc;
    return mbsrtowcs(dst, src, len, ps);
}

/* "C" locale: one byte is one character. */
size_t mbsnrtowcs_l(wchar_t *dst, const char **src, size_t nms, size_t len, mbstate_t *ps,
                    ps64_locale loc)
{
    (void)ps; (void)loc;
    const unsigned char *s = (const unsigned char *)*src;
    size_t n = 0;
    while (n < nms && (!dst || n < len)) {
        if (dst)
            dst[n] = s[n];
        if (!s[n]) {
            if (dst)
                *src = NULL;
            return n;
        }
        n++;
    }
    if (dst)
        *src = (const char *)s + n;
    return n;
}

size_t wcsnrtombs_l(char *dst, const wchar_t **src, size_t nwc, size_t len, mbstate_t *ps,
                    ps64_locale loc)
{
    (void)ps; (void)loc;
    const wchar_t *s = *src;
    size_t n = 0;
    while (n < nwc && (!dst || n < len)) {
        if (s[n] < 0 || s[n] > 255) {
            errno = EILSEQ;
            return (size_t)-1;
        }
        if (dst)
            dst[n] = (char)s[n];
        if (!s[n]) {
            if (dst)
                *src = NULL;
            return n;
        }
        n++;
    }
    if (dst)
        *src = s + n;
    return n;
}

int snprintf_l(char *s, size_t n, ps64_locale loc, const char *fmt, ...)
{
    (void)loc;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

int asprintf_l(char **out, ps64_locale loc, const char *fmt, ...)
{
    (void)loc;
    va_list ap;
    va_start(ap, fmt);
    int r = vasprintf(out, fmt, ap);
    va_end(ap);
    return r;
}

int sscanf_l(const char *s, ps64_locale loc, const char *fmt, ...)
{
    (void)loc;
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}

/* ---- message catalogs: none --------------------------------------------------------- */

void *catopen(const char *name, int flag);
char *catgets(void *cat, int set, int msg, const char *s);
int catclose(void *cat);
void *catopen(const char *name, int flag) { (void)name; (void)flag; return (void *)-1; }
char *catgets(void *cat, int set, int msg, const char *s) { (void)cat; (void)set; (void)msg; return (char *)s; }
int catclose(void *cat) { (void)cat; return 0; }

/* ---- functions only libScePosixForWebKit has -------------------------------------- */

int isatty(int fd);
char *strcasestr(const char *haystack, const char *needle);
uint32_t arc4random(void);
int isatty(int fd) { (void)fd; return 0; }

char *strcasestr(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (; *haystack; haystack++) {
        size_t i = 0;
        while (i < n && haystack[i]) {
            int a = (unsigned char)haystack[i], b = (unsigned char)needle[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b)
                break;
            i++;
        }
        if (i == n)
            return (char *)haystack;
    }
    return n ? NULL : (char *)haystack;
}

/* Not cryptographic; nothing in PS64 needs it to be (C++ random_device, VICE). */
uint32_t arc4random(void)
{
    static uint64_t state;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    if (!state)
        state = (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)&state ^ 0x9E3779B97F4A7C15ull;
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    uint32_t r = (uint32_t)(state >> 16);
    pthread_mutex_unlock(&lock);
    return r;
}

/* ---- odds and ends ------------------------------------------------------------------ */

struct timespec;
int utimensat(int fd, const char *path, const struct timespec times[2], int flag);
int utimensat(int fd, const char *path, const struct timespec times[2], int flag)
{
    (void)fd; (void)path; (void)times; (void)flag;
    errno = ENOSYS;
    return -1;
}

void __assert(const char *func, const char *file, int line, const char *expr);
void __assert(const char *func, const char *file, int line, const char *expr)
{
    fprintf(stderr, "assertion failed: %s (%s:%d, %s)\n", expr, file, line, func ? func : "?");
    abort();
}

void *__dso_handle = &__dso_handle;

/* Destructors of C++ thread_local objects: not run (PS64 doesn't rely on them). */
int __cxa_thread_atexit_impl(void (*dtor)(void *), void *obj, void *dso);
int __cxa_thread_atexit_impl(void (*dtor)(void *), void *obj, void *dso)
{
    (void)dtor; (void)obj; (void)dso;
    return 0;
}

/* ---- emulated thread-local storage (the C++ runtime's exception state) ---------------- */

typedef struct {
    size_t size, align;
    union { uintptr_t index; void *address; } object;   /* here: pthread key + 1 */
    void *value;                                         /* initial contents, or NULL */
} emutls_control;

void *__emutls_get_address(emutls_control *control);

void *__emutls_get_address(emutls_control *control)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    uintptr_t index = __atomic_load_n(&control->object.index, __ATOMIC_ACQUIRE);
    if (!index) {
        pthread_mutex_lock(&lock);
        index = control->object.index;
        if (!index) {
            pthread_key_t key;
            if (pthread_key_create(&key, free) != 0)
                abort();
            index = (uintptr_t)key + 1;
            __atomic_store_n(&control->object.index, index, __ATOMIC_RELEASE);
        }
        pthread_mutex_unlock(&lock);
    }
    pthread_key_t key = (pthread_key_t)(index - 1);
    void *p = pthread_getspecific(key);
    if (!p) {
        size_t align = control->align < sizeof(void *) ? sizeof(void *) : control->align;
        if (posix_memalign(&p, align, control->size) != 0)
            abort();
        if (control->value)
            memcpy(p, control->value, control->size);
        else
            memset(p, 0, control->size);
        pthread_setspecific(key, p);
    }
    return p;
}
