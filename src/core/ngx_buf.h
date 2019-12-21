
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

/**
 * 内存块，内存链，链表结点，分层次控制
 */

#ifndef _NGX_BUF_H_INCLUDED_
#define _NGX_BUF_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef void *            ngx_buf_tag_t;

typedef struct ngx_buf_s  ngx_buf_t;

struct ngx_buf_s {
    u_char          *pos;       // 内存中, 数据开始的位置
    u_char          *last;      // 内存中，数据结束的位置
    off_t            file_pos;  // 文件中，数据当前位置
    off_t            file_last; // 文件中，数据结束位置

    u_char          *start;         /* start of buffer */ // 内存段的开头
    u_char          *end;           /* end of buffer */
    ngx_buf_tag_t    tag;
    ngx_file_t      *file;
    ngx_buf_t       *shadow;


    /* the buf's content could be changed */
    unsigned         temporary:1;

    /*
     * the buf's content is in a memory cache or in a read only memory
     * and must not be changed
     */
    unsigned         memory:1;

    /* the buf's content is mmap()ed and must not be changed */
    unsigned         mmap:1;

    unsigned         recycled:1;
    unsigned         in_file:1;
    unsigned         flush:1;
    unsigned         sync:1;
    unsigned         last_buf:1;
    unsigned         last_in_chain:1;

    unsigned         last_shadow:1;
    unsigned         temp_file:1;

    /* STUB */ int   num;
};


// 链表节点，将内存串成链表，不能用侵入式链表，因为一个buf可能在不同的chain中
struct ngx_chain_s {
    ngx_buf_t    *buf;
    ngx_chain_t  *next;
};


// 对一串内存的描述，
typedef struct {
    ngx_int_t    num;   // 内存个数
    size_t       size;  // 每个内存大小
} ngx_bufs_t;


typedef struct ngx_output_chain_ctx_s  ngx_output_chain_ctx_t;

typedef ngx_int_t (*ngx_output_chain_filter_pt)(void *ctx, ngx_chain_t *in);

#if (NGX_HAVE_FILE_AIO)
typedef void (*ngx_output_chain_aio_pt)(ngx_output_chain_ctx_t *ctx,
    ngx_file_t *file);
#endif

// 专门设计发送的数据结构
struct ngx_output_chain_ctx_s {
    ngx_buf_t                   *buf;       // 要发送的buf，不断变化中，相当于过滤后的一个缓存
    ngx_chain_t                 *in;        // 待过滤的数据 链
    ngx_chain_t                 *free;
    ngx_chain_t                 *busy;

    unsigned                     sendfile:1;
    unsigned                     directio:1;
#if (NGX_HAVE_ALIGNED_DIRECTIO)
    unsigned                     unaligned:1;
#endif
    unsigned                     need_in_memory:1;
    unsigned                     need_in_temp:1;
#if (NGX_HAVE_FILE_AIO)
    unsigned                     aio:1;

    ngx_output_chain_aio_pt      aio_handler;
#endif

    off_t                        alignment;

    ngx_pool_t                  *pool;      // 该内存串使用的内存池
    ngx_int_t                    allocated;
    ngx_bufs_t                   bufs;      // 对内存串的描述，只是个描述
    ngx_buf_tag_t                tag;

    ngx_output_chain_filter_pt   output_filter;  // 输出过滤操作
    void                        *filter_ctx;
};

// 写的上下文
typedef struct {
    ngx_chain_t                 *out;
    ngx_chain_t                **last;
    ngx_connection_t            *connection;
    ngx_pool_t                  *pool;
    off_t                        limit;
} ngx_chain_writer_ctx_t;


#define NGX_CHAIN_ERROR     (ngx_chain_t *) NGX_ERROR


#define ngx_buf_in_memory(b)        (b->temporary || b->memory || b->mmap)
#define ngx_buf_in_memory_only(b)   (ngx_buf_in_memory(b) && !b->in_file)

#define ngx_buf_special(b)                                                   \
    ((b->flush || b->last_buf || b->sync)                                    \
     && !ngx_buf_in_memory(b) && !b->in_file)

#define ngx_buf_sync_only(b)                                                 \
    (b->sync                                                                 \
     && !ngx_buf_in_memory(b) && !b->in_file && !b->flush && !b->last_buf)

#define ngx_buf_size(b)                                                      \
    (ngx_buf_in_memory(b) ? (off_t) (b->last - b->pos):                      \
                            (b->file_last - b->file_pos))

ngx_buf_t *ngx_create_temp_buf(ngx_pool_t *pool, size_t size);
/**
 * @brief 申请一串内存，组成chain
 *
 * @param pool
 * @param bufs 对该串内存的描述，num, size
 *
 * @return
 */
ngx_chain_t *ngx_create_chain_of_bufs(ngx_pool_t *pool, ngx_bufs_t *bufs);


#define ngx_alloc_buf(pool)  ngx_palloc(pool, sizeof(ngx_buf_t))
#define ngx_calloc_buf(pool) ngx_pcalloc(pool, sizeof(ngx_buf_t))

/**
 * @brief 申请一个链表节点，或者从pool池子里拿出来，或者新创建
 *
 * @param pool
 * @return ngx_chain_t*
 */
ngx_chain_t *ngx_alloc_chain_link(ngx_pool_t *pool);

// 释放是还给pool链表，不是完全删除
#define ngx_free_chain(pool, cl)                                             \
    cl->next = pool->chain;                                                  \
    pool->chain = cl

/**
 * @brief 将 @in 中的内存串准备就绪，放到就绪链表中，所有关于内存的处理都在这里完成：
 *         将文件中的内容挪到内存中、对内容进行过滤等等操作。
 *
 * @param ctx 内容发送的上下文
 * @param in 要处理的内存串
 * @return ngx_int_t
 */
ngx_int_t ngx_output_chain(ngx_output_chain_ctx_t *ctx, ngx_chain_t *in);

/**
 * @brief 将 @in 中的buf都发送出去，会有限速控制
 *
 * @param ctx
 * @param in
 * @return ngx_int_t
 */
ngx_int_t ngx_chain_writer(void *ctx, ngx_chain_t *in);

/**
 * @brief 将 @in 中的内存串复制到 @chain 中，不管节点头
 *
 * @param pool
 * @param chain
 * @param in
 * @return ngx_int_t
 */
ngx_int_t ngx_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain,
    ngx_chain_t *in);

/**
 * @brief 从 @free 空闲链表中取一个，没有就从 @p 新创建一个
 *
 * @param p
 * @param free
 * @return ngx_chain_t*
 */
ngx_chain_t *ngx_chain_get_free_buf(ngx_pool_t *p, ngx_chain_t **free);

/**
 * @brief 将 @out 和 @busy 中的内存串都清空，其中标记为 @tag 的内存块放入 @free 链表
 *          不是 @tag 标记的直接释放
 *
 * @param p
 * @param free
 * @param busy
 * @param out
 * @param tag
 */
void ngx_chain_update_chains(ngx_pool_t *p, ngx_chain_t **free,
    ngx_chain_t **busy, ngx_chain_t **out, ngx_buf_tag_t tag);


#endif /* _NGX_BUF_H_INCLUDED_ */
