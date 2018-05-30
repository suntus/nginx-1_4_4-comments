
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HASH_H_INCLUDED_
#define _NGX_HASH_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

// 槽位
typedef struct {
    void             *value;    // 指向用户自定义的指针，如果当前槽为空，则指向0
    u_short           len;      // 元素关键字长度
    u_char            name[1];  // 元素关键字首地址(利用了跟0长数组一样的技巧)
} ngx_hash_elt_t;


typedef struct {
    ngx_hash_elt_t  **buckets;  // 指向散列表的槽
    ngx_uint_t        size;     // 散列表中槽的总数
} ngx_hash_t;


// 模糊匹配
typedef struct {
    ngx_hash_t        hash;
    void             *value;
} ngx_hash_wildcard_t;

// 散列表中的元素
typedef struct {
    ngx_str_t         key;      // 元素关键字
    ngx_uint_t        key_hash; // 由散列方法算出来的关键码
    void             *value;    // 指向实际的用户数据
} ngx_hash_key_t;

// 可以自定义散列函数，有两个预设的散列函数，ngx_hash_key, ngx_hash_key_lc
typedef ngx_uint_t (*ngx_hash_key_pt) (u_char *data, size_t len);


typedef struct {
    ngx_hash_t            hash;
    ngx_hash_wildcard_t  *wc_head;
    ngx_hash_wildcard_t  *wc_tail;
} ngx_hash_combined_t;


// 专门用于散列表初始化
typedef struct {
    ngx_hash_t       *hash; // 指向普通的完全散列表
    ngx_hash_key_pt   key;  // 用于初始化预添加元素的散列方法

    ngx_uint_t        max_size; // 散列表中槽的最大个数
    ngx_uint_t        bucket_size;  // 每个槽的大小，限制元素关键字的最大长度

    char             *name; // 散列表名称
    ngx_pool_t       *pool; // 内存池，用于分配最多三个散列表的所有槽
    ngx_pool_t       *temp_pool;    // 临时内存池，用于分配初始化之前的一些临时数组，
                                    // 带通配符的元素初始化的时候也要用到
} ngx_hash_init_t;


#define NGX_HASH_SMALL            1
#define NGX_HASH_LARGE            2

#define NGX_HASH_LARGE_ASIZE      16384
#define NGX_HASH_LARGE_HSIZE      10007

#define NGX_HASH_WILDCARD_KEY     1
#define NGX_HASH_READONLY_KEY     2


typedef struct {
    // 下面的keys_hash，dns_wc_head_hash,dns_tail_hash都是简易哈希表,hsize指明了这些
    // 简易散列表的槽数，其简易散列方法也要对hsize取余
    ngx_uint_t        hsize;

    ngx_pool_t       *pool; // 暂时无意义
    ngx_pool_t       *temp_pool;    // 临时内存池，下面的动态数组需要的内存都由其分配

    // 动态数组保存着以ngx_hash_key_t 结构体保存着不含通配符关键字的元素
    ngx_array_t       keys;
    // 一个极其简易的散列表，它以数组的形式保存着hsize个元素，每个元素都是ngx_array_t动态
    // 数组。在用户添加元素过程中，会根据关键码将用户的ngx_str_t类型的关键字添加到ngx_array_t
    // 动态数组中，这里所有的用户元素的关键字都不带通配符，表示精确匹配
    // 主要用于在array中添加元素时，去重或者合并元素，下边的dns_wc_head_hash和
    // dns_wc_tail_hash用法都是一样
    ngx_array_t      *keys_hash;

    ngx_array_t       dns_wc_head;
    ngx_array_t      *dns_wc_head_hash;

    ngx_array_t       dns_wc_tail;
    ngx_array_t      *dns_wc_tail_hash;
} ngx_hash_keys_arrays_t;


typedef struct {
    ngx_uint_t        hash;
    ngx_str_t         key;
    ngx_str_t         value;
    u_char           *lowcase_key;
} ngx_table_elt_t;


void *ngx_hash_find(ngx_hash_t *hash, ngx_uint_t key, u_char *name, size_t len);
void *ngx_hash_find_wc_head(ngx_hash_wildcard_t *hwc, u_char *name, size_t len);
void *ngx_hash_find_wc_tail(ngx_hash_wildcard_t *hwc, u_char *name, size_t len);
void *ngx_hash_find_combined(ngx_hash_combined_t *hash, ngx_uint_t key,
    u_char *name, size_t len);

ngx_int_t ngx_hash_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
    ngx_uint_t nelts);
ngx_int_t ngx_hash_wildcard_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
    ngx_uint_t nelts);

// 预设的hash算法, BKDR算法
#define ngx_hash(key, c)   ((ngx_uint_t) key * 31 + c)
// 预设的两个散列函数，根据输入的字符串data映射成uint类型的key
// 就是生成字符串的key
ngx_uint_t ngx_hash_key(u_char *data, size_t len);
// 先将字符串data转换成小写，再去映射，也就是不去区分大小写
ngx_uint_t ngx_hash_key_lc(u_char *data, size_t len);
// 这个在散列的同时，会把src转换成小写输出到dst中
ngx_uint_t ngx_hash_strlow(u_char *dst, u_char *src, size_t n);


ngx_int_t ngx_hash_keys_array_init(ngx_hash_keys_arrays_t *ha, ngx_uint_t type);
ngx_int_t ngx_hash_add_key(ngx_hash_keys_arrays_t *ha, ngx_str_t *key,
    void *value, ngx_uint_t flags);


#endif /* _NGX_HASH_H_INCLUDED_ */
