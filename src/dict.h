/* Hash Tables Implementation.
 *
 * This file implements in-memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto-resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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
 */

#include <stdint.h>

#ifndef __DICT_H
#define __DICT_H

// 定义成功与出错的值
#define DICT_OK 0
#define DICT_ERR 1

/* Unused arguments generate annoying warnings... */
// 没有使用dict 时，用于提示警告
#define DICT_NOTUSED(V) ((void) V)

/* 字典结构体 ，用于保存key-vaule */
typedef struct dictEntry {
    void *key;  // 字典 key函数指针
    union {
        void *val;
        uint64_t u64;   // 无符号 64位整型
        int64_t s64;    // 有符号 64整型
        double d;
    } v;
    // 指向下一个字典节点
    struct dictEntry *next;
} dictEntry;

/* 字典类型*/
typedef struct dictType {
    uint64_t (*hashFunction)(const void *key);                                   // 哈希函数
    void *(*keyDup)(void *privdata, const void *key);                           // 复制key 函数指针
    void *(*valDup)(void *privdata, const void *obj);                           // 复制value 函数指针
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);     // 进行key值的比较
    void (*keyDestructor)(void *privdata, void *key);                            // key 的析构
    void (*valDestructor)(void *privdata, void *obj);                            // value 的析构
} dictType;

/* This is our hash table structure. Every dictionary has two of this as we
 * implement incremental rehashing, for the old to the new table.
 * 哈希表结构体
 * */
typedef struct dictht {
    dictEntry **table;                   // 字典实体
    unsigned long size;                // 可容纳的长度
    unsigned long sizemask;            // 长度-1， 为了方便获取元素的index，应该是sizemask & hash值 = 元素的下标
    unsigned long used;                // 已经使用的数量
} dictht;

/* 字典主操作类 */
typedef struct dict {
    dictType *type;                     // 字典类型
    void *privdata;                    // 私有数据指针
    dictht ht[2];                       // 字典的哈希表，一张旧的，一张新的
    long rehashidx; /* rehashing not in progress if rehashidx == -1 */           // 重新定位哈希时的下标,等于-1时，表示没有处于rehash状态
    unsigned long iterators; /* number of iterators currently running */        // 当前还在运行的安全迭代器数量
} dict;

/* If safe is set to 1 this is a safe iterator, that means, you can call
 * dictAdd, dictFind, and other functions against the dictionary even while
 * iterating. Otherwise it is a non safe iterator, and only dictNext()
 * should be called while iterating.
 * 字典迭代器，如果是安全迭代器，safe设置为1，可调用 dictAdd,dictFind,dictDelete
 * 如果是不安全的，迭代时只能调用dictNext,而不对字典进行修改
 *
 * 安全迭代器允许在迭代的过程中对字典结构进行修改，即添加、删除、修改字典中的键值对节点
 * 不安全迭代器不允许对字典中任何节点进行修改，只能调用dictNext
 * */
typedef struct dictIterator {
    dict *d;                             // 指向一个即将被迭代的字典结构
    long index;                         // 记录当前迭代到字典的桶索引
    // table 取值0,1 表示当前迭代的是字典中哪个哈希表，新旧表
    // safe表示当前迭代器是安全还是不安全的
    int table, safe;
    // entry是当前迭代的节点，nextEntry 等于entry的next 指针，防止当前节点接收删除操作后续节点丢失
    dictEntry *entry, *nextEntry;
    /* unsafe iterator fingerprint for misuse detection. */
    // fingerprint 保存了dictFingerprint() 函数根据当前字典的基本信息计算的一个指纹信息
    // 稍有变动，指纹信息就会发生变化，用于不安全迭代器检验
    long long fingerprint;
} dictIterator;

/* 字典扫描方法 */
typedef void (dictScanFunction)(void *privdata, const dictEntry *de);
/* 字典扫描桶方法 */
typedef void (dictScanBucketFunction)(void *privdata, dictEntry **bucketref);

/* This is the initial size of every hash table */
// 哈希表的初始化数量
#define DICT_HT_INITIAL_SIZE     4

/* ------------------------------- Macros 宏定义--------------------------------*/
/* 字典释放Val函数时使用，如果dict中的dictType 定义对应的函数指针 */
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata, (entry)->v.val)

/* 字典val函数复制时使用，如果dict中的dictType 定义对应的函数指针 */
#define dictSetVal(d, entry, _val_) do { \
    if ((d)->type->valDup) \
        (entry)->v.val = (d)->type->valDup((d)->privdata, _val_); \
    else \
        (entry)->v.val = (_val_); \
} while(0)

/* 设置dictEntry中共用体v中有符号类型的值 */
#define dictSetSignedIntegerVal(entry, _val_) \
    do { (entry)->v.s64 = _val_; } while(0)

/* 设置dictEntry中共用体v中无符号类型的值 */
#define dictSetUnsignedIntegerVal(entry, _val_) \
    do { (entry)->v.u64 = _val_; } while(0)

/* 设置dictEntry中共用体v中double类型的值 */
#define dictSetDoubleVal(entry, _val_) \
    do { (entry)->v.d = _val_; } while(0)

/* 调用dictType定义的key 析构函数*/
#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata, (entry)->key)

/* 调用dictType定义的key复制函数，没有定义直接赋值 */
#define dictSetKey(d, entry, _key_) do { \
    if ((d)->type->keyDup) \
        (entry)->key = (d)->type->keyDup((d)->privdata, _key_); \
    else \
        (entry)->key = (_key_); \
} while(0)

/* 调用dictType定义的key比较函数，没有定义直接key值直接比较 */
#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata, key1, key2) : \
        (key1) == (key2))

#define dictHashKey(d, key) (d)->type->hashFunction(key)        // 哈希定位方法
#define dictGetKey(he) ((he)->key)                               // 获取dictEntry的key值
#define dictGetVal(he) ((he)->v.val)                             // 获取dictEntry中共用体v的val值
#define dictGetSignedIntegerVal(he) ((he)->v.s64)              // 获取dictEntry中共用体v的64位有符号值
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)            // 获取dictEntry中共用体v的64位无符号值
#define dictGetDoubleVal(he) ((he)->v.d)                        // 获取dictEntry中共用体v的double值
#define dictSlots(d) ((d)->ht[0].size+(d)->ht[1].size)           // 获取dict字典中总的表大小
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)            // 获取dict字典中两个表中已使用的数量
#define dictIsRehashing(d) ((d)->rehashidx != -1)               // 判断当前字典表是否处于rehash状态，正在rehash说明一部分键在ht[0]，一部分在ht[1]

/* API */
dict *dictCreate(dictType *type, void *privDataPtr);                                 // 创建一个字典
int dictExpand(dict *d, unsigned long size);                                       // 字典扩容
int dictAdd(dict *d, void *key, void *val);                                         // 字典添加一个元素
dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing);                     // 添加键值对，若存在则返回NULL,否则直接添加
dictEntry *dictAddOrFind(dict *d, void *key);                                        // 可以完成添加和查找操作
int dictReplace(dict *d, void *key, void *val);                                     // 替换dict中的一个元素
int dictDelete(dict *d, const void *key);                                           // 根据key删除一个元素
dictEntry *dictUnlink(dict *ht, const void *key);                                   //  根据key获取一个元素，并脱离字典
void dictFreeUnlinkedEntry(dict *d, dictEntry *he);                                  //  释放某个元素
void dictRelease(dict *d);                                                           //  释放整个字典
dictEntry * dictFind(dict *d, const void *key);                                     //  查询键值对
void *dictFetchValue(dict *d, const void *key);                                     // 根据key值寻找相应的val值
int dictResize(dict *d);                                                             // 重新计算大小
dictIterator *dictGetIterator(dict *d);                                              // 安全迭代器获取方式
dictIterator *dictGetSafeIterator(dict *d);                                          // 不安全迭代器获取方式
dictEntry *dictNext(dictIterator *iter);                                             // 迭代器获取字典的下一个节点
void dictReleaseIterator(dictIterator *iter);                                        // 释放迭代器
dictEntry *dictGetRandomKey(dict *d);                                                // 随机获取一个字典集
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count);       // 选取指定数目的dictEntry
void dictGetStats(char *buf, size_t bufsize, dict *d);                              //
uint64_t dictGenHashFunction(const void *key, int len);                             // 输入的key值，目标长度，此方法帮你计算出索引值，利用siphash 算法
uint64_t dictGenCaseHashFunction(const unsigned char *buf, int len);               // 同样计算hash值，方式更为简单
void dictEmpty(dict *d, void(callback)(void*));                                     // 清空字典
void dictEnableResize(void);                                                         // 启用调整方法
void dictDisableResize(void);                                                        // 禁用调整方法
int dictRehash(dict *d, int n);                                                      // redis 的rehash ,redis是单线程的
int dictRehashMilliseconds(dict *d, int ms);                                         // 在给定时间内，循环执行rehash
void dictSetHashFunctionSeed(uint8_t *seed);                                         //  设置哈希种子
uint8_t *dictGetHashFunctionSeed(void);                                              // 获取哈希种子
/* 执行字典扫描方法 */
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, dictScanBucketFunction *bucketfn, void *privdata);
uint64_t dictGetHash(dict *d, const void *key);                                      // 获取哈希值
dictEntry **dictFindEntryRefByPtrAndHash(dict *d, const void *oldptr, uint64_t hash);   // 根据指针和hash值获取对应的dictEntry

/* Hash table types */
/* 哈希表类型 */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#endif /* __DICT_H */
