# Memory Allocator in C

A **malloc/free implementation built from scratch** on a 1 MB static heap, featuring first-fit allocation, block splitting, forward coalescing, and a colour-coded visual heap dump.

Written in pure C with no external dependencies. Part of the Corg-Labs collection.

---

# Features
- `my_malloc`, `my_free`, `my_calloc`, and `my_realloc` built from scratch
- 1 MB static heap — no calls to `sbrk` or `mmap`
- 8-byte aligned allocations
- First-fit free list with block splitting to reduce fragmentation
- Forward coalescing: adjacent free blocks are merged on `my_free`
- `heap_dump()` — ANSI colour-coded visual block map
- Interactive REPL: alloc, free, realloc, calloc, dump, stats
- Up to 32 live pointer slots tracked in the demo

---

# Tutorial

## 1. The Block Header

Every allocation is prefixed by a `Block` header that lives immediately before the user payload. The header stores the usable size (excluding the header), a free flag, and a pointer to the next block. The free list is an intrusive singly-linked list through these headers.

```c
typedef struct Block {
    size_t       size;   /* usable bytes, not counting this header */
    int          free;   /* 1 = free, 0 = allocated */
    struct Block *next;  /* next block in the list */
} Block;

#define ALIGN     8
#define ALIGN_UP(n)  (((n) + (ALIGN-1)) & ~(size_t)(ALIGN-1))
#define HDR_SIZE  ALIGN_UP(sizeof(Block))
```

The heap is a plain static array. On first use, `heap_init` wraps the entire array in a single free block.

```c
static char  _heap[HEAP_SIZE];   /* 1 MB */
static Block *_free_list = NULL;

static void heap_init(void) {
    _free_list       = (Block *)_heap;
    _free_list->size = HEAP_SIZE - HDR_SIZE;
    _free_list->free = 1;
    _free_list->next = NULL;
}
```

## 2. First-Fit Allocation

`my_malloc` walks the free list and takes the first block that is marked free and large enough. The requested size is rounded up to the next multiple of 8 before searching.

```c
void *my_malloc(size_t size) {
    if (!_initialized) heap_init();
    if (size == 0) return NULL;
    size = ALIGN_UP(size);

    for (Block *b = _free_list; b != NULL; b = b->next) {
        if (b->free && b->size >= size) {
            split_block(b, size);   /* carve off only what is needed */
            b->free = 0;
            return (char *)b + HDR_SIZE;   /* pointer past the header */
        }
    }
    return NULL;   /* out of memory */
}
```

The returned pointer is cast past the header so the caller never sees the bookkeeping bytes. To recover the header from a user pointer, subtract `HDR_SIZE`:

```c
Block *b = (Block *)((char *)ptr - HDR_SIZE);
```

## 3. Block Splitting

When a free block is larger than needed, `split_block` carves it in two: the front block is exactly the requested size and marked allocated; the remainder becomes a new free block with its own header. Splitting is only performed if the leftover is large enough to hold a header plus at least one aligned payload byte.

```c
static void split_block(Block *b, size_t need) {
    size_t leftover = b->size - need;
    if (leftover <= HDR_SIZE + ALIGN)
        return;   /* too small to split */

    Block *nb  = (Block *)((char *)b + HDR_SIZE + need);
    nb->size   = leftover - HDR_SIZE;
    nb->free   = 1;
    nb->next   = b->next;

    b->size    = need;
    b->next    = nb;
}
```

Without splitting, a 4-byte `my_malloc` would consume the entire 1 MB heap.

## 4. Free and Forward Coalescing

`my_free` marks the block free, then calls `coalesce` to merge it with any immediately following free blocks. This prevents the heap from fragmenting into many tiny unusable pieces over time.

```c
void my_free(void *ptr) {
    if (!ptr) return;
    Block *b = (Block *)((char *)ptr - HDR_SIZE);
    b->free  = 1;
    coalesce(b);
}

static void coalesce(Block *b) {
    while (b->next && b->next->free) {
        /* Verify physical adjacency before merging */
        char *expected = (char *)b + HDR_SIZE + b->size;
        if ((char *)b->next != expected) break;
        b->size += HDR_SIZE + b->next->size;
        b->next  = b->next->next;
    }
}
```

The adjacency check (`expected == (char *)b->next`) is essential: blocks must be physically contiguous, not just consecutive in the linked list.

## 5. Realloc — Three Strategies

`my_realloc` handles three cases without always allocating+copying:

1. **Shrink in place** — if the new size is smaller, split the block and return the same pointer.
2. **Expand in place** — if the immediately following block is free and physically adjacent, absorb it.
3. **Allocate-copy-free** — fall back to a new allocation, `memcpy`, and free the old block.

```c
void *my_realloc(void *ptr, size_t new_size) {
    Block *b = (Block *)((char *)ptr - HDR_SIZE);
    new_size = ALIGN_UP(new_size);

    if (new_size <= b->size) {
        split_block(b, new_size);
        return ptr;
    }
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
    /* Fall back */
    void *np = my_malloc(new_size);
    memcpy(np, ptr, b->size < new_size ? b->size : new_size);
    my_free(ptr);
    return np;
}
```

## 6. The Heap Dump Visualiser

`heap_dump` walks the entire free list and prints each block as a colour-coded tag: green for free, red for allocated. Every 6 blocks it wraps to a new line. A summary line shows total used, free, and overhead bytes.

```c
void heap_dump(void) {
    for (Block *b = _free_list; b != NULL; b = b->next) {
        if (b->free)
            printf("%s[FREE:%6zu]%s ", GRN, b->size, RST);
        else
            printf("%s[USED:%6zu]%s ", RED, b->size, RST);
        if (++blocks % 6 == 0) printf("\n");
    }
    printf("\n--- blocks: %zu  |  used: %zu B  |  free: %zu B ---\n",
           blocks, total_used, total_free);
}
```

## 7. Interactive Demo

The `main` function provides a REPL over `my_malloc`/`my_free`/`my_realloc`/`my_calloc`, keeping live pointers in a 32-slot array indexed by slot number.

```
heap> alloc 1024
Allocated 1024 bytes → slot[0] @ 0x104c4b030
heap> alloc 512
Allocated 512 bytes → slot[1] @ 0x104c4b440
heap> dump
[USED:  1024] [USED:   512] [FREE:1046520]
--- blocks: 3  |  used: 1536 B  |  free: 1046520 B ---
heap> free 0
Freeing slot[0] @ 0x104c4b030
heap> dump
[FREE:  1024] [USED:   512] [FREE:1046520]
```

---

# Build
```
gcc mymalloc.c -o mymalloc
```

# Run
```
./mymalloc
```

# Controls

| Command | Action |
|---|---|
| `alloc <bytes>` | Allocate N bytes, store in next free slot |
| `free <slot>` | Free the pointer in slot N |
| `realloc <slot> <bytes>` | Resize the allocation in slot N |
| `calloc <n> <size>` | Zero-initialised allocation for N elements |
| `dump` | Print colour-coded heap block map |
| `stats` | Print live allocation count |
| `quit` / `q` | Exit |

---

# Concepts Practiced
- Memory block header design (size, free flag, next pointer)
- Pointer arithmetic to navigate between header and payload
- First-fit free list traversal
- Block splitting to minimise internal fragmentation
- Forward coalescing to combat external fragmentation
- Physical adjacency verification before coalescing
- `my_realloc` in-place expansion by absorbing adjacent free block
- 8-byte alignment via bitwise round-up macro

---

# Dependencies
Standard C libraries only: `stdio.h`, `string.h`, `stddef.h`, `stdint.h`
