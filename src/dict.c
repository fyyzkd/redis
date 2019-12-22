/* Hash Tables Implementation.
 *
 * This file implements in memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *  哈希表会根据需要自动调整大小为2的幂的表
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
 * redis 的底层数据结构（字典）
 */

#include "fmacros.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>

#include "dict.h"
#include "zmalloc.h"
#ifndef DICT_BENCHMARK_MAIN
#include "redisassert.h"
#else
#include <assert.h>
#endif

/* Using dictEnableResize() / dictDisableResize() we make possible to
 * enable/disable resizing of the hash table as needed. This is very important
 * for Redis, as we use copy-on-write and don't want to move too much memory
 * around when there is a child performing saving operations.
 *
 * Note that even when dict_can_resize is set to 0, not all resizes are
 * prevented: a hash table is still allowed to grow if the ratio between
 * the number of elements and the buckets > dict_force_resize_ratio.
 * redis 使用dictEnableResize() / dictDisableResize()方法使用/禁用 调整哈希表的长度
 * redis 使用写时复制的方式，不会移动太多的内存
 * 请注意，如果元素数和存储桶数之间的比率> dict_force_resize_ratio。即使将dict_can_resize设置为0，也不是所有调整大小都被禁止
 * */
static int dict_can_resize = 1;                                          // 当 dict_can_resize =1 时，允许执行扩展操作
static unsigned int dict_force_resize_ratio = 5;                        // 当比例used/size>dict_force_resize_ratio时，会强制调整执行rehash操作

/* -------------------------- private prototypes ---------------------------- */

static int _dictExpandIfNeeded(dict *ht);                               // 字典是否需要扩展
static unsigned long _dictNextPower(unsigned long size);             // 调整后的字典长度
static long _dictKeyIndex(dict *ht, const void *key, uint64_t hash, dictEntry **existing);      // 获取key对应的hash值，如果已存在则返回-1
static int _dictInit(dict *ht, dictType *type, void *privDataPtr);     // 字典初始化方法

/* -------------------------- hash functions -------------------------------- */
/* -------------------------- 计算哈希索引的方法 -------------------------------- */

static uint8_t dict_hash_function_seed[16];

/* 设置哈希种子 */
void dictSetHashFunctionSeed(uint8_t *seed) {
    memcpy(dict_hash_function_seed,seed,sizeof(dict_hash_function_seed));
}

/* 获取哈希种子 */
uint8_t *dictGetHashFunctionSeed(void) {
    return dict_hash_function_seed;
}

/* The default hashing function uses SipHash implementation
 * in siphash.c.
 * redis 字典默认使用sipHash 算法来计算key的哈希值
 * */
// 以下调用siphash.c
uint64_t siphash(const uint8_t *in, const size_t inlen, const uint8_t *k);
uint64_t siphash_nocase(const uint8_t *in, const size_t inlen, const uint8_t *k);

uint64_t dictGenHashFunction(const void *key, int len) {
    return siphash(key,len,dict_hash_function_seed);
}

uint64_t dictGenCaseHashFunction(const unsigned char *buf, int len) {
    return siphash_nocase(buf,len,dict_hash_function_seed);
}

/* ----------------------------- API implementation ------------------------- */

/* Reset a hash table already initialized with ht_init().
 * NOTE: This function should only be called by ht_destroy().
 * 重置表
 * */
static void _dictReset(dictht *ht)
{
    // 清空相应的数据
    ht->table = NULL;           // ht->table指的是dictEntry
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

/* Create a new hash table  创建一个新的哈希表*/
dict *dictCreate(dictType *type,
        void *privDataPtr)
{
    dict *d = zmalloc(sizeof(*d));      // 申请内存空间分配

    _dictInit(d,type,privDataPtr);      // 字典初始化
    return d;
}

/* Initialize the hash table 字典初始化 */
int _dictInit(dict *d, dictType *type,
        void *privDataPtr)
{
    // 重置两个哈希表
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);
    d->type = type;                 // 设置dictType
    d->privdata = privDataPtr;      // 设置私有数据指针
    d->rehashidx = -1;              // -1表示没有处于rehash操作
    d->iterators = 0;               // 迭代器数量为0
    return DICT_OK;               // 返回DICT_OK 表示初始化成功
}

/* Resize the table to the minimal size that contains all the elements,
 * but with the invariant of a USED/BUCKETS ratio near to <= 1
 * 扩容入口
 * 重新调整哈希表，用最小的空间容纳所有的字典集合
 * 但USED / BUCKETS比率的不变性接近<= 1
 * 先设置扩容前的最小值，后进行扩容操作
 * */
int dictResize(dict *d)
{
    int minimal;
    // 如果不允许执行扩容或正在执行rehash操作
    // dict_can_resize 为 static int 全局变量 开始为1
    if (!dict_can_resize || dictIsRehashing(d)) return DICT_ERR;
    minimal = d->ht[0].used;        //  最小值为旧表目前使用的大小

    // 这里是特意定为4的。保证每次扩容之后，都是2的倍数
    // 而每次扩容都是上一次乘以2，因此，hash的桶的容量必然为2的幂
    if (minimal < DICT_HT_INITIAL_SIZE)
        minimal = DICT_HT_INITIAL_SIZE;
    // 调用dictExpand 执行扩容操作
    return dictExpand(d, minimal);
}

/* Expand or create the hash table 扩增或创建哈希表，虽然扩容了，但实际上并没有移动元素*/
int dictExpand(dict *d, unsigned long size)
{
    /* the size is invalid if it is smaller than the number of
     * elements already inside the hash table
     * 如果size小于当前哈希表的元素个数，则说明size值非法值
     * */
    if (dictIsRehashing(d) || d->ht[0].used > size)
        // 正处于rehash过程中或者size值非法
        return DICT_ERR;

    dictht n; /* the new hash table 新的哈希表*/
    unsigned long realsize = _dictNextPower(size);  // 计算新的哈希表容量，原来的2倍

    /* Rehashing to the same table size is not useful.
     * 如果新的哈希值不符合要求
     * */
    if (realsize == d->ht[0].size) return DICT_ERR;

    /* Allocate the new hash table and initialize all pointers to NULL
     * 分配新哈希表的内存，并初始化所有的参数
     * */
    n.size = realsize;
    n.sizemask = realsize-1;
    n.table = zcalloc(realsize*sizeof(dictEntry*));     // 为新哈希表申请内存
    n.used = 0;

    /* Is this the first initialization? If so it's not really a rehashing
     * we just set the first hash table so that it can accept keys. */
    if (d->ht[0].table == NULL) {
        d->ht[0] = n;       // 第一次扩容时，table[0]指向新的哈希表
        return DICT_OK;
    }

    /* Prepare a second hash table for incremental rehashing */
    d->ht[1] = n;           // 赋值给新表
    d->rehashidx = 0;       // 更新状态为0
    return DICT_OK;
}

/* Performs N steps of incremental rehashing. Returns 1 if there are still
 * keys to move from the old to the new hash table, otherwise 0 is returned.
 *
 * Note that a rehashing step consists in moving a bucket (that may have more
 * than one key as we use chaining) from the old to the new hash table, however
 * since part of the hash table may be composed of empty spaces, it is not
 * guaranteed that this function will rehash even a single bucket, since it
 * will visit at max N*10 empty buckets in total, otherwise the amount of
 * work it does would be unbound and the function may block for a long time.
 * 更新n个元素：
 * hash重定位，主要是将旧表映射到新表中
 * 返回1说明旧表还有key移到新表中，返回0则说明没有
 * rehash过程：
 *      (1)创建一个比ht[0]更大的ht[1]
 *      (2)将ht[0]中的所有键值移到ht[1]中
 *      (3)将原有ht[0]清空，并将ht[1]替换为ht[0]
 * */
int dictRehash(dict *d, int n) {
    /* 最大访问空桶数量，用于保证在一定时间内能够更新完成，进一步减小可能引起阻塞的时间*/
    int empty_visits = n*10; /* Max number of empty buckets to visit. */
    // 如果表不处于rehash操作，说明没有元素需要重定位
    if (!dictIsRehashing(d)) return 0;           /*如果没有正在再hash则不用查找第二张表，因为rehash本就是将元素从旧表移动到新表中*/
    // n次 && 旧表已使用的数量>0
    //n为每次迁移的步长，扩容时，每次只移动 n 个元素，防止 redis 阻塞
    while(n-- && d->ht[0].used != 0) {
        dictEntry *de, *nextde;

        /* Note that rehashidx can't overflow as we are sure there are more
         * elements because ht[0].used != 0
         * 保证旧表的长度大于更新的下标 ，rehashidx 表示重定位哈希的下标
         * */
        assert(d->ht[0].size > (unsigned long)d->rehashidx);
        // 空的查找次数
        while(d->ht[0].table[d->rehashidx] == NULL) {
            d->rehashidx++;
            if (--empty_visits == 0) return 1;  // 说明空元素的查找次数
        }
        de = d->ht[0].table[d->rehashidx];      // 获得非空桶的下标
        /* Move all the keys in this bucket from the old to the new hash HT */
        /* 将桶中所有元素从旧表移到新表 ，此处是移动的关键操作*/
        while(de) {
            uint64_t h;

            nextde = de->next;
            /* Get the index in the new hash table  获取新哈希表的下标*/
            /*redis规则：通过hash函数获取的哈希值 与 sizemask（表的长度-1 ）直接得到
                  元素下标。由于容量为2的幂级数 那么sizemask为容量-1
                  可得出sizemask为二进制全1的数
                  与操作是取出来hash的低位，舍去了高位。
              */
            h = dictHashKey(d, de->key) & d->ht[1].sizemask;
            de->next = d->ht[1].table[h];       // 头插法
            d->ht[1].table[h] = de;
            d->ht[0].used--;    // 旧表使用次数--
            d->ht[1].used++;    // 新表使用次数++
            de = nextde;        // 指向下一个元素
        }
        d->ht[0].table[d->rehashidx] = NULL;    // 移动完元素后，将旧表置空
        d->rehashidx++;                          // 更新下标
    }

    /* Check if we already rehashed the whole table... */
    /* 检测是否更新了整张表 */
    if (d->ht[0].used == 0) {
        zfree(d->ht[0].table);  // 释放旧表
        d->ht[0] = d->ht[1];    // 旧表指向新表
        _dictReset(&d->ht[1]);  // 重置新表
        d->rehashidx = -1;      // 更新rehash状态，-1表示未处于重定位状态，标识rehash操作已完成
        return 0;
    }

    /* More to rehash... */
    return 1;
}

/* 获取当前毫秒时间 */
long long timeInMilliseconds(void) {
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

/* Rehash for an amount of time between ms milliseconds and ms+1 milliseconds */
/* 在给定的时间内循环执行rehash */
int dictRehashMilliseconds(dict *d, int ms) {
    long long start = timeInMilliseconds();
    int rehashes = 0;   // 重定位的次数

    while(dictRehash(d,100)) {
        rehashes += 100;
        if (timeInMilliseconds()-start > ms) break;
    }
    return rehashes;
}

/* This function performs just a step of rehashing, and only if there are
 * no safe iterators bound to our hash table. When we have iterators in the
 * middle of a rehashing we can't mess with the two hash tables otherwise
 * some element can be missed or duplicated.
 *
 * This function is called by common lookup or update operations in the
 * dictionary so that the hash table automatically migrates from H1 to H2
 * while it is actively used.
 * 渐进式rehash，每次执行一步长
 * 将rehash的计算量分摊到每次的增删查改操作中,以减少集中式rehash带来的庞大计算量
 * */
static void _dictRehashStep(dict *d) {
    // 当没有迭代器时，进行重定位
    if (d->iterators == 0) dictRehash(d,1);
}

/* Add an element to the target hash table 添加一个dict 元素 */
int dictAdd(dict *d, void *key, void *val)
{
    dictEntry *entry = dictAddRaw(d,key,NULL);      //  会判断key对应的元素是否存在，返回NUll表示已存在

    if (!entry) return DICT_ERR;
    dictSetVal(d, entry, val);                      //  调用宏定义进行赋值
    return DICT_OK;
}

/* Low level add or find:
 * This function adds the entry but instead of setting a value returns the
 * dictEntry structure to the user, that will make sure to fill the value
 * field as he wishes.
 *
 * This function is also directly exposed to the user API to be called
 * mainly in order to store non-pointers inside the hash value, example:
 *
 * entry = dictAddRaw(dict,mykey,NULL);
 * if (entry != NULL) dictSetSignedIntegerVal(entry,1000);
 *
 * Return values:
 *
 * If key already exists NULL is returned, and "*existing" is populated
 * with the existing entry if existing is not NULL.
 *
 * If key was added, the hash entry is returned to be manipulated by the caller.
 * 判断新的元素是否已存在，若存在返回NULL，否则插入哈希表
 */
dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing)
{
    long index;
    dictEntry *entry;
    dictht *ht;

    if (dictIsRehashing(d)) _dictRehashStep(d);     // 如果哈希表处于更新状态，则更新一个元素

    /* Get the index of the new element, or -1 if
     * the element already exists.
     * 获取新元素的下标，如果返回-1，则表示已存在
     * */
    if ((index = _dictKeyIndex(d, key, dictHashKey(d,key), existing)) == -1)
        return NULL;

    /* Allocate the memory and store the new entry.
     * Insert the element in top, with the assumption that in a database
     * system it is more likely that recently added entries are accessed
     * more frequently.
     * // 如果处于rehash状态，则在ht[1]中添加元素，否则在旧表进行添加，以保证rehash过程中ht[0]的长度只减不增
     * */
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    entry = zmalloc(sizeof(*entry));                         // 为新的dictEntry分配存储空间
    entry->next = ht->table[index];                           // 头插法
    ht->table[index] = entry;                                 //
    ht->used++;                                               // 已使用数量++

    /* Set the hash entry fields. */
    //设置key值 key值有可能会调用用户的type函数，所以特地用一个函数包装起来
    dictSetKey(d, entry, key);
    return entry;                                            // 返回更新后的元素
}

/* Add or Overwrite:
 * Add an element, discarding the old value if the key already exists.
 * Return 1 if the key was added from scratch, 0 if there was already an
 * element with such key and dictReplace() just performed a value update
 * operation.
 * 替换一个元素，若已存在则替换，否则添加
 * 返回1则表示更新元素,0表示添加到哈希表中，设置新值后需要释放旧值
 * */
int dictReplace(dict *d, void *key, void *val)
{
    dictEntry *entry, *existing, auxentry;

    /* Try to add the element. If the key
     * does not exists dictAdd will succeed.
     * 判断是否存在一个新的元素，若已存在返回null，否则添加哈希表中
     * */
    entry = dictAddRaw(d,key,&existing);
    if (entry) {
        dictSetVal(d, entry, val);      // 更新val 值
        return 1;
    }

    /* Set the new value and free the old one. Note that it is important
     * to do that in this order, as the value may just be exactly the same
     * as the previous one. In this context, think to reference counting,
     * you want to increment (set), and then decrement (free), and not the
     * reverse.
     * 设置新值，释放旧值
     * */
    auxentry = *existing;                // 暂存旧值
    dictSetVal(d, existing, val);       // 设置新值
    dictFreeVal(d, &auxentry);          // 释放旧值
    return 0;
}

/* Add or Find:
 * dictAddOrFind() is simply a version of dictAddRaw() that always
 * returns the hash entry of the specified key, even if the key already
 * exists and can't be added (in that case the entry of the already
 * existing key is returned.)
 *
 * See dictAddRaw() for more information.
 * 同dictAddRaw ，返回的都是最后的dictEntry
 * */
dictEntry *dictAddOrFind(dict *d, void *key) {
    dictEntry *entry, *existing;
    entry = dictAddRaw(d,key,&existing);
    return entry ? entry : existing;
}

/* Search and remove an element. This is an helper function for
 * dictDelete() and dictUnlink(), please check the top comment
 * of those functions.
 * 删除指定key的结点，可控制是否调用释放方法
 * nofree 为0时释放，为1时则不释放
 * */
static dictEntry *dictGenericDelete(dict *d, const void *key, int nofree) {
    uint64_t h, idx;
    dictEntry *he, *prevHe;
    int table;

    if (d->ht[0].used == 0 && d->ht[1].used == 0) return NULL;                  // 如果旧表和新表都为空

    if (dictIsRehashing(d)) _dictRehashStep(d);                                // 如果处于rehash状态，进行单步更新
    h = dictHashKey(d, key);                                                    // 获取hash值

    for (table = 0; table <= 1; table++) {
        idx = h & d->ht[table].sizemask;                                         // 获取元素的坐标
        he = d->ht[table].table[idx];                                            // 得到该key的第一个元素
        prevHe = NULL;                                                          //  用于存储该key的前一个元素
        while(he) {
            if (key==he->key || dictCompareKeys(d, key, he->key)) {            // 如果key值相同则进行删除
                /* Unlink the element from the list */
                /* Unlink 该元素*/
                if (prevHe)
                    prevHe->next = he->next;
                else
                    d->ht[table].table[idx] = he->next;
                if (!nofree) {
                    dictFreeKey(d, he);                                         // 释放key 和val值
                    dictFreeVal(d, he);
                    zfree(he);                                                   // 释放存储空间
                }
                d->ht[table].used--;                                             // 更新已使用值
                return he;
            }
            prevHe = he;
            he = he->next;                                                        //  遍历下一个元素
        }
        if (!dictIsRehashing(d)) break;                                        // /*如果没有正在rehash则不用查找第二张表*/
    }
    return NULL; /* not found */
}

/* Remove an element, returning DICT_OK on success or DICT_ERR if the
 * element was not found.
 * 删除指定元素，并释放相应的该元素
 * */
int dictDelete(dict *ht, const void *key) {
    return dictGenericDelete(ht,key,0) ? DICT_OK : DICT_ERR;
}

/* Remove an element from the table, but without actually releasing
 * the key, value and dictionary entry. The dictionary entry is returned
 * if the element was found (and unlinked from the table), and the user
 * should later call `dictFreeUnlinkedEntry()` with it in order to release it.
 * Otherwise if the key is not found, NULL is returned.
 *
 * 可以与dictFreeUnlinkedEntry结合：先获取并使用该元素，稍后再释放
 * 如果没有这个方式，则需要先调用dictFind 查找该元素，再调用dictDelete删除该元素，相当于执行了两次查找
 * This function is useful when we want to remove something from the hash
 * table but want to use its value before actually deleting the entry.
 * Without this function the pattern would require two lookups:
 *
 *  entry = dictFind(...);
 *  // Do something with entry
 *  dictDelete(dictionary,entry);
 *
 * Thanks to this function it is possible to avoid this, and use
 * instead:
 *
 * entry = dictUnlink(dictionary,entry);
 * // Do something with entry
 * dictFreeUnlinkedEntry(entry); // <- This does not need to lookup again.
 * 返回指定key对应的dictEntry ，并从字典中删除，但不释放该元素
 */
dictEntry *dictUnlink(dict *ht, const void *key) {
    return dictGenericDelete(ht,key,1);
}

/* You need to call this function to really free the entry after a call
 * to dictUnlink(). It's safe to call this function with 'he' = NULL.
 * 调用dictUnlink() 之后再执行dictFreeUnlinkedEntry 释放该元素
 * 即使dictEntry *he 为null，也是安全的
 * */
void dictFreeUnlinkedEntry(dict *d, dictEntry *he) {
    if (he == NULL) return;
    dictFreeKey(d, he);             // 执行dictType定义的key 析构函数
    dictFreeVal(d, he);             // 执行dictType定义的value 析构函数
    zfree(he);                       // 释放该元素
}

/* Destroy an entire dictionary 销毁已存在的哈希表*/
/* 逐个释放表已存在的节点，最后释放表结构并重置表 */
int _dictClear(dict *d, dictht *ht, void(callback)(void *)) {
    unsigned long i;

    /* Free all the elements 释放所有元素*/
    for (i = 0; i < ht->size && ht->used > 0; i++) {                    // 先判断哈希表是否存在已使用的元素
        dictEntry *he, *nextHe;

        if (callback && (i & 65535) == 0) callback(d->privdata);        // 回调函数

        if ((he = ht->table[i]) == NULL) continue;                      // 当
        while(he) {                                                     // 依次释放节点
            nextHe = he->next;
            dictFreeKey(d, he);
            dictFreeVal(d, he);
            zfree(he);
            ht->used--;
            he = nextHe;
        }
    }
    /* Free the table and the allocated cache structure 释放已存在的哈希表空间 */
    zfree(ht->table);
    /* Re-initialize the table 重置表*/
    _dictReset(ht);
    return DICT_OK; /* never fails */
}

/* Clear & Release the hash table 清空两张表，最后释放字典*/
/* dictRelease是清空两张表并释放字典；dictEmpty 是清空两张表*/
void dictRelease(dict *d)
{
    _dictClear(d,&d->ht[0],NULL);
    _dictClear(d,&d->ht[1],NULL);
    zfree(d);
}

/* 查找指定key对应的dictEntry */
dictEntry *dictFind(dict *d, const void *key)
{
    dictEntry *he;
    uint64_t h, idx, table;

    if (d->ht[0].used + d->ht[1].used == 0) return NULL; /* dict is empty 如果字典为空*/
    if (dictIsRehashing(d)) _dictRehashStep(d);         /* 如果处于rehash过程中，则执行单步更新*/
    h = dictHashKey(d, key);                             /* 计算hash值 */
    for (table = 0; table <= 1; table++) {                /* 遍历两张表 */
        idx = h & d->ht[table].sizemask;                  /* 获取元素的下标 */
        he = d->ht[table].table[idx];                     /* 获取 该位置的第一个元素 */
        while(he) {
            if (key==he->key || dictCompareKeys(d, key, he->key)) /* 如果找到，则返回该元素，否则查找下一个 */
                return he;
            he = he->next;
        }
        if (!dictIsRehashing(d)) return NULL;            /*如果没有正在rehash则不用查找第二张表*/
    }
    return NULL;
}

/* 获取目标key对应的value值 */
void *dictFetchValue(dict *d, const void *key) {
    dictEntry *he;

    he = dictFind(d,key);
    return he ? dictGetVal(he) : NULL;
}

/* A fingerprint is a 64 bit number that represents the state of the dictionary
 * at a given time, it's just a few dict properties xored together.
 * When an unsafe iterator is initialized, we get the dict fingerprint, and check
 * the fingerprint again when the iterator is released.
 * If the two fingerprints are different it means that the user of the iterator
 * performed forbidden operations against the dictionary while iterating
 * 通过指纹来禁止每个不安全的哈希迭代器的非法操作，每个不安全迭代器只能有一个指纹
 * 计算并保存指纹信息
 * . */
long long dictFingerprint(dict *d) {
    long long integers[6], hash = 0;
    int j;

    integers[0] = (long) d->ht[0].table;
    integers[1] = d->ht[0].size;
    integers[2] = d->ht[0].used;
    integers[3] = (long) d->ht[1].table;
    integers[4] = d->ht[1].size;
    integers[5] = d->ht[1].used;

    /* We hash N integers by summing every successive integer with the integer
     * hashing of the previous sum. Basically:
     *
     * Result = hash(hash(hash(int1)+int2)+int3) ...
     *
     * This way the same set of integers in a different order will (likely) hash
     * to a different number. */
    for (j = 0; j < 6; j++) {
        hash += integers[j];
        /* For the hashing step we use Tomas Wang's 64 bit integer hash. */
        hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
    }
    return hash;
}

/* 获取迭代器，默认不安全的*/
dictIterator *dictGetIterator(dict *d)
{
    dictIterator *iter = zmalloc(sizeof(*iter));                /* 为迭代器分配存储空间*/

    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;
    return iter;
}

/* 获取安全的迭代器 */
dictIterator *dictGetSafeIterator(dict *d) {
    dictIterator *i = dictGetIterator(d);                       /* 获取迭代器 */
    // 设置安全的迭代器
    i->safe = 1;
    return i;
}

/*
 * redis 字典中安全和非安全迭代器不允许边rehash边遍历的
 * redis 另一种高级遍历方式scan遍历则允许边rehash边迭代
 * */

/* 获取字典中的下一个节点 ？？？？*/
dictEntry *dictNext(dictIterator *iter)
{
    /*
     *  死循环内，entry 为NULL时，迭代器要么初次执行，要么迭代到桶的最后节点处，如果是后者，判断是否整个字典全部迭代结束，没有的话取下一个桶
     *  如果字典尚未处于rehash状态，自增iterators 属性的操作会禁止后续节点操作触发rehash，
     *  如果已处于rehash过程中，当ht[0]迭代结束，再去迭代迭代器工作前已被转移到ht[1]的那些节点，
     *  如果是安全迭代器，iterators 自增后，后续节点就不会触发rehash迁移节点，所以不会重复迭代数据
     * */
    while (1) {
        // 迭代器初次工作时，其对应的entry必为null
        if (iter->entry == NULL) {
            // 获取迭代器中d 字段保存的字典
            dictht *ht = &iter->d->ht[iter->table];     // ht表示数据表，iter->table 的0/1表示 新旧表
            if (iter->index == -1 && iter->table == 0) {
                if (iter->safe)
                    // 如果是安全的迭代器，字典的iterators 字段自增，禁止渐进式rehash 操作，iterators!=0
                    iter->d->iterators++;
                else
                    // 如果是不安全的迭代器，计算并保存指纹信息
                    iter->fingerprint = dictFingerprint(iter->d);
            }
            // 迭代器开始工作，指向 下一个桶
            iter->index++;
            // 如果index >= size ，即最后一个桶迭代结束
            if (iter->index >= (long) ht->size) {
                if (dictIsRehashing(iter->d) && iter->table == 0) {
                    // 执行rehash 且ht[0] 已经遍历结束
                    // 继续遍历另一个Table
                    iter->table++;
                    // index 置为 0
                    iter->index = 0;
                    // 执行hashTable[1]
                    ht = &iter->d->ht[1];
                } else {
                    break;
                }
            }
            // 根据index 取出节点
            iter->entry = ht->table[iter->index];
        } else {
            // 如果entry 不为NULL，尝试遍历其后续节点
            iter->entry = iter->nextEntry;
        }
        if (iter->entry) {
            // 如果 next 节点不为NULL，记录nextEntry节点的值，返回该节点
            /* We need to save the 'next' here, the iterator user
             * may delete the entry we are returning. */
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }
    return NULL;
}

/* 释放迭代器 */
void dictReleaseIterator(dictIterator *iter)
{
    /*
     * 释放迭代器时，如果是安全的迭代器，自减iterators
     * 不安全的迭代器会重新计算指纹并与迭代器开始工作时计算的指纹比较，并通过assert判断指纹是否一致
     * 不一致说明在不安全的迭代器中执行了修改字典结构的方法，程序保持并退出
     * */
    if (!(iter->index == -1 && iter->table == 0)) {
        if (iter->safe)
            iter->d->iterators--;
        else
            assert(iter->fingerprint == dictFingerprint(iter->d));
    }
    zfree(iter);
}

/* Return a random entry from the hash table. Useful to
 * implement randomized algorithms
 * 返回一个随机的dictEntry，对实现随机算法有用 */
dictEntry *dictGetRandomKey(dict *d)
{
    /*先随机找一个非空桶，然后从该桶随机返回一个元素 */
    dictEntry *he, *orighe;
    unsigned long h;
    int listlen, listele;

    if (dictSize(d) == 0) return NULL;                                          /* 字典元素个数为0 ，*/
    if (dictIsRehashing(d)) _dictRehashStep(d);                                 /* 处于rehash状态时，执行渐进式rehash*/
    /* 如果正在rehash，说明一部分键在ht[0],一部分键在ht[1]上，需要考虑两张表被均匀获取的可能性，就要保证从两个哈希表中均匀分配随机种子*/
    if (dictIsRehashing(d)) {
        do {
            /* We are sure there are no elements in indexes from 0
             * to rehashidx-1 随机数向2个哈希表的总数求余运算,确保哈希值不在0到rehashidex-1 范围内*/
            h = d->rehashidx + (random() % (d->ht[0].size +
                                            d->ht[1].size -
                                            d->rehashidx));                     //计算随机哈希值，这个哈希值一定是在rehashidx的后部
            he = (h >= d->ht[0].size) ? d->ht[1].table[h - d->ht[0].size] :     // 根据上面计算的哈希值拿到对应的bucket
                                      d->ht[0].table[h];
        } while(he == NULL);
    } else {        // 没有进行hash操作，说明所有的键值都在ht[0]上
        do {
            h = random() & d->ht[0].sizemask;                   /* 通过对sizemask的按位与运算计算哈希值 */
            he = d->ht[0].table[h];                             /* 找到哈希表上第h个bucket*/
        } while(he == NULL);
    }

    /* Now we found a non empty bucket, but it is a linked
     * list and we need to get a random element from the list.
     * The only sane way to do so is counting the elements and
     * select a random index. */
    // 现在我们得到了一个不为空的bucket，
    // 而这个bucket的后面还挂接了一个或多个dictEntry（链地址法解决哈希冲突），
    // 所以同样需要计算一个随机索引，来判断究竟访问哪一个dickEntry链表结点
    listlen = 0;
    orighe = he;
    while(he) {
        he = he->next;
        listlen++;                                              // 计算链表长度
    }
    listele = random() % listlen;                               // 随机数对链表长度取余，确定获取哪一个结点
    he = orighe;
    while(listele--) he = he->next;                             // 从前到后遍历这个bucket上的链表，找到这个结点，并最终返回该节点
    return he;
}

/* This function samples the dictionary to return a few keys from random
 * locations.
 * 此方法随机返回指定数目的keys
 *
 * It does not guarantee to return all the keys specified in 'count', nor
 * it does guarantee to return non-duplicated elements, however it will make
 * some effort to do both things.
 *
 * Returned pointers to hash table entries are stored into 'des' that
 * points to an array of dictEntry pointers. The array must have room for
 * at least 'count' elements, that is the argument we pass to the function
 * to tell how many random elements we need.
 *
 * The function returns the number of items stored into 'des', that may
 * be less than 'count' if the hash table has less than 'count' elements
 * inside, or if not enough elements were found in a reasonable amount of
 * steps.
 *
 * Note that this function is not suitable when you need a good distribution
 * of the returned items, but only when you need to "sample" a given number
 * of continuous elements to run some kind of algorithm or to produce
 * statistics. However the function is much faster than dictGetRandomKey()
 * at producing N elements. */
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count) {
    unsigned long j; /* internal hash table id, 0 or 1. */
    unsigned long tables; /* 1 or 2 tables? */
    unsigned long stored = 0, maxsizemask;
    unsigned long maxsteps;

    if (dictSize(d) < count) count = dictSize(d);
    maxsteps = count*10;

    /* Try to do a rehashing work proportional to 'count'. */
    for (j = 0; j < count; j++) {
        if (dictIsRehashing(d))
            _dictRehashStep(d);
        else
            break;
    }

    tables = dictIsRehashing(d) ? 2 : 1;
    maxsizemask = d->ht[0].sizemask;
    if (tables > 1 && maxsizemask < d->ht[1].sizemask)
        maxsizemask = d->ht[1].sizemask;

    /* Pick a random point inside the larger table. */
    unsigned long i = random() & maxsizemask;
    unsigned long emptylen = 0; /* Continuous empty entries so far. */
    while(stored < count && maxsteps--) {
        for (j = 0; j < tables; j++) {
            /* Invariant of the dict.c rehashing: up to the indexes already
             * visited in ht[0] during the rehashing, there are no populated
             * buckets, so we can skip ht[0] for indexes between 0 and idx-1. */
            if (tables == 2 && j == 0 && i < (unsigned long) d->rehashidx) {
                /* Moreover, if we are currently out of range in the second
                 * table, there will be no elements in both tables up to
                 * the current rehashing index, so we jump if possible.
                 * (this happens when going from big to small table). */
                if (i >= d->ht[1].size)
                    i = d->rehashidx;
                else
                    continue;
            }
            if (i >= d->ht[j].size) continue; /* Out of range for this table. */
            dictEntry *he = d->ht[j].table[i];

            /* Count contiguous empty buckets, and jump to other
             * locations if they reach 'count' (with a minimum of 5). */
            if (he == NULL) {
                emptylen++;
                if (emptylen >= 5 && emptylen > count) {
                    i = random() & maxsizemask;
                    emptylen = 0;
                }
            } else {
                emptylen = 0;
                while (he) {
                    /* Collect all the elements of the buckets found non
                     * empty while iterating. */
                    *des = he;
                    des++;
                    he = he->next;
                    stored++;
                    if (stored == count) return stored;
                }
            }
        }
        i = (i+1) & maxsizemask;
    }
    return stored;
}

/* Function to reverse bits. Algorithm from:
 * http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel */
/* 反转位算法 ，*/
static unsigned long rev(unsigned long v) {
    unsigned long s = 8 * sizeof(v); // bit size; must be power of 2
    unsigned long mask = ~0;
    while ((s >>= 1) > 0) {
        mask ^= (mask << s);
        v = ((v >> s) & mask) | ((v << s) & ~mask);
    }
    return v;
}

/* dictScan() is used to iterate over the elements of a dictionary.
 *
 * Iterating works the following way:
 *
 * 1) Initially you call the function using a cursor (v) value of 0.
 * 2) The function performs one step of the iteration, and returns the
 *    new cursor value you must use in the next call.
 * 3) When the returned cursor is 0, the iteration is complete.
 *
 * The function guarantees all elements present in the
 * dictionary get returned between the start and end of the iteration.
 * However it is possible some elements get returned multiple times.
 *
 * For every element returned, the callback argument 'fn' is
 * called with 'privdata' as first argument and the dictionary entry
 * 'de' as second argument.
 *
 * HOW IT WORKS.
 *
 * The iteration algorithm was designed by Pieter Noordhuis.
 * The main idea is to increment a cursor starting from the higher order
 * bits. That is, instead of incrementing the cursor normally, the bits
 * of the cursor are reversed, then the cursor is incremented, and finally
 * the bits are reversed again.
 *
 * This strategy is needed because the hash table may be resized between
 * iteration calls.
 *
 * dict.c hash tables are always power of two in size, and they
 * use chaining, so the position of an element in a given table is given
 * by computing the bitwise AND between Hash(key) and SIZE-1
 * (where SIZE-1 is always the mask that is equivalent to taking the rest
 *  of the division between the Hash of the key and SIZE).
 *
 * For example if the current hash table size is 16, the mask is
 * (in binary) 1111. The position of a key in the hash table will always be
 * the last four bits of the hash output, and so forth.
 *
 * WHAT HAPPENS IF THE TABLE CHANGES IN SIZE?
 *
 * If the hash table grows, elements can go anywhere in one multiple of
 * the old bucket: for example let's say we already iterated with
 * a 4 bit cursor 1100 (the mask is 1111 because hash table size = 16).
 *
 * If the hash table will be resized to 64 elements, then the new mask will
 * be 111111. The new buckets you obtain by substituting in ??1100
 * with either 0 or 1 can be targeted only by keys we already visited
 * when scanning the bucket 1100 in the smaller hash table.
 *
 * By iterating the higher bits first, because of the inverted counter, the
 * cursor does not need to restart if the table size gets bigger. It will
 * continue iterating using cursors without '1100' at the end, and also
 * without any other combination of the final 4 bits already explored.
 *
 * Similarly when the table size shrinks over time, for example going from
 * 16 to 8, if a combination of the lower three bits (the mask for size 8
 * is 111) were already completely explored, it would not be visited again
 * because we are sure we tried, for example, both 0111 and 1111 (all the
 * variations of the higher bit) so we don't need to test it again.
 *
 * WAIT... YOU HAVE *TWO* TABLES DURING REHASHING!
 *
 * Yes, this is true, but we always iterate the smaller table first, then
 * we test all the expansions of the current cursor into the larger
 * table. For example if the current cursor is 101 and we also have a
 * larger table of size 16, we also test (0)101 and (1)101 inside the larger
 * table. This reduces the problem back to having only one table, where
 * the larger one, if it exists, is just an expansion of the smaller one.
 *
 * LIMITATIONS
 *
 * This iterator is completely stateless, and this is a huge advantage,
 * including no additional memory used.
 *
 * The disadvantages resulting from this design are:
 *
 * 1) It is possible we return elements more than once. However this is usually
 *    easy to deal with in the application level.
 * 2) The iterator must return multiple elements per call, as it needs to always
 *    return all the keys chained in a given bucket, and all the expansions, so
 *    we are sure we don't miss keys moving during rehashing.
 * 3) The reverse cursor is somewhat hard to understand at first, but this
 *    comment is supposed to help.
 */
unsigned long dictScan(dict *d,
                       unsigned long v,
                       dictScanFunction *fn,                            // 字典扫描方法
                       dictScanBucketFunction* bucketfn,                // 字典桶扫描方法
                       void *privdata)
{
    dictht *t0, *t1;
    const dictEntry *de, *next;
    unsigned long m0, m1;

    if (dictSize(d) == 0) return 0;

    if (!dictIsRehashing(d)) {                                   /*如果没有正在rehash，直接遍历*/
        t0 = &(d->ht[0]);
        m0 = t0->sizemask;

        /* Emit entries at cursor */
        if (bucketfn) bucketfn(privdata, &t0->table[v & m0]);
        // 将hash表中cursor索引（table[cursor]）指向的链表（dictEntry）都遍历一遍
        de = t0->table[v & m0];
        while (de) {
            next = de->next;
            fn(privdata, de);                                       // 调用用户的提供的fn 函数，需要对键值进行的操作
            de = next;
        }

        /* Set unmasked bits so incrementing the reversed cursor
         * operates on the masked bits */
        v |= ~m0;

        /* Increment the reverse cursor */
        v = rev(v);
        v++;
        v = rev(v);

    } else {
        t0 = &d->ht[0];
        t1 = &d->ht[1];

        /* Make sure t0 is the smaller and t1 is the bigger table */
        if (t0->size > t1->size) {
            t0 = &d->ht[1];
            t1 = &d->ht[0];
        }

        m0 = t0->sizemask;
        m1 = t1->sizemask;

        /* Emit entries at cursor */
        if (bucketfn) bucketfn(privdata, &t0->table[v & m0]);
        de = t0->table[v & m0];
        while (de) {
            next = de->next;
            fn(privdata, de);
            de = next;
        }

        /* Iterate over indices in larger table that are the expansion
         * of the index pointed to by the cursor in the smaller table */
        do {
            /* Emit entries at cursor */
            if (bucketfn) bucketfn(privdata, &t1->table[v & m1]);
            de = t1->table[v & m1];
            while (de) {
                next = de->next;
                fn(privdata, de);
                de = next;
            }

            /* Increment the reverse cursor not covered by the smaller mask.*/
            v |= ~m1;
            v = rev(v);
            v++;
            v = rev(v);

            /* Continue while bits covered by mask difference is non-zero */
        } while (v & (m0 ^ m1));
    }

    return v;
}

/* ------------------------- private functions ------------------------------ */

/* Expand the hash table if needed */
/* 扩容算法：当元素个数：字典超过1:1时或者元素个数/桶>dict_force_resize_ratio(默认为5)时，进行扩容*/
static int _dictExpandIfNeeded(dict *d)
{
    /* Incremental rehashing already in progress. Return. 正处于rehash过程中*/
    if (dictIsRehashing(d)) return DICT_OK;

    /* If the hash table is empty expand it to the initial size. 如果哈希表为空，扩容大小为初始大小*/
    if (d->ht[0].size == 0) return dictExpand(d, DICT_HT_INITIAL_SIZE);

    /* If we reached the 1:1 ratio, and we are allowed to resize the hash
     * table (global setting) or we should avoid it but the ratio between
     * elements/buckets is over the "safe" threshold, we resize doubling
     * the number of buckets. */
    /* 如果比例used/size达到1:1，并且可以调整字典大小时
     * 或者当元素个数/存储桶超过dict_force_resize_ratio时执行扩容操作*/
    if (d->ht[0].used >= d->ht[0].size &&
        (dict_can_resize ||
         d->ht[0].used/d->ht[0].size > dict_force_resize_ratio))
    {
        return dictExpand(d, d->ht[0].used*2);              /* 扩容操作*/
    }
    return DICT_OK;
}

/* Our hash table capability is a power of two
 * 哈希表的容量是2的倍数
 * 新的哈希容量是原来的2倍
 * */
static unsigned long _dictNextPower(unsigned long size)
{
    unsigned long i = DICT_HT_INITIAL_SIZE;     // 哈希表的初始容量

    if (size >= LONG_MAX) return LONG_MAX + 1LU;
    while(1) {
        if (i >= size)
            return i;
        i *= 2;
    }
}

/* Returns the index of a free slot that can be populated with
 * a hash entry for the given 'key'.
 * If the key already exists, -1 is returned
 * and the optional output parameter may be filled.
 *
 * Note that if we are in the process of rehashing the hash table, the
 * index is always returned in the context of the second (new) hash table.
 * 返回可填充的空槽索引；返回-1表示该key已存在，
 * 如果正在执行哈希操作，索引总是在第二张表上进行，因为rehash过程中只在第2张表上添加元素
 * */
static long _dictKeyIndex(dict *d, const void *key, uint64_t hash, dictEntry **existing)
{
    unsigned long idx, table;
    dictEntry *he;
    if (existing) *existing = NULL;

    /* Expand the hash table if needed */
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;
    for (table = 0; table <= 1; table++) {
        idx = hash & d->ht[table].sizemask;
        /* Search if this slot does not already contain the given key */
        he = d->ht[table].table[idx];
        while(he) {
            if (key==he->key || dictCompareKeys(d, key, he->key)) {
                if (existing) *existing = he;
                return -1;
            }
            he = he->next;
        }
        if (!dictIsRehashing(d)) break;                     /*如果没有正在rehash则不用查找第二张表*/
    }
    return idx;
}

/*  清空整个字典，即清空里面的2张哈希表 */
/* dictRelease是清空两张表并释放字典；dictEmpty 是清空两张表*/
void dictEmpty(dict *d, void(callback)(void*)) {
    _dictClear(d,&d->ht[0],callback);
    _dictClear(d,&d->ht[1],callback);
    d->rehashidx = -1;
    d->iterators = 0;
}

/* 允许resize操作 */
void dictEnableResize(void) {
    dict_can_resize = 1;
}

/* 不允许resize操作 */
void dictDisableResize(void) {
    dict_can_resize = 0;
}

/* 计算hash值  */
uint64_t dictGetHash(dict *d, const void *key) {
    return dictHashKey(d, key);
}

/* Finds the dictEntry reference by using pointer and pre-calculated hash.
 * oldkey is a dead pointer and should not be accessed.
 * the hash value should be provided using dictGetHash.
 * no string / key comparison is performed.
 * return value is the reference to the dictEntry if found, or NULL if not found.
 * 根据指针和hash值查找dictEntry
* */
dictEntry **dictFindEntryRefByPtrAndHash(dict *d, const void *oldptr, uint64_t hash) {
    dictEntry *he, **heref;
    unsigned long idx, table;

    if (d->ht[0].used + d->ht[1].used == 0) return NULL; /* dict is empty */
    for (table = 0; table <= 1; table++) {
        idx = hash & d->ht[table].sizemask;
        heref = &d->ht[table].table[idx];
        he = *heref;
        while(he) {
            if (oldptr==he->key)
                return heref;
            heref = &he->next;
            he = *heref;
        }
        /* 如果没有正在rehash则不用查找第二张表*/
        if (!dictIsRehashing(d)) return NULL;
    }
    return NULL;
}

/* ------------------------------- Debugging ---------------------------------*/

#define DICT_STATS_VECTLEN 50
size_t _dictGetStatsHt(char *buf, size_t bufsize, dictht *ht, int tableid) {
    unsigned long i, slots = 0, chainlen, maxchainlen = 0;
    unsigned long totchainlen = 0;
    unsigned long clvector[DICT_STATS_VECTLEN];
    size_t l = 0;

    if (ht->used == 0) {
        return snprintf(buf,bufsize,
            "No stats available for empty dictionaries\n");
    }

    /* Compute stats. */
    for (i = 0; i < DICT_STATS_VECTLEN; i++) clvector[i] = 0;
    for (i = 0; i < ht->size; i++) {
        dictEntry *he;

        if (ht->table[i] == NULL) {
            clvector[0]++;
            continue;
        }
        slots++;
        /* For each hash entry on this slot... */
        chainlen = 0;
        he = ht->table[i];
        while(he) {
            chainlen++;
            he = he->next;
        }
        clvector[(chainlen < DICT_STATS_VECTLEN) ? chainlen : (DICT_STATS_VECTLEN-1)]++;
        if (chainlen > maxchainlen) maxchainlen = chainlen;
        totchainlen += chainlen;
    }

    /* Generate human readable stats. */
    l += snprintf(buf+l,bufsize-l,
        "Hash table %d stats (%s):\n"
        " table size: %ld\n"
        " number of elements: %ld\n"
        " different slots: %ld\n"
        " max chain length: %ld\n"
        " avg chain length (counted): %.02f\n"
        " avg chain length (computed): %.02f\n"
        " Chain length distribution:\n",
        tableid, (tableid == 0) ? "main hash table" : "rehashing target",
        ht->size, ht->used, slots, maxchainlen,
        (float)totchainlen/slots, (float)ht->used/slots);

    for (i = 0; i < DICT_STATS_VECTLEN-1; i++) {
        if (clvector[i] == 0) continue;
        if (l >= bufsize) break;
        l += snprintf(buf+l,bufsize-l,
            "   %s%ld: %ld (%.02f%%)\n",
            (i == DICT_STATS_VECTLEN-1)?">= ":"",
            i, clvector[i], ((float)clvector[i]/ht->size)*100);
    }

    /* Unlike snprintf(), teturn the number of characters actually written. */
    if (bufsize) buf[bufsize-1] = '\0';
    return strlen(buf);
}

void dictGetStats(char *buf, size_t bufsize, dict *d) {
    size_t l;
    char *orig_buf = buf;
    size_t orig_bufsize = bufsize;

    l = _dictGetStatsHt(buf,bufsize,&d->ht[0],0);
    buf += l;
    bufsize -= l;
    if (dictIsRehashing(d) && bufsize > 0) {
        _dictGetStatsHt(buf,bufsize,&d->ht[1],1);
    }
    /* Make sure there is a NULL term at the end. */
    if (orig_bufsize) orig_buf[orig_bufsize-1] = '\0';
}

/* ------------------------------- Benchmark ---------------------------------*/

#ifdef DICT_BENCHMARK_MAIN

#include "sds.h"

uint64_t hashCallback(const void *key) {
    return dictGenHashFunction((unsigned char*)key, sdslen((char*)key));
}

int compareCallback(void *privdata, const void *key1, const void *key2) {
    int l1,l2;
    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

void freeCallback(void *privdata, void *val) {
    DICT_NOTUSED(privdata);

    sdsfree(val);
}

dictType BenchmarkDictType = {
    hashCallback,
    NULL,
    NULL,
    compareCallback,
    freeCallback,
    NULL
};

#define start_benchmark() start = timeInMilliseconds()
#define end_benchmark(msg) do { \
    elapsed = timeInMilliseconds()-start; \
    printf(msg ": %ld items in %lld ms\n", count, elapsed); \
} while(0);

/* dict-benchmark [count] */
int main(int argc, char **argv) {
    long j;
    long long start, elapsed;
    dict *dict = dictCreate(&BenchmarkDictType,NULL);
    long count = 0;

    if (argc == 2) {
        count = strtol(argv[1],NULL,10);
    } else {
        count = 5000000;
    }

    start_benchmark();
    for (j = 0; j < count; j++) {
        int retval = dictAdd(dict,sdsfromlonglong(j),(void*)j);
        assert(retval == DICT_OK);
    }
    end_benchmark("Inserting");
    assert((long)dictSize(dict) == count);

    /* Wait for rehashing. */
    while (dictIsRehashing(dict)) {
        dictRehashMilliseconds(dict,100);
    }

    start_benchmark();
    for (j = 0; j < count; j++) {
        sds key = sdsfromlonglong(j);
        dictEntry *de = dictFind(dict,key);
        assert(de != NULL);
        sdsfree(key);
    }
    end_benchmark("Linear access of existing elements");

    start_benchmark();
    for (j = 0; j < count; j++) {
        sds key = sdsfromlonglong(j);
        dictEntry *de = dictFind(dict,key);
        assert(de != NULL);
        sdsfree(key);
    }
    end_benchmark("Linear access of existing elements (2nd round)");

    start_benchmark();
    for (j = 0; j < count; j++) {
        sds key = sdsfromlonglong(rand() % count);
        dictEntry *de = dictFind(dict,key);
        assert(de != NULL);
        sdsfree(key);
    }
    end_benchmark("Random access of existing elements");

    start_benchmark();
    for (j = 0; j < count; j++) {
        sds key = sdsfromlonglong(rand() % count);
        key[0] = 'X';
        dictEntry *de = dictFind(dict,key);
        assert(de == NULL);
        sdsfree(key);
    }
    end_benchmark("Accessing missing");

    start_benchmark();
    for (j = 0; j < count; j++) {
        sds key = sdsfromlonglong(j);
        int retval = dictDelete(dict,key);
        assert(retval == DICT_OK);
        key[0] += 17; /* Change first number to letter. */
        retval = dictAdd(dict,key,(void*)j);
        assert(retval == DICT_OK);
    }
    end_benchmark("Removing and adding");
}
#endif
