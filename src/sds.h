/* SDSLib 2.0 -- A C dynamic strings library
 *
 * Copyright (c) 2006-2015, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2015, Oran Agra
 * Copyright (c) 2015, Redis Labs, Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 针对的是sds—简单动态字符串
 */

#ifndef __SDS_H
#define __SDS_H

/* 最大内存分配 */
#define SDS_MAX_PREALLOC (1024*1024)
const char *SDS_NOINIT;

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

/* 声明sds的一种char 类型*/
typedef char *sds;

//__attribute__ ((__packed__)) 保证了内存不对齐，即每次存储该结构时连续存储，保证后面api能够使用指针，直接访问想要的数据结构。
/* __attribute__ ((__packed__)) 是c语言作为紧凑型的空间压缩(c最少为4字节,可以节省大量字节空间),
*  也方便sds按压缩后的字节读取*/

/* Note: sdshdr5 is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings.
 * 长度最长为2的5次方 即32个字符  已经被放弃使用*/
struct __attribute__ ((__packed__)) sdshdr5 {
    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */
    char buf[];
};
/* 长度最长为2的8次方，即256个字符
 * 其内存的存储结构是连续的存储空间
 * */
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len; /* used 为已用容量，字符串的实际长度，不包括空终止符 */
    uint8_t alloc; /* excluding the header and null terminator 为已分配容量，不包括header 和结尾的终止符*/
    unsigned char flags; /* 3 lsb of type, 5 unused bits ——header 的类型标志 */
    char buf[];         /* 存放具体的buf[]，即字符串的实际内容*/
};
struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len; /* used */
    uint16_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t len; /* used */
    uint32_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t len; /* used */
    uint64_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};

/* 不同长度的字符串类型，对应的header不同，flag取值为0-4 */
#define SDS_TYPE_5  0
#define SDS_TYPE_8  1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4
#define SDS_TYPE_MASK 7
#define SDS_TYPE_BITS 3
#define SDS_HDR_VAR(T,s) struct sdshdr##T *sh = (void*)((s)-(sizeof(struct sdshdr##T)));    // 获取header 头指针
#define SDS_HDR(T,s) ((struct sdshdr##T *)((s)-(sizeof(struct sdshdr##T))))                  /* 获取header头指针*/
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)                                               // 获取sdshdr5的长度

/* 获取sds 真实长度，即字符串实际使用的长度 */
static inline size_t sdslen(const sds s) {
    unsigned char flags = s[-1];            // 用来获取sds head的类型
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->len;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->len;
    }
    return 0;
}

/* 计算sds 可用长度*/
static inline size_t sdsavail(const sds s) {
    // 通过flags 字段获取header的类型，获取flags 字段仅需要将字符串s前移一个字节
    unsigned char flags = s[-1];         // 获取flags 字段
    switch(flags&SDS_TYPE_MASK) {       // 获取header的类型
        case SDS_TYPE_5: {
            return 0;
        }
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8,s);
            return sh->alloc - sh->len;     /* alloc为当前容量，len为已用容量*/
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}

/* 在不超过sds 本类型容量的情况下重新设置sds的当前占用量*/
static inline void sdssetlen(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;      /* 获取head*/
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len = newlen;
            break;
    }
}

/* 在不超过sds本类型容量的情况下增加其使用量 */
static inline void sdsinclen(sds s, size_t inc) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                unsigned char newlen = SDS_TYPE_5_LEN(flags)+inc;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len += inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len += inc;
            break;
    }
}

/* sdsalloc() = sdsavail() + sdslen() ，计算sds的容量 */
static inline size_t sdsalloc(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->alloc;
    }
    return 0;
}

/* 在不超过sds本类型最大容量的情况下，重新设置其容量，sds5为固定值，其他类型可以动态扩容、缩容 */
static inline void sdssetalloc(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            /* Nothing to do, this type has no total allocation info. */
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->alloc = newlen;
            break;
    }
}

sds sdsnewlen(const void *init, size_t initlen);        /*  根据给定长度和初始字符串，新创建一个sds */
sds sdsnew(const char *init);                           /* 根据给定的值，新创建一个sds */
sds sdsempty(void);                                     /* 清空sds */
sds sdsdup(const sds s);                                /* 复制sds */
void sdsfree(sds s);                                    /* 释放给定的sds */
sds sdsgrowzero(sds s, size_t len);                     /* 扩展sds 到指定的长度 */
sds sdscatlen(sds s, const void *t, size_t len);       /* */
sds sdscat(sds s, const char *t);                      /* sds 连接char*字符串*/
sds sdscatsds(sds s, const sds t);                      /* sds 和sds 连接*/
sds sdscpylen(sds s, const char *t, size_t len);       /* 字符串复制相关 */
sds sdscpy(sds s, const char *t);                       /* 字符串复制相关 */

sds sdscatvprintf(sds s, const char *fmt, va_list ap);  /* 字符串格式化输出，是 sdscatprintf的包装 */
#ifdef __GNUC__
sds sdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

sds sdscatfmt(sds s, char const *fmt, ...);             /* 字符串格式化输出 */
sds sdstrim(sds s, const char *cset);                   /* 字符串缩减 */
void sdsrange(sds s, ssize_t start, ssize_t end);       /* 字符串截取 */
void sdsupdatelen(sds s);                               /* 更新字符串最新的长度 */
void sdsclear(sds s);                                   /* 字符串清空 */
int sdscmp(const sds s1, const sds s2);                /* sds 比较*/
sds *sdssplitlen(const char *s, ssize_t len, const char *sep, int seplen, int *count);      /* 字符串分割子字符串 */
void sdsfreesplitres(sds *tokens, int count);           /* 释放字符串数组 */
void sdstolower(sds s);                                 /* 字符串转小写 */
void sdstoupper(sds s);
sds sdsfromlonglong(long long value);                   /* 利用long long 值创建一个sds */
sds sdscatrepr(sds s, const char *p, size_t len);       /* 将长度为len的字符串p添加到给定sds的末尾 */
sds *sdssplitargs(const char *line, int *argc);         /* 参数拆分 */
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);                    /* 字符映射，ho", "01", h映射为0， o映射为1*/
sds sdsjoin(char **argv, int argc, char *sep);          /* 以分隔符连接字符串子数组构成新的字符串*/
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);

/* Low level functions exposed to the user API
 * 开放给使用者的API
 * */
sds sdsMakeRoomFor(sds s, size_t addlen);               /* 扩容 */
void sdsIncrLen(sds s, ssize_t incr);                   /* 用来计算调整sds字符串中len和free的大小*/
sds sdsRemoveFreeSpace(sds s);                          /* 压缩，*/
size_t sdsAllocSize(sds s);                             /* 返回sds字符串总的长度 */
void *sdsAllocPtr(sds s);                               /* 返回实际SDS分配的指针*/

/* Export the allocator used by SDS to the program using SDS.
 * Sometimes the program SDS is linked to, may use a different set of
 * allocators, but may want to allocate or free things that SDS will
 * respectively free or allocate. */
void *sds_malloc(size_t size);
void *sds_realloc(void *ptr, size_t size);
void sds_free(void *ptr);

#ifdef REDIS_TEST
int sdsTest(int argc, char *argv[]);
#endif

#endif
