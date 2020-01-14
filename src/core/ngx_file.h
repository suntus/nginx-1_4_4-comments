
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_FILE_H_INCLUDED_
#define _NGX_FILE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


// 文件句柄，包含各种文件信息
struct ngx_file_s {
    ngx_fd_t                   fd;
    ngx_str_t                  name;
    ngx_file_info_t            info;    // file stat

    // 这两个offset是为了标注是多线程的offset还是系统offset，
    // 对第一个offset使用pread去操作文件，对第二个sys_offset用lseek先去查到指定位置
    // pread不会改变文件在系统中的offset，read会改变
    off_t                      offset;
    off_t                      sys_offset;

    ngx_log_t                 *log;

#if (NGX_HAVE_FILE_AIO)
    ngx_event_aio_t           *aio;
#endif

    unsigned                   valid_info:1;
    unsigned                   directio:1;
};


#define NGX_MAX_PATH_LEVEL  3


typedef time_t (*ngx_path_manager_pt) (void *data);
typedef void (*ngx_path_loader_pt) (void *data);


// 文件路径句柄
typedef struct {
    ngx_str_t                  name;        // 路径的基础名字，/spool/nginx/fastcgi_temp/
    size_t                     len;         // 要创建的临时文件的相对路径长度 7/45/
    size_t                     level[3];    // 最多3级子目录

    ngx_path_manager_pt        manager;
    ngx_path_loader_pt         loader;
    void                      *data;

    u_char                    *conf_file;   // 配置该路径的配置文件名
    ngx_uint_t                 line;        // 在配置文件中的行数
} ngx_path_t;


// 用于临时文件生成目录的条件
typedef struct {
    ngx_str_t                  name;        // 根目录的名称
    size_t                     level[3];    // 最多可以有3级子临时目录，每个子目录的名字长度
} ngx_path_init_t;


// 临时文件句柄
typedef struct {
    ngx_file_t                 file;    // 文件本身
    off_t                      offset;
    ngx_path_t                *path;    // 路径
    ngx_pool_t                *pool;
    char                      *warn;

    ngx_uint_t                 access;

    unsigned                   log_level:8;
    unsigned                   persistent:1;
    unsigned                   clean:1;
} ngx_temp_file_t;


// 重命名文件时的一些相关信息
typedef struct {
    ngx_uint_t                 access;
    ngx_uint_t                 path_access;
    time_t                     time;
    ngx_fd_t                   fd;

    unsigned                   create_path:1;
    unsigned                   delete_file:1;

    ngx_log_t                 *log;
} ngx_ext_rename_file_t;


// 复制文件时的一些相关信息
typedef struct {
    off_t                      size;
    size_t                     buf_size;

    ngx_uint_t                 access;
    time_t                     time;

    ngx_log_t                 *log;
} ngx_copy_file_t;


typedef struct ngx_tree_ctx_s  ngx_tree_ctx_t;

typedef ngx_int_t (*ngx_tree_init_handler_pt) (void *ctx, void *prev);
typedef ngx_int_t (*ngx_tree_handler_pt) (ngx_tree_ctx_t *ctx, ngx_str_t *name);

struct ngx_tree_ctx_s {
    off_t                      size;
    off_t                      fs_size;
    ngx_uint_t                 access;
    time_t                     mtime;

    ngx_tree_init_handler_pt   init_handler;
    ngx_tree_handler_pt        file_handler;
    ngx_tree_handler_pt        pre_tree_handler;
    ngx_tree_handler_pt        post_tree_handler;
    ngx_tree_handler_pt        spec_handler;

    void                      *data;
    size_t                     alloc;

    ngx_log_t                 *log;
};


// 将内存串中内容写入临时文件
ssize_t ngx_write_chain_to_temp_file(ngx_temp_file_t *tf, ngx_chain_t *chain);

// 创建临时文件
ngx_int_t ngx_create_temp_file(ngx_file_t *file, ngx_path_t *path,
    ngx_pool_t *pool, ngx_uint_t persistent, ngx_uint_t clean,
    ngx_uint_t access);

// 创建临时文件名
void ngx_create_hashed_filename(ngx_path_t *path, u_char *file, size_t len);

// 只是在创建临时文件时使用
ngx_int_t ngx_create_path(ngx_file_t *file, ngx_path_t *path);

// 一层层创建完整路径
ngx_err_t ngx_create_full_path(u_char *dir, ngx_uint_t access);

// 将临时文件路径模板加入到全局配置存储，用以检查是否有重复的路径配置
ngx_int_t ngx_add_path(ngx_conf_t *cf, ngx_path_t **slot);

// 统一创建一些临时使用文件的基础路径，临时路径会在运行时创建
ngx_int_t ngx_create_paths(ngx_cycle_t *cycle, ngx_uid_t user);

// 重命名文件，mv
ngx_int_t ngx_ext_rename_file(ngx_str_t *src, ngx_str_t *to,
    ngx_ext_rename_file_t *ext);

// 复制文件，cp
ngx_int_t ngx_copy_file(u_char *from, u_char *to, ngx_copy_file_t *cf);

// 遍历路径下的每一个文件
ngx_int_t ngx_walk_tree(ngx_tree_ctx_t *ctx, ngx_str_t *tree);

// 临时文件/目录名的取值，使用+1就得了，
ngx_atomic_uint_t ngx_next_temp_number(ngx_uint_t collision);

// 解析配置时对文件路径进行解析，解析类似这样的配置项：astcgi_temp_path path [level1 [level2 [level3]]];
// 配置成这样:  fastcgi_temp_path /spool/nginx/fastcgi_temp 1 2;
// 最后会生成这种格式的临时文件： /spool/nginx/fastcgi_temp/7/45/00000123457
char *ngx_conf_set_path_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

// 合并路径，init 是默认的文件路径模式
char *ngx_conf_merge_path_value(ngx_conf_t *cf, ngx_path_t **path,
    ngx_path_t *prev, ngx_path_init_t *init);

// 设置新建目录或文件的权限，比如
// Syntax:	fastcgi_store_access users:permissions ...;
// Default:
// fastcgi_store_access user:rw;
// Context:	http, server, location
char *ngx_conf_set_access_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


extern ngx_atomic_t      *ngx_temp_number;
extern ngx_atomic_int_t   ngx_random_number;


#endif /* _NGX_FILE_H_INCLUDED_ */
