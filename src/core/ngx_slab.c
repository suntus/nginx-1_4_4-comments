
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <ngx_config.h>
#include <ngx_core.h>

#define NGX_SLAB_PAGE_MASK 3
#define NGX_SLAB_PAGE 0
#define NGX_SLAB_BIG 1
#define NGX_SLAB_EXACT 2
#define NGX_SLAB_SMALL 3

#if (NGX_PTR_SIZE == 4)

#define NGX_SLAB_PAGE_FREE 0
#define NGX_SLAB_PAGE_BUSY 0xffffffff
#define NGX_SLAB_PAGE_START 0x80000000

#define NGX_SLAB_SHIFT_MASK 0x0000000f
#define NGX_SLAB_MAP_MASK 0xffff0000
#define NGX_SLAB_MAP_SHIFT 16

#define NGX_SLAB_BUSY 0xffffffff

#else /* (NGX_PTR_SIZE == 8) */

#define NGX_SLAB_PAGE_FREE 0
#define NGX_SLAB_PAGE_BUSY 0xffffffffffffffff
#define NGX_SLAB_PAGE_START 0x8000000000000000

#define NGX_SLAB_SHIFT_MASK 0x000000000000000f
#define NGX_SLAB_MAP_MASK 0xffffffff00000000
#define NGX_SLAB_MAP_SHIFT 32

#define NGX_SLAB_BUSY 0xffffffffffffffff

#endif

#if (NGX_DEBUG_MALLOC)

#define ngx_slab_junk(p, size) ngx_memset(p, 0xA5, size)

#elif (NGX_HAVE_DEBUG_MALLOC)

#define ngx_slab_junk(p, size) \
    if (ngx_debug_malloc) ngx_memset(p, 0xA5, size)

#else

#define ngx_slab_junk(p, size)

#endif

static ngx_slab_page_t *ngx_slab_alloc_pages(ngx_slab_pool_t *pool,
                                             ngx_uint_t pages);
static void ngx_slab_free_pages(ngx_slab_pool_t *pool, ngx_slab_page_t *page,
                                ngx_uint_t pages);
static void ngx_slab_error(ngx_slab_pool_t *pool, ngx_uint_t level, char *text);

static ngx_uint_t ngx_slab_max_size;    // 2048，slab和page的分割点，大于等于该值需要从page中分配

// 32位环境下是128。
// 一个uintptr_t位图最多能表示8*sizeof(uintptr_t)=32个块，
// 如果用一个uintptr_t去管理一页，那每一页最多就是8*sizeof(uintptr_t)=32个块，
// 那每一块大小就是 4k/(8*sizeof(uintptr_t))=128B
static ngx_uint_t ngx_slab_exact_size;
static ngx_uint_t ngx_slab_exact_shift; // 32位下是7

//
//
//
/**
 * @brief 初始化slab池
 *  共享内存排布：
 *  ngx_slab_pool_t
 *  --------
 *  ngx_slab_page_t[0]
 *  ...
 *  ngx_slab_page_t[n-1], n = 9
 *  --------
 *  ngx_slab_page_t[0]/pool->pages
 *  ...
 *  ngx_slab_page_t[pages-1]
 *  --------
 *  pool->start
 *
 * @param pool
 */
void ngx_slab_init(ngx_slab_pool_t *pool) {
    u_char *p;
    size_t size;
    ngx_int_t m;
    ngx_uint_t i, n, pages;
    ngx_slab_page_t *slots;

    /* STUB */
    if (ngx_slab_max_size == 0) {
        ngx_slab_max_size = ngx_pagesize / 2;   // ngx_pagesize = 4K
        ngx_slab_exact_size = ngx_pagesize / (8 * sizeof(uintptr_t));
        for (n = ngx_slab_exact_size; n >>= 1; ngx_slab_exact_shift++) {
            /* void */
        }
    }
    /**/

    pool->min_size = 1 << pool->min_shift;

    p = (u_char *)pool + sizeof(ngx_slab_pool_t);
    size = pool->end - p;   // slab管理的总共的内存大小

    ngx_slab_junk(p, size);

    slots = (ngx_slab_page_t *)p;  // slots紧跟着slab管理结构体之后
    n = ngx_pagesize_shift - pool->min_shift;

    // 每个slot都是同样大小的slab块，共9个槽位，依次管理的块大小为：
    // 2^3 = 8
    // 2^4 = 16
    // 2^5 = 32
    // 2^6 = 64
    // 2^7 = 128
    // 2^8 = 256
    // 2^9 = 512
    // 2^10 = 1024
    // 2^11 = 2048
    for (i = 0; i < n; i++) {
        slots[i].slab = 0;
        slots[i].next = &slots[i];  // 是个判断标志，表示该槽是否已经申请了page
        slots[i].prev = 0;
    }

    p += n * sizeof(ngx_slab_page_t);

    // 可以划分多少个slab_page去管理所有内存
    pages = (ngx_uint_t)(size / (ngx_pagesize + sizeof(ngx_slab_page_t)));

    ngx_memzero(p, pages * sizeof(ngx_slab_page_t));

    pool->pages = (ngx_slab_page_t *)p;

    pool->free.prev = 0;
    pool->free.next = (ngx_slab_page_t *)p;

    pool->pages->slab = pages;
    pool->pages->next = &pool->free;
    pool->pages->prev = (uintptr_t)&pool->free;

    // 跳过slab_page管理结构，对齐实际可用页面的位置，按页对齐
    pool->start = (u_char *)ngx_align_ptr(
        (uintptr_t)p + pages * sizeof(ngx_slab_page_t), ngx_pagesize);

    // 对齐操作可能导致实际可用页面数减少，所以需要调整
    m = pages - (pool->end - pool->start) / ngx_pagesize;
    if (m > 0) {
        pages -= m;
        pool->pages->slab = pages;  // 总共管理多少页面
    }

    pool->log_ctx = &pool->zero;
    pool->zero = '\0';
}

void *ngx_slab_alloc(ngx_slab_pool_t *pool, size_t size) {
    void *p;

    ngx_shmtx_lock(&pool->mutex);

    p = ngx_slab_alloc_locked(pool, size);

    ngx_shmtx_unlock(&pool->mutex);

    return p;
}

void *ngx_slab_alloc_locked(ngx_slab_pool_t *pool, size_t size) {
    size_t s;
    uintptr_t p, n, m, mask, *bitmap;
    ngx_uint_t i, slot, shift, map;
    ngx_slab_page_t *page, *prev, *slots;

    // 如果申请的大小大于最大大小，也就是页的一半大小，就直接申请一页
    if (size >= ngx_slab_max_size) {
        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                       "slab alloc: %uz", size);

        // 取出来的是ngx_slab_page_t管理节点
        page = ngx_slab_alloc_pages(pool, (size >> ngx_pagesize_shift) +
                                              ((size % ngx_pagesize) ? 1 : 0));
        // 根据管理节点获取实际的内存地址
        if (page) {
            p = (page - pool->pages) << ngx_pagesize_shift;
            p += (uintptr_t)pool->start;

        } else {
            p = 0;
        }

        goto done;
    }

    // 下面就使用slot去管理了

    if (size > pool->min_size) {
        shift = 1;
        for (s = size - 1; s >>= 1; shift++) { /* void */
        }
        slot = shift - pool->min_shift; // 计算处于第几个槽中

    } else {
        // 可分配的最少的内存
        size = pool->min_size;
        shift = pool->min_shift;
        slot = 0;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                   "slab alloc: %uz slot: %ui", size, slot);

    // slots数组
    slots = (ngx_slab_page_t *)((u_char *)pool + sizeof(ngx_slab_pool_t));
    page = slots[slot].next; // 这个slot的管理节点

    if (page->next != page) { // 之前已经申请过page了，要检查这个页面是否有足够的空闲内存
        if (shift < ngx_slab_exact_shift) { // 申请的内存比较小，需要多个uintptr_t才能管理一页
            do {
                p = (page - pool->pages) << ngx_pagesize_shift;
                bitmap = (uintptr_t *)(pool->start + p); // 一个页的最开始

                // 需要几个 uintptr_t bitmap来标记该页的所有内存slab
                map = (1 << (ngx_pagesize_shift - shift)) /
                      (sizeof(uintptr_t) * 8);

                for (n = 0; n < map; n++) {
                    if (bitmap[n] != NGX_SLAB_BUSY) {
                        // 有空余的内存，从头开始检查每个bitmap位，找到第一个空闲的
                        // m表示实际的bit位，i表示m在第几位上
                        for (m = 1, i = 0; m; m <<= 1, i++) {
                            if ((bitmap[n] & m)) {
                                continue;
                            }

                            bitmap[n] |= m; // 将该空闲位标记已被占用

                            // 需要查找对应的实际内存块了
                            // 对应的实际内存块距离开头有多远，一个uintptr_t的bitmap可以
                            // 表示sizeof(uintptr_t)*8个内存块，每个内存块的大小为都一样，都是 1<<shift
                            // 所以第n个uintptr_t中的第i位表示的内存块距离内存开头就是这个：
                            i = ((n * sizeof(uintptr_t) * 8) << shift) +
                                (i << shift);

                            // 检查本页是否还有空余，要是没有空余了，
                            // 就需要再弄个新的页面了
                            if (bitmap[n] == NGX_SLAB_BUSY) {
                                for (n = n + 1; n < map; n++) {
                                    if (bitmap[n] != NGX_SLAB_BUSY) {
                                        p = (uintptr_t)bitmap + i;

                                        goto done;
                                    }
                                }

                                prev = (ngx_slab_page_t *)(page->prev &
                                                           ~NGX_SLAB_PAGE_MASK);
                                prev->next = page->next;
                                page->next->prev = page->prev;

                                page->next = NULL;
                                page->prev = NGX_SLAB_SMALL;
                            }

                            p = (uintptr_t)bitmap + i;

                            goto done;
                        }
                    }
                }

                page = page->next;

            } while (page);

        } else if (shift == ngx_slab_exact_shift) { // 申请的内存大小刚好够用一个uinptr_t管理整个页面
            do {
                // 就直接用管理节点中的slab去表示了，不需要在实际页中再申请其他的bitmap了
                if (page->slab != NGX_SLAB_BUSY) {
                    for (m = 1, i = 0; m; m <<= 1, i++) {
                        if ((page->slab & m)) {
                            continue;
                        }

                        page->slab |= m;

                        if (page->slab == NGX_SLAB_BUSY) {
                            prev = (ngx_slab_page_t *)(page->prev &
                                                       ~NGX_SLAB_PAGE_MASK);
                            prev->next = page->next;
                            page->next->prev = page->prev;

                            page->next = NULL;
                            page->prev = NGX_SLAB_EXACT;
                        }

                        p = (page - pool->pages) << ngx_pagesize_shift;
                        p += i << shift;
                        p += (uintptr_t)pool->start;

                        goto done;
                    }
                }

                page = page->next;

            } while (page);

        } else { /* shift > ngx_slab_exact_shift */
            // 申请的内存比较大，一个uinptr_t管理一个页面绰绰有余，管理的内存块增大一倍，
            // 需要的uintptr_t的位就少一半，这时候用前半段存储位图
            n = ngx_pagesize_shift - (page->slab & NGX_SLAB_SHIFT_MASK);
            n = 1 << n;
            n = ((uintptr_t)1 << n) - 1;
            mask = n << NGX_SLAB_MAP_SHIFT;

            do {
                if ((page->slab & NGX_SLAB_MAP_MASK) != mask) {
                    for (m = (uintptr_t)1 << NGX_SLAB_MAP_SHIFT, i = 0;
                         m & mask; m <<= 1, i++) {
                        if ((page->slab & m)) {
                            continue;
                        }

                        page->slab |= m;

                        if ((page->slab & NGX_SLAB_MAP_MASK) == mask) {
                            prev = (ngx_slab_page_t *)(page->prev &
                                                       ~NGX_SLAB_PAGE_MASK);
                            prev->next = page->next;
                            page->next->prev = page->prev;

                            page->next = NULL;
                            page->prev = NGX_SLAB_BIG;
                        }

                        p = (page - pool->pages) << ngx_pagesize_shift;
                        p += i << shift;
                        p += (uintptr_t)pool->start;

                        goto done;
                    }
                }

                page = page->next;

            } while (page);
        }
    }

    page = ngx_slab_alloc_pages(pool, 1);

    if (page) {
        if (shift < ngx_slab_exact_shift) {
            p = (page - pool->pages) << ngx_pagesize_shift;
            bitmap = (uintptr_t *)(pool->start + p);

            s = 1 << shift;
            n = (1 << (ngx_pagesize_shift - shift)) / 8 / s;

            if (n == 0) {
                n = 1;
            }

            bitmap[0] = (2 << n) - 1;

            map = (1 << (ngx_pagesize_shift - shift)) / (sizeof(uintptr_t) * 8);

            for (i = 1; i < map; i++) {
                bitmap[i] = 0;
            }

            page->slab = shift;
            page->next = &slots[slot];
            page->prev = (uintptr_t)&slots[slot] | NGX_SLAB_SMALL;

            slots[slot].next = page;

            p = ((page - pool->pages) << ngx_pagesize_shift) + s * n;
            p += (uintptr_t)pool->start;

            goto done;

        } else if (shift == ngx_slab_exact_shift) {
            page->slab = 1; // 只申请了其中1块内存
            page->next = &slots[slot];
            page->prev = (uintptr_t)&slots[slot] | NGX_SLAB_EXACT;

            slots[slot].next = page;

            p = (page - pool->pages) << ngx_pagesize_shift;
            p += (uintptr_t)pool->start;

            goto done;

        } else { /* shift > ngx_slab_exact_shift */
            // 前半边bit用作bitmap，后半段用于存储该块的大小
            page->slab = ((uintptr_t)1 << NGX_SLAB_MAP_SHIFT) | shift;
            page->next = &slots[slot];
            page->prev = (uintptr_t)&slots[slot] | NGX_SLAB_BIG;

            slots[slot].next = page;

            p = (page - pool->pages) << ngx_pagesize_shift;
            p += (uintptr_t)pool->start;

            goto done;
        }
    }

    p = 0;

done:

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0, "slab alloc: %p", p);

    return (void *)p;
}

void ngx_slab_free(ngx_slab_pool_t *pool, void *p) {
    ngx_shmtx_lock(&pool->mutex);

    ngx_slab_free_locked(pool, p);

    ngx_shmtx_unlock(&pool->mutex);
}

void ngx_slab_free_locked(ngx_slab_pool_t *pool, void *p) {
    size_t size;
    uintptr_t slab, m, *bitmap;
    ngx_uint_t n, type, slot, shift, map;
    ngx_slab_page_t *slots, *page;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0, "slab free: %p", p);

    if ((u_char *)p < pool->start || (u_char *)p > pool->end) {
        ngx_slab_error(pool, NGX_LOG_ALERT, "ngx_slab_free(): outside of pool");
        goto fail;
    }

    n = ((u_char *)p - pool->start) >> ngx_pagesize_shift;
    page = &pool->pages[n];
    slab = page->slab;
    type = page->prev & NGX_SLAB_PAGE_MASK;

    switch (type) {
        case NGX_SLAB_SMALL:

            shift = slab & NGX_SLAB_SHIFT_MASK;
            size = 1 << shift;

            if ((uintptr_t)p & (size - 1)) {
                goto wrong_chunk;
            }

            n = ((uintptr_t)p & (ngx_pagesize - 1)) >> shift;
            m = (uintptr_t)1 << (n & (sizeof(uintptr_t) * 8 - 1));
            n /= (sizeof(uintptr_t) * 8);
            bitmap = (uintptr_t *)((uintptr_t)p & ~(ngx_pagesize - 1));

            if (bitmap[n] & m) {
                if (page->next == NULL) {
                    slots = (ngx_slab_page_t *)((u_char *)pool +
                                                sizeof(ngx_slab_pool_t));
                    slot = shift - pool->min_shift;

                    page->next = slots[slot].next;
                    slots[slot].next = page;

                    page->prev = (uintptr_t)&slots[slot] | NGX_SLAB_SMALL;
                    page->next->prev = (uintptr_t)page | NGX_SLAB_SMALL;
                }

                bitmap[n] &= ~m;

                n = (1 << (ngx_pagesize_shift - shift)) / 8 / (1 << shift);

                if (n == 0) {
                    n = 1;
                }

                if (bitmap[0] & ~(((uintptr_t)1 << n) - 1)) {
                    goto done;
                }

                map = (1 << (ngx_pagesize_shift - shift)) /
                      (sizeof(uintptr_t) * 8);

                for (n = 1; n < map; n++) {
                    if (bitmap[n]) {
                        goto done;
                    }
                }

                ngx_slab_free_pages(pool, page, 1);

                goto done;
            }

            goto chunk_already_free;

        case NGX_SLAB_EXACT:

            m = (uintptr_t)1 << (((uintptr_t)p & (ngx_pagesize - 1)) >>
                                 ngx_slab_exact_shift);
            size = ngx_slab_exact_size;

            if ((uintptr_t)p & (size - 1)) {
                goto wrong_chunk;
            }

            if (slab & m) {
                if (slab == NGX_SLAB_BUSY) {
                    slots = (ngx_slab_page_t *)((u_char *)pool +
                                                sizeof(ngx_slab_pool_t));
                    slot = ngx_slab_exact_shift - pool->min_shift;

                    page->next = slots[slot].next;
                    slots[slot].next = page;

                    page->prev = (uintptr_t)&slots[slot] | NGX_SLAB_EXACT;
                    page->next->prev = (uintptr_t)page | NGX_SLAB_EXACT;
                }

                page->slab &= ~m;

                if (page->slab) {
                    goto done;
                }

                ngx_slab_free_pages(pool, page, 1);

                goto done;
            }

            goto chunk_already_free;

        case NGX_SLAB_BIG:

            shift = slab & NGX_SLAB_SHIFT_MASK;
            size = 1 << shift;

            if ((uintptr_t)p & (size - 1)) {
                goto wrong_chunk;
            }

            m = (uintptr_t)1
                << ((((uintptr_t)p & (ngx_pagesize - 1)) >> shift) +
                    NGX_SLAB_MAP_SHIFT);

            if (slab & m) {
                if (page->next == NULL) {
                    slots = (ngx_slab_page_t *)((u_char *)pool +
                                                sizeof(ngx_slab_pool_t));
                    slot = shift - pool->min_shift;

                    page->next = slots[slot].next;
                    slots[slot].next = page;

                    page->prev = (uintptr_t)&slots[slot] | NGX_SLAB_BIG;
                    page->next->prev = (uintptr_t)page | NGX_SLAB_BIG;
                }

                page->slab &= ~m;

                if (page->slab & NGX_SLAB_MAP_MASK) {   // 本页还有被占用的内存
                    goto done;
                }

                ngx_slab_free_pages(pool, page, 1);

                goto done;
            }

            goto chunk_already_free;

        case NGX_SLAB_PAGE:

            if ((uintptr_t)p & (ngx_pagesize - 1)) {
                goto wrong_chunk;
            }

            if (slab == NGX_SLAB_PAGE_FREE) {
                ngx_slab_error(pool, NGX_LOG_ALERT,
                               "ngx_slab_free(): page is already free");
                goto fail;
            }

            if (slab == NGX_SLAB_PAGE_BUSY) {
                ngx_slab_error(pool, NGX_LOG_ALERT,
                               "ngx_slab_free(): pointer to wrong page");
                goto fail;
            }

            n = ((u_char *)p - pool->start) >> ngx_pagesize_shift;
            size = slab & ~NGX_SLAB_PAGE_START;

            ngx_slab_free_pages(pool, &pool->pages[n], size);

            ngx_slab_junk(p, size << ngx_pagesize_shift);

            return;
    }

    /* not reached */

    return;

done:

    ngx_slab_junk(p, size);

    return;

wrong_chunk:

    ngx_slab_error(pool, NGX_LOG_ALERT,
                   "ngx_slab_free(): pointer to wrong chunk");

    goto fail;

chunk_already_free:

    ngx_slab_error(pool, NGX_LOG_ALERT,
                   "ngx_slab_free(): chunk is already free");

fail:

    return;
}

/**
 * @brief 申请整页的内存。第一个符合的ngx_slab_page_t管理节点就是可用的了，
 *        如果申请的大于1页，那么其余的ngx_slab_page_t节点就标记为已占用。
 *
 * @param pool
 * @param pages 总共申请几页
 * @return ngx_slab_page_t* 返回的是page管理结构
 */
static ngx_slab_page_t *ngx_slab_alloc_pages(ngx_slab_pool_t *pool,
                                             ngx_uint_t pages) {
    ngx_slab_page_t *page, *p;

    for (page = pool->free.next; page != &pool->free; page = page->next) {
        if (page->slab >= pages) {
            if (page->slab > pages) {   // 太大了，需要分割内存
                // 将该管理结点page和之后用到的管理节点都移出，
                // 第一个作为节点，其他的不用，
                // page[pages]是分割出来的空闲节点
                page[pages].slab = page->slab - pages;  // 分割出来的节点管理多少页面
                page[pages].next = page->next;  // 将page摘出来
                page[pages].prev = page->prev;

                p = (ngx_slab_page_t *)page->prev;
                p->next = &page[pages];
                page->next->prev = (uintptr_t)&page[pages];

            } else {    // 正好就是这么多页数，就不用管后续的了
                p = (ngx_slab_page_t *)page->prev;
                p->next = page->next;
                page->next->prev = page->prev;
            }

            page->slab = pages | NGX_SLAB_PAGE_START;
            page->next = NULL;
            page->prev = NGX_SLAB_PAGE;

            if (--pages == 0) { // 只有1个page
                return page;
            }

            // 将已经分配出去的管理节点都给设置特定标志
            for (p = page + 1; pages; pages--) {
                p->slab = NGX_SLAB_PAGE_BUSY;
                p->next = NULL;
                p->prev = NGX_SLAB_PAGE;
                p++;
            }

            return page;
        }
    }

    ngx_slab_error(pool, NGX_LOG_CRIT, "ngx_slab_alloc() failed: no memory");

    return NULL;
}

static void ngx_slab_free_pages(ngx_slab_pool_t *pool, ngx_slab_page_t *page,
                                ngx_uint_t pages) {
    ngx_slab_page_t *prev;

    page->slab = pages--;

    if (pages) {
        ngx_memzero(&page[1], pages * sizeof(ngx_slab_page_t));
    }

    if (page->next) {
        prev = (ngx_slab_page_t *)(page->prev & ~NGX_SLAB_PAGE_MASK);
        prev->next = page->next;
        page->next->prev = page->prev;
    }

    page->prev = (uintptr_t)&pool->free;
    page->next = pool->free.next;

    page->next->prev = (uintptr_t)page;

    pool->free.next = page;
}

static void ngx_slab_error(ngx_slab_pool_t *pool, ngx_uint_t level,
                           char *text) {
    ngx_log_error(level, ngx_cycle->log, 0, "%s%s", text, pool->log_ctx);
}
