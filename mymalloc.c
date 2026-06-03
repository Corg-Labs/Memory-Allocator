/*
 * mymalloc.c — Custom memory allocator built on a static 1MB heap.
 *
 * Features:
 *   - First-fit free list with block headers (size, free flag, next pointer)
 *   - Coalescing: adjacent free blocks are merged on my_free()
 *   - heap_dump(): visual map with ANSI color coding
 *   - Interactive demo in main()
 *
 * Compile: gcc mymalloc.c -o mymalloc
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Configuration
 * ---------------------------------------------------------------------- */

#define HEAP_SIZE    (1024 * 1024)   /* 1 MB static heap                  */
#define ALIGN        8               /* alignment in bytes                 */
#define ALIGN_UP(n)  (((n) + (ALIGN-1)) & ~(size_t)(ALIGN-1))

/* ANSI colour helpers */
#define RED   "\033[1;31m"
#define GRN   "\033[1;32m"
#define YEL   "\033[1;33m"
#define CYN   "\033[1;36m"
#define RST   "\033[0m"

/* -------------------------------------------------------------------------
 * Block header — lives immediately before the user payload.
 * ---------------------------------------------------------------------- */

typedef struct Block {
    size_t       size;   /* usable bytes (excludes this header)           */
    int          free;   /* 1 = free, 0 = allocated                       */
    struct Block *next;  /* next block in the intrusive list               */
} Block;

#define HDR_SIZE  ALIGN_UP(sizeof(Block))

/* -------------------------------------------------------------------------
 * Heap storage
 * ---------------------------------------------------------------------- */

static char  _heap[HEAP_SIZE];
static Block *_free_list = NULL;   /* head of block list                  */
static int    _initialized = 0;

/* -------------------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------------- */

void my_free(void *ptr);

/* -------------------------------------------------------------------------
 * Internals
 * ---------------------------------------------------------------------- */

static void heap_init(void)
{
    _free_list       = (Block *)_heap;
    _free_list->size = HEAP_SIZE - HDR_SIZE;
    _free_list->free = 1;
    _free_list->next = NULL;
    _initialized     = 1;
}

/* Split block b so that b holds `need` bytes; the remainder becomes a new
 * free block — only if the leftover is large enough to hold a header plus
 * at least ALIGN bytes of payload. */
static void split_block(Block *b, size_t need)
{
    size_t leftover = b->size - need;
    if (leftover <= HDR_SIZE + ALIGN)
        return;   /* not worth splitting */

    Block *nb  = (Block *)((char *)b + HDR_SIZE + need);
    nb->size   = leftover - HDR_SIZE;
    nb->free   = 1;
    nb->next   = b->next;

    b->size    = need;
    b->next    = nb;
}

/* Merge b with b->next if the neighbour is free and physically adjacent. */
static void coalesce(Block *b)
{
    while (b->next && b->next->free) {
        /* Verify physical adjacency */
        char *expected = (char *)b + HDR_SIZE + b->size;
        if ((char *)b->next != expected)
            break;
        b->size += HDR_SIZE + b->next->size;
        b->next  = b->next->next;
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void *my_malloc(size_t size)
{
    if (!_initialized) heap_init();
    if (size == 0) return NULL;

    size = ALIGN_UP(size);

    /* First-fit search */
    for (Block *b = _free_list; b != NULL; b = b->next) {
        if (b->free && b->size >= size) {
            split_block(b, size);
            b->free = 0;
            return (char *)b + HDR_SIZE;
        }
    }
    return NULL;   /* OOM */
}

void *my_calloc(size_t nmemb, size_t esz)
{
    size_t total = nmemb * esz;
    void  *ptr   = my_malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *my_realloc(void *ptr, size_t new_size)
{
    if (!ptr)          return my_malloc(new_size);
    if (new_size == 0) { my_free(ptr); return NULL; }

    Block *b    = (Block *)((char *)ptr - HDR_SIZE);
    new_size    = ALIGN_UP(new_size);

    if (new_size <= b->size) {
        /* Shrink in place */
        split_block(b, new_size);
        return ptr;
    }

    /* Grow: try to absorb the immediately following free block */
    if (b->next && b->next->free) {
        char *expected = (char *)b + HDR_SIZE + b->size;
        if ((char *)b->next == expected) {
            size_t combined = b->size + HDR_SIZE + b->next->size;
            if (combined >= new_size) {
                b->size = combined;
                b->next = b->next->next;
                split_block(b, new_size);
                return ptr;
            }
        }
    }

    /* Fall back to allocate-copy-free */
    void *np = my_malloc(new_size);
    if (!np) return NULL;
    memcpy(np, ptr, b->size < new_size ? b->size : new_size);
    my_free(ptr);
    return np;
}

void my_free(void *ptr)
{
    if (!ptr) return;
    Block *b = (Block *)((char *)ptr - HDR_SIZE);
    b->free  = 1;

    /* Coalesce forward (we use a singly-linked ordered list) */
    coalesce(b);
}

/* -------------------------------------------------------------------------
 * Diagnostics
 * ---------------------------------------------------------------------- */

void heap_dump(void)
{
    if (!_initialized) heap_init();

    printf("\n%s=== HEAP DUMP ===%s\n", YEL, RST);
    size_t total_used = 0, total_free = 0, blocks = 0;

    for (Block *b = _free_list; b != NULL; b = b->next) {
        ++blocks;
        if (b->free) {
            printf("%s[FREE:%6zu]%s ", GRN, b->size, RST);
            total_free += b->size;
        } else {
            printf("%s[USED:%6zu]%s ", RED, b->size, RST);
            total_used += b->size;
        }
        /* Line-wrap every 6 blocks for readability */
        if (blocks % 6 == 0) printf("\n");
    }
    printf("\n");
    printf("%s--- blocks: %zu  |  used: %zu B  |  free: %zu B  |  overhead: %zu B ---%s\n\n",
           CYN, blocks, total_used, total_free, blocks * HDR_SIZE, RST);
}

/* How many live allocations are there? */
static size_t live_alloc_count(void)
{
    size_t n = 0;
    for (Block *b = _free_list; b; b = b->next)
        if (!b->free) ++n;
    return n;
}

/* -------------------------------------------------------------------------
 * Interactive demo
 * ---------------------------------------------------------------------- */

#define MAX_PTRS 32

int main(void)
{
    printf("%smymalloc — custom allocator demo%s\n", CYN, RST);
    printf("Commands: alloc <bytes> | free <slot> | realloc <slot> <bytes>\n");
    printf("          calloc <n> <size> | dump | stats | quit\n\n");

    void  *ptrs[MAX_PTRS] = {0};
    char   line[256];

    heap_init();

    while (1) {
        printf(YEL "heap> " RST);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        char cmd[32];
        if (sscanf(line, "%31s", cmd) != 1) continue;

        if (!strcmp(cmd, "quit") || !strcmp(cmd, "q")) {
            printf("Bye.\n");
            break;

        } else if (!strcmp(cmd, "alloc")) {
            size_t sz = 0;
            sscanf(line, "%*s %zu", &sz);
            if (sz == 0) { printf("Usage: alloc <bytes>\n"); continue; }

            /* Find free slot */
            int slot = -1;
            for (int i = 0; i < MAX_PTRS; i++)
                if (!ptrs[i]) { slot = i; break; }
            if (slot < 0) { printf("No free pointer slots.\n"); continue; }

            ptrs[slot] = my_malloc(sz);
            if (ptrs[slot]) {
                /* Write a recognisable pattern so the memory is "used" */
                memset(ptrs[slot], 0xAB, sz);
                printf(GRN "Allocated %zu bytes → slot[%d] @ %p\n" RST,
                       sz, slot, ptrs[slot]);
            } else {
                printf(RED "Allocation of %zu bytes FAILED (out of memory)\n" RST, sz);
            }

        } else if (!strcmp(cmd, "free")) {
            int slot = -1;
            sscanf(line, "%*s %d", &slot);
            if (slot < 0 || slot >= MAX_PTRS || !ptrs[slot]) {
                printf("Invalid or empty slot.\n"); continue;
            }
            printf(RED "Freeing slot[%d] @ %p\n" RST, slot, ptrs[slot]);
            my_free(ptrs[slot]);
            ptrs[slot] = NULL;

        } else if (!strcmp(cmd, "realloc")) {
            int slot = -1; size_t sz = 0;
            sscanf(line, "%*s %d %zu", &slot, &sz);
            if (slot < 0 || slot >= MAX_PTRS || !ptrs[slot] || sz == 0) {
                printf("Usage: realloc <slot> <bytes>\n"); continue;
            }
            void *np = my_realloc(ptrs[slot], sz);
            if (np) {
                ptrs[slot] = np;
                printf(GRN "Realloc'd slot[%d] to %zu bytes → %p\n" RST, slot, sz, np);
            } else {
                printf(RED "Realloc failed.\n" RST);
            }

        } else if (!strcmp(cmd, "calloc")) {
            size_t n = 0, esz = 0;
            sscanf(line, "%*s %zu %zu", &n, &esz);
            if (!n || !esz) { printf("Usage: calloc <n> <elemsize>\n"); continue; }

            int slot = -1;
            for (int i = 0; i < MAX_PTRS; i++)
                if (!ptrs[i]) { slot = i; break; }
            if (slot < 0) { printf("No free pointer slots.\n"); continue; }

            ptrs[slot] = my_calloc(n, esz);
            if (ptrs[slot])
                printf(GRN "calloc(%zu, %zu) → slot[%d] @ %p (zeroed)\n" RST,
                       n, esz, slot, ptrs[slot]);
            else
                printf(RED "calloc failed.\n" RST);

        } else if (!strcmp(cmd, "dump")) {
            heap_dump();

        } else if (!strcmp(cmd, "stats")) {
            printf("%sLive allocations: %zu%s\n", CYN, live_alloc_count(), RST);

        } else {
            printf("Unknown command: %s\n", cmd);
        }
    }

    return 0;
}
