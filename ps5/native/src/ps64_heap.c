/*
 * ps64_heap - the app's own malloc. A native PS5 app's C library heap holds
 * only about 5 MB, while mmap gives hundreds. So malloc and friends (selected
 * with APP_WRAP_SYMBOLS) run dlmalloc inside one large mmap'd block.
 *
 * Memory the C library allocates itself (strdup, stdio buffers, ...) still
 * comes from its own heap; free() and realloc() recognise those pointers by
 * address and hand them back to it.
 */
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

static char *heap_base;
static size_t heap_size, heap_top;

static void *ps64_morecore(intptr_t increment);

#define USE_DL_PREFIX 1
#define HAVE_MMAP 0
#define HAVE_MREMAP 0
#define MORECORE ps64_morecore
#define MORECORE_CONTIGUOUS 1
#define MORECORE_CANNOT_TRIM 1
#define USE_LOCKS 1
/* The PS5's C++ ABI has __STDCPP_DEFAULT_NEW_ALIGNMENT__ 32 (AVX2): the compiler
 * stores into new'd objects with 32-byte aligned AVX moves (vmovaps ymm), so every
 * block must be 32-aligned. dlmalloc's default 16 crashed at random (general
 * protection fault in libsidplayfp's c64::addExtraSid). */
#define MALLOC_ALIGNMENT ((size_t)32U)
#define NO_MALLOC_STATS 1
#define malloc_getpagesize ((size_t)16384U)
#define DLMALLOC_EXPORT static
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-null-pointer-arithmetic"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wunused-function"
#include "dlmalloc.c"
#pragma clang diagnostic pop

/*
 * Reserve the block on first use. A native app's flexible memory budget is
 * about 450 MB (384 MB mapped, 512 MB didn't), and thread stacks and other
 * mappings come from it too, so take 256 MB if possible: plenty for PS64
 * with the emulator.
 */
static void *ps64_morecore(intptr_t increment)
{
    if (!heap_base) {
        static const size_t sizes[] = { 256u << 20, 192u << 20, 128u << 20, 96u << 20 };
        for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]) && !heap_base; i++) {
            void *p = mmap(NULL, sizes[i], PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
            if (p != MAP_FAILED) {
                heap_base = p;
                heap_size = sizes[i];
            }
        }
        if (!heap_base)
            return (void *)MFAIL;
    }
    if (increment < 0 || heap_top + (size_t)increment > heap_size)
        return (void *)MFAIL;
    void *at = heap_base + heap_top;
    heap_top += (size_t)increment;
    return at;
}

static int ours(const void *p)
{
    return heap_base && (const char *)p >= heap_base && (const char *)p < heap_base + heap_size;
}

size_t ps64_heap_reserved(void) { return heap_size; }
size_t ps64_heap_used(void)     { return heap_top; }

void  __real_free(void *p);
void *__real_realloc(void *p, size_t n);

void *__wrap_malloc(size_t n);
void  __wrap_free(void *p);
void *__wrap_calloc(size_t count, size_t n);
void *__wrap_realloc(void *p, size_t n);
int   __wrap_posix_memalign(void **out, size_t alignment, size_t n);
void *__wrap_memalign(size_t alignment, size_t n);
void *__wrap_aligned_alloc(size_t alignment, size_t n);

void *__wrap_malloc(size_t n) { return dlmalloc(n); }

void __wrap_free(void *p)
{
    if (!p)
        return;
    if (ours(p))
        dlfree(p);
    else
        __real_free(p);
}

void *__wrap_calloc(size_t count, size_t n) { return dlcalloc(count, n); }

void *__wrap_realloc(void *p, size_t n)
{
    if (p && !ours(p))
        return __real_realloc(p, n);        /* stays in the C library's heap */
    return dlrealloc(p, n);
}

int __wrap_posix_memalign(void **out, size_t alignment, size_t n)
{
    return dlposix_memalign(out, alignment, n);
}

void *__wrap_memalign(size_t alignment, size_t n) { return dlmemalign(alignment, n); }
void *__wrap_aligned_alloc(size_t alignment, size_t n) { return dlmemalign(alignment, n); }
