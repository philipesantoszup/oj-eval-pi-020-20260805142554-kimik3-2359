#include "buddy.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAXRANK 16
#define PGSIZE 4096L

static uintptr_t base_addr = 0; /* start of managed pool */
static long npg = 0;            /* number of managed 4K pages */

static int8_t *pg_rank = NULL;    /* per page: rank of the block containing it */
static uint8_t *head_free = NULL; /* page is head of a free block */
static uint8_t *head_alloc = NULL;/* page is head of an allocated block */
static long *fl_next = NULL;      /* free-list links (valid for free heads) */
static long *fl_prev = NULL;
static long free_head[MAXRANK + 1]; /* first free block of each rank (-1 empty) */
static long free_cnt[MAXRANK + 1];  /* number of free blocks of each rank */

static long blk_size(int r) { return 1L << (r - 1); }

static void set_rank(long i, int r) { memset(pg_rank + i, r, (size_t)blk_size(r)); }

static void push_free(long i, int r)
{
    fl_prev[i] = -1;
    fl_next[i] = free_head[r];
    if (free_head[r] >= 0)
        fl_prev[free_head[r]] = i;
    free_head[r] = i;
    free_cnt[r]++;
    head_free[i] = 1;
}

static void remove_free(long i, int r)
{
    if (fl_prev[i] >= 0)
        fl_next[fl_prev[i]] = fl_next[i];
    else
        free_head[r] = fl_next[i];
    if (fl_next[i] >= 0)
        fl_prev[fl_next[i]] = fl_prev[i];
    free_cnt[r]--;
    head_free[i] = 0;
}

int init_page(void *p, int pgcount)
{
    int r;
    long i;

    free(pg_rank);   pg_rank = NULL;
    free(head_free); head_free = NULL;
    free(head_alloc);head_alloc = NULL;
    free(fl_next);   fl_next = NULL;
    free(fl_prev);   fl_prev = NULL;
    base_addr = 0;
    npg = 0;
    for (r = 1; r <= MAXRANK; r++) {
        free_head[r] = -1;
        free_cnt[r] = 0;
    }
    if (p == NULL || pgcount <= 0)
        return OK;

    base_addr = (uintptr_t)p;
    npg = pgcount;
    pg_rank   = (int8_t *)malloc((size_t)npg);
    head_free = (uint8_t *)calloc((size_t)npg, 1);
    head_alloc= (uint8_t *)calloc((size_t)npg, 1);
    fl_next   = (long *)malloc((size_t)npg * sizeof(long));
    fl_prev   = (long *)malloc((size_t)npg * sizeof(long));
    if (!pg_rank || !head_free || !head_alloc || !fl_next || !fl_prev)
        return -ENOSPC;

    /* decompose the pool into maximal aligned power-of-2 blocks */
    i = 0;
    while (i < npg) {
        r = MAXRANK;
        while (r > 1 && ((i & (blk_size(r) - 1)) != 0 || i + blk_size(r) > npg))
            r--;
        set_rank(i, r);
        push_free(i, r);
        i += blk_size(r);
    }
    return OK;
}

void *alloc_pages(int rank)
{
    int r;
    long i;

    if (rank < 1 || rank > MAXRANK)
        return ERR_PTR(-EINVAL);
    r = rank;
    while (r <= MAXRANK && free_head[r] < 0)
        r++;
    if (r > MAXRANK)
        return ERR_PTR(-ENOSPC);

    i = free_head[r];
    remove_free(i, r);
    /* split down, leaving the right halves in the free lists */
    while (r > rank) {
        long half;
        r--;
        half = i + blk_size(r);
        set_rank(half, r);
        push_free(half, r);
    }
    set_rank(i, rank);
    head_alloc[i] = 1;
    return (void *)(base_addr + (uintptr_t)i * (uintptr_t)PGSIZE);
}

int return_pages(void *p)
{
    uintptr_t a;
    long i;
    int r;

    if (p == NULL || base_addr == 0)
        return -EINVAL;
    a = (uintptr_t)p;
    if (a < base_addr || ((a - base_addr) & (PGSIZE - 1)) != 0)
        return -EINVAL;
    i = (long)((a - base_addr) / PGSIZE);
    if (i >= npg)
        return -EINVAL;
    if (!head_alloc[i])
        return -EINVAL; /* not the start of an allocated block */

    r = pg_rank[i];
    head_alloc[i] = 0;
    /* merge with the buddy while possible */
    while (r < MAXRANK) {
        long sz = blk_size(r);
        long buddy = i ^ sz;
        if (buddy >= npg)
            break;
        if (!head_free[buddy] || pg_rank[buddy] != r)
            break;
        remove_free(buddy, r);
        if (buddy < i)
            i = buddy;
        r++;
    }
    set_rank(i, r);
    push_free(i, r);
    return OK;
}

int query_ranks(void *p)
{
    uintptr_t a;
    long i;

    if (p == NULL || base_addr == 0)
        return -EINVAL;
    a = (uintptr_t)p;
    if (a < base_addr || ((a - base_addr) & (PGSIZE - 1)) != 0)
        return -EINVAL;
    i = (long)((a - base_addr) / PGSIZE);
    if (i >= npg)
        return -EINVAL;
    return pg_rank[i];
}

int query_page_counts(int rank)
{
    if (rank < 1 || rank > MAXRANK)
        return -EINVAL;
    return (int)free_cnt[rank];
}
