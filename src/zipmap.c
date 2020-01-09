/* String -> String Map data structure optimized for size.
 * This file implements a data structure mapping strings to other strings
 * implementing an O(n) lookup data structure designed to be very memory
 * efficient.
 *
 *
 * The Redis Hash type uses this data structure for hashes composed of a small
 * number of elements, to switch to a hash table once a given number of
 * elements is reached.
 *
 *
 * Given that many times Redis Hashes are used to represent objects composed
 * of few fields, this is a very big win in terms of used memory.
 *
 * zipmap是为了节省内存空间的一种结构（哈希结构：字符串->字符串映射结构）
 * 支持O(n)的查找效率并具有良好的内存效率
 *
 * Redis内置的Hash 类型在保存少量数据时，使用zipmap来保存键值对，当数量达到给定值时，才会采用哈希表来保存，可以节省内存
 *
 * Redis的Hash 结构通常会保存有少量字段的对象，可以很大程度上节省内存空间
 * --------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

/* Memory layout of a zipmap, for the map "foo" => "bar", "hello" => "world":
 *
 * <zmlen><len>"foo"<len><free>"bar"<len>"hello"<len><free>"world"
 *
 * <zmlen> is 1 byte length that holds the current size of the zipmap.
 * When the zipmap length is greater than or equal to 254, this value
 * is not used and the zipmap needs to be traversed to find out the length.
 * zmlen 用1个字节的长度表示zipmap的元素个数，最多可表示253，大于等于254时，就不起作用了，
 * 只能通过逐个遍历的方式获取元素个数,这一点和ziplist是不一样的
 * <len> is the length of the following string (key or value).
 * <len> lengths are encoded in a single value or in a 5 bytes value.
 * If the first byte value (as an unsigned 8 bit value) is between 0 and
 * 253, it's a single-byte length. If it is 254 then a four bytes unsigned
 * integer follows (in the host byte ordering). A value of 255 is used to
 * signal the end of the hash.
 * len 代表后面字符串（key或value）的值的长度，一般被编码1个或5个字节表示，这一点和ziplist类似，
 * 如果在0-253之间，单字节表示，超过254的话用5字节表示，第一个字节是254，接下来的4个字节表示实际的长度
 * <free> is the number of free unused bytes after the string, resulting
 * from modification of values associated to a key. For instance if "foo"
 * is set to "bar", and later "foo" will be set to "hi", it will have a
 * free byte to use if the value will enlarge again later, or even in
 * order to add a key/value pair if it fits.
 *
 *
 * <free> is always an unsigned 8 bit number, because if after an
 * update operation there are more than a few free bytes, the zipmap will be
 * reallocated to make sure it is as small as possible.
 *
 * free一般用来表示后面的value长度的空闲字节数，一般都比较小，用1个字节保存free
 * 如果有较多的空闲空间，zipmap会进行调整使得map整体尽可能小一些
 *  zipmap也存在一个结尾符号，占1个字节，用0xff表示
 * The most compact representation of the above two elements hash is actually:
 *
 * "\x02\x03foo\x03\x00bar\x05hello\x05\x00world\xff"
 *
 * Note that because keys and values are prefixed length "objects",
 * the lookup will take O(N) where N is the number of elements
 * in the zipmap and *not* the number of bytes needed to represent the zipmap.
 * This lowers the constant times considerably.
 */

#include <stdio.h>
#include <string.h>
#include "zmalloc.h"
#include "endianconv.h"

#define ZIPMAP_BIGLEN 254   // zipmap的元素个数超过253时的标识符
#define ZIPMAP_END 255      // 结束符

/* The following defines the max value for the <free> field described in the
 * comments above, that is, the max number of trailing bytes in a value. */
 // free 字段的最大值，即value后面的最大空闲字节数
#define ZIPMAP_VALUE_MAX_FREE 4

/* The following macro returns the number of bytes needed to encode the length
 * for the integer value _l, that is, 1 byte for lengths < ZIPMAP_BIGLEN and
 * 5 bytes for all the other lengths.
 * 用来确定len字段所占用的字节数
 * */
#define ZIPMAP_LEN_BYTES(_l) (((_l) < ZIPMAP_BIGLEN) ? 1 : sizeof(unsigned int)+1)

/* Create a new empty zipmap.
 * 创建一个空的zipmap */
unsigned char *zipmapNew(void) {
    // 初始化时只有两个字节，第一个字节表示zipmap保存的key-value对的个数，第2个字节是结尾符号
    unsigned char *zm = zmalloc(2);

    zm[0] = 0; /* Length 当前保存的键值对个数为0 */
    zm[1] = ZIPMAP_END; // 结束符
    return zm;
}

/* Decode the encoded length pointed by 'p' */
/* 获取len字段的数值（即随后字符串的长度）
 *     查看第一个字节的数值，如果很小（小于254），直接返回
 *     否则读取接下来4个字节的内容所表示的数值（5个字节表示的话，第一个字节是254）
 *  */
static unsigned int zipmapDecodeLength(unsigned char *p) {
    unsigned int len = *p;

    if (len < ZIPMAP_BIGLEN) return len;
    memcpy(&len,p+1,sizeof(unsigned int));
    memrev32ifbe(&len);     // 统一转换为小端模式表示
    return len;
}

/* Encode the length 'l' writing it in 'p'. If p is NULL it just returns
 * the amount of bytes required to encode such a length.
 * 将长度len 编码到p指针所指向的内存空间即保存长度len所需要的空间
 * */
static unsigned int zipmapEncodeLength(unsigned char *p, unsigned int len) {
    if (p == NULL) {
        return ZIPMAP_LEN_BYTES(len);
    } else {
        if (len < ZIPMAP_BIGLEN) {
            // 小于254，仅用1字节表示
            p[0] = len;
            return 1;
        } else {
            // 大于等于254，第一个字节设置为254，后4个字节是实际表示的长度值
            p[0] = ZIPMAP_BIGLEN;
            memcpy(p+1,&len,sizeof(len));
            memrev32ifbe(p+1);
            return 1+sizeof(len);
        }
    }
}

/* Search for a matching key, returning a pointer to the entry inside the
 * zipmap. Returns NULL if the key is not found.
 *
 * If NULL is returned, and totlen is not NULL, it is set to the entire
 * size of the zimap, so that the calling function will be able to
 * reallocate the original zipmap to make room for more entries.
 * 按关键字key查找zipmap，如果totlen不为NULL，函数返回后存放zipmap占用的字节数
 * */
static unsigned char *zipmapLookupRaw(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned int *totlen) {
    // zipmap的第一个字节表示zmlen字段，zm+1就是跳过该字段
    unsigned char *p = zm+1, *k = NULL;
    unsigned int l,llen;

    // 从前往后遍历
    while(*p != ZIPMAP_END) {
        unsigned char free;

        /* Match or skip the key
         * 确定key字符串的长度 */
        l = zipmapDecodeLength(p);
        // 确定保存key字符串长度所需要的字节数，就是len字段所需要的字节数
        llen = zipmapEncodeLength(NULL,l);
        // 判断当前key是不是和指定的key匹配
        if (key != NULL && k == NULL && l == klen && !memcmp(p+llen,key,l)) {
            /* Only return when the user doesn't care
             * for the total length of the zipmap. */
            /* 如果totlen为NULL，表示不关心zipmap占用的字节数，此时直接返回p */
            if (totlen != NULL) {
                k = p;
            } else {
                return p;
            }
        }
        // p移动len+l，指向保存value长度的属性value length处
        p += llen+l;
        /* Skip the value as well 确定value字符串的长度 */
        l = zipmapDecodeLength(p);
        /* 确定保存value字符串长度所需要的字节数，即len字段的字节数*/
        p += zipmapEncodeLength(NULL,l);  // 指向free字段处
        // 获取free字段的值
        free = p[0];
        // 跳到下一个key节点，free占一个字节，+1是为了跳过free字段，free为value值后面的空闲字节数
        p += l+1+free; /* +1 to skip the free byte */
    }
    // 遍历整个zipmap后，得到其占用的字节数
    if (totlen != NULL) *totlen = (unsigned int)(p-zm)+1;
    return k;
}

// 保存一个由长度为klen的key和长度为vlen的value组成的键值对所需要的字节数
static unsigned long zipmapRequiredLength(unsigned int klen, unsigned int vlen) {
    unsigned int l;

    l = klen+vlen+3;
    if (klen >= ZIPMAP_BIGLEN) l += 4; // 长度超过254个字节，保存表示key长度的klen需要5个字节
    if (vlen >= ZIPMAP_BIGLEN) l += 4; // 同上
    return l;
}

/* Return the total amount used by a key (encoded length + payload) */
/* 获取某个key节点占用的字节数，即len字段和key字符串的长度 */
static unsigned int zipmapRawKeyLength(unsigned char *p) {
    // 获取key字符串的长度
    unsigned int l = zipmapDecodeLength(p);
    // 加上保存key字符串长度所需要字节数
    // l是key字符串的长度，zipmapEncodeLength()是获取保存l所需要的字节数
    return zipmapEncodeLength(NULL,l) + l;
}

/* Return the total amount used by a value
 * (encoded length + single byte free count + payload)
 * 返回某个value节点占用的字节数.包括value结点实际的字符串 ，value字符串长度的编码长度以及free字段
 * */
static unsigned int zipmapRawValueLength(unsigned char *p) {
    // 获取value节点的长度
    unsigned int l = zipmapDecodeLength(p);
    unsigned int used;

    // 获取保存value节点长度所需要的字节数
    used = zipmapEncodeLength(NULL,l);
    // p[used]为free字段的值，即空闲空间的大小
    // +数字1 为保存free字段所需要的1个字节
    // l（字母L）是value节点的长度
    used += p[used] + 1 + l;
    return used;
}

/* If 'p' points to a key, this function returns the total amount of
 * bytes used to store this entry (entry = key + associated value + trailing
 * free space if any).
 * 如果P指向key，就返回存储这个键值对所需要的字节数 ，包括key节点长度和value节点长度
 * */
static unsigned int zipmapRawEntryLength(unsigned char *p) {
    unsigned int l = zipmapRawKeyLength(p);     // key节点长度
    return l + zipmapRawValueLength(p+l);       // value 长度
}

// 重新调整zipmap的大小
static inline unsigned char *zipmapResize(unsigned char *zm, unsigned int len) {
    // 重新分配空间
    zm = zrealloc(zm, len);
    // 设置结束符号
    zm[len-1] = ZIPMAP_END;
    return zm;
}

/* Set key to value, creating the key if it does not already exist.
 * If 'update' is not NULL, *update is set to 1 if the key was
 * already preset, otherwise to 0.
 * 如果存在key，则更新对象的value值，如果不存在，就增加一个键值对<key,value>,
 * 参数update可用来区分更新和添加操作
 * */
unsigned char *zipmapSet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char *val, unsigned int vlen, int *update) {
    unsigned int zmlen, offset;
    // 保存长度分别为klen和vlen的key和value组成的键值对需要的空间
    unsigned int freelen, reqlen = zipmapRequiredLength(klen,vlen);
    unsigned int empty, vempty;
    unsigned char *p;

    freelen = reqlen;
    if (update) *update = 0;
    //查找是否存在key对应的键值对，zmlen会保存zipmap所占据的字节数
    p = zipmapLookupRaw(zm,key,klen,&zmlen);
    if (p == NULL) {
        /* Key not found: enlarge 没找到*/
        zm = zipmapResize(zm, zmlen+reqlen);    // 对zipmap扩容 ，新的空间大小为zmlen+reqlen
        p = zm+zmlen-1; // 此时p指向原来zipmap的末尾，新的键值对在这个地方添加
        zmlen = zmlen+reqlen;  // 更新zipmap所占用的空间大小

        /* Increase zipmap length (this is an insert) */
        if (zm[0] < ZIPMAP_BIGLEN) zm[0]++;  // 更新zmlen字段，即保存zipmap的元素个数
    } else {
        // 如果找到key对应的键值对，需要判断是否有足够的空间容纳新值
        /* Key found. Is there enough space for the new value? */
        /* Compute the total length: */
        if (update) *update = 1;
        // 原先键值对节点的空间大小
        freelen = zipmapRawEntryLength(p);
        if (freelen < reqlen) {
            /* Store the offset of this key within the current zipmap, so
             * it can be resized. Then, move the tail backwards so this
             * pair fits at the current position.
             * 如果旧节点的空间不足没，需要扩容，所以要记录更新的键值对的位置*/
            offset = p-zm;      // 旧节点的偏移量
            zm = zipmapResize(zm, zmlen-freelen+reqlen); // 调整大小
            p = zm+offset;      // p指向更新的键值对的位置

            /* The +1 in the number of bytes to be moved is caused by the
             * end-of-zipmap byte. Note: the *original* zmlen is used.
             * 移动待更新节点以后的元素，以确保由足够的空间容纳新值*/
            // p+reqlen, p+freelen分别表示旧节点后的那个元素的更新前后的位置
            // zmlen-(offset+freelen+1)为需要移动的字节数，
                    // （1） zmlen为原先zipmap的字节数
                    // （2） offset 是旧节点的偏移量
                    // （3） freelen是旧节点的空间大小
                    // （4） +1 是需要移动结束符号
            memmove(p+reqlen, p+freelen, zmlen-(offset+freelen+1));
            zmlen = zmlen-freelen+reqlen;   // 更新zipmap所占用的空间大小
            freelen = reqlen;               // 更新后的键值对组成的新节点所需要的空间大小
        }
    }

    /* We now have a suitable block where the key/value entry can
     * be written. If there is too much free space, move the tail
     * of the zipmap a few bytes to the front and shrink the zipmap,
     * as we want zipmaps to be very space efficient.
     * freelen表示剩余空间大小 reqlen为插入或更新后的键值对所需要的空间，两者之差就是free字段的新值
     * 如果该值过大zipmap会进行调整 */
    empty = freelen-reqlen;
    if (empty >= ZIPMAP_VALUE_MAX_FREE) {
        /* First, move the tail <empty> bytes to the front, then resize
         * the zipmap to be <empty> bytes smaller. */
        offset = p-zm;
        memmove(p+reqlen, p+freelen, zmlen-(offset+freelen+1));
        zmlen -= empty;
        zm = zipmapResize(zm, zmlen);
        p = zm+offset;
        vempty = 0;
    } else {
        vempty = empty;
    }

    /* Just write the key + value and we are done.
     * 下面是将key和value写入指定的位置*/
    /* Key: */
    p += zipmapEncodeLength(p,klen); // 将key的长度编码写入zipmap中
    memcpy(p,key,klen);              // 将key 字符串写入
    p += klen;                      // 编码应该就是指十进制数据转换成二进制数据254->1111 1110
    /* Value: 写入value的字符串和长度编码*/
    p += zipmapEncodeLength(p,vlen);
    *p++ = vempty;      // 写入free 字段
    memcpy(p,val,vlen);
    return zm;
}

/* Remove the specified key. If 'deleted' is not NULL the pointed integer is
 * set to 0 if the key was not found, to 1 if it was found and deleted. */
// 根据key删除指定的键值对
unsigned char *zipmapDel(unsigned char *zm, unsigned char *key, unsigned int klen, int *deleted) {
    unsigned int zmlen, freelen;
    // 判断指定的键值对是否存在于zipmap
    unsigned char *p = zipmapLookupRaw(zm,key,klen,&zmlen);
    if (p) {
        freelen = zipmapRawEntryLength(p);  // 获取指定键值对所占据的空间
        memmove(p, p+freelen, zmlen-((p-zm)+freelen+1));    // 将该键值对之后的数据前移
        zm = zipmapResize(zm, zmlen-freelen);               // 调整zipmap的大小

        /* Decrease zipmap length */
        if (zm[0] < ZIPMAP_BIGLEN) zm[0]--;              // 更新zipmap的长度字段

        if (deleted) *deleted = 1;                          // 表示删除成功
    } else {
        if (deleted) *deleted = 0;                          // 表示删除失败
    }
    return zm;
}

/* Call before iterating through elements via zipmapNext() */
// zipmapNext 迭代器函数，返回值指向zipmap第一个键值对的首地址，zipmap的第一个字节是zmlen，表示长度
unsigned char *zipmapRewind(unsigned char *zm) {
    return zm+1;
}

/* This function is used to iterate through all the zipmap elements.
 * In the first call the first argument is the pointer to the zipmap + 1.
 * In the next calls what zipmapNext returns is used as first argument.
 * Example:
 *  zipmap的迭代器方式遍历，用法示例：
 * unsigned char *i = zipmapRewind(my_zipmap);      // 指向zipmap的第一个键值对的首地址
 * while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) { // 指向下一个键值对的首地址，判断是否为NULL
 *     printf("%d bytes key at $p\n", klen, key);
 *     printf("%d bytes value at $p\n", vlen, value);
 * }
 * 迭代器
 */
unsigned char *zipmapNext(unsigned char *zm, unsigned char **key, unsigned int *klen, unsigned char **value, unsigned int *vlen) {
    if (zm[0] == ZIPMAP_END) return NULL;       // 指向结束符
    if (key) {
        *key = zm;
        *klen = zipmapDecodeLength(zm);             // 获取key结点的长度
        *key += ZIPMAP_LEN_BYTES(*klen);          // 跳过key结点长度编码，相当于指向key结点实际的字符串
    }
    zm += zipmapRawKeyLength(zm);                   // 跳过key结点
    if (value) {                                    // 相当于跳过value 结点
        *value = zm+1;                              // +1 是为了跳过free字段
        *vlen = zipmapDecodeLength(zm);             // 获取value结点的长度
        *value += ZIPMAP_LEN_BYTES(*vlen);        // 跳过value结点长度编码，相当于指向value结点实际的字符串
    }
    zm += zipmapRawValueLength(zm);                  // 跳过value结点
    return zm;
}

/* Search a key and retrieve the pointer and len of the associated value.
 * If the key is found the function returns 1, otherwise 0.
 * 根据key查找相应的value值，实际是对zipmapLookupRaw的封装 */
int zipmapGet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char **value, unsigned int *vlen) {
    unsigned char *p;

    // 判断某key结点是否存在,则指向key结点的首地址
    if ((p = zipmapLookupRaw(zm,key,klen,NULL)) == NULL) return 0;
    p += zipmapRawKeyLength(p); //
    *vlen = zipmapDecodeLength(p);
    *value = p + ZIPMAP_LEN_BYTES(*vlen) + 1;
    return 1;
}

/* Return 1 if the key exists, otherwise 0 is returned. 判断某个key是否存在 */
int zipmapExists(unsigned char *zm, unsigned char *key, unsigned int klen) {
    return zipmapLookupRaw(zm,key,klen,NULL) != NULL;
}

/* Return the number of entries inside a zipmap
 * 返回zipmap的键值对个数,小于254时，zmlen表示键值对个数，大于等于254时需要逐个遍历来统计*/
unsigned int zipmapLen(unsigned char *zm) {
    unsigned int len = 0;
    if (zm[0] < ZIPMAP_BIGLEN) {
        len = zm[0];    // 长度小于254，直接返回
    } else {
        unsigned char *p = zipmapRewind(zm);    // 获取zipmap的第一个键值对首地址
        while((p = zipmapNext(p,NULL,NULL,NULL,NULL)) != NULL) len++;   // 逐个遍历

        /* Re-store length if small enough */
        if (len < ZIPMAP_BIGLEN) zm[0] = len;
    }
    return len;
}

/* Return the raw size in bytes of a zipmap, so that we can serialize
 * the zipmap on disk (or everywhere is needed) just writing the returned
 * amount of bytes of the C array starting at the zipmap pointer. */
// 获取整个zipmap占用的字节数，
size_t zipmapBlobLen(unsigned char *zm) {
    unsigned int totlen;
    zipmapLookupRaw(zm,NULL,0,&totlen);
    return totlen;
}

#ifdef REDIS_TEST
static void zipmapRepr(unsigned char *p) {
    unsigned int l;

    printf("{status %u}",*p++);
    while(1) {
        if (p[0] == ZIPMAP_END) {
            printf("{end}");
            break;
        } else {
            unsigned char e;

            l = zipmapDecodeLength(p);
            printf("{key %u}",l);
            p += zipmapEncodeLength(NULL,l);
            if (l != 0 && fwrite(p,l,1,stdout) == 0) perror("fwrite");
            p += l;

            l = zipmapDecodeLength(p);
            printf("{value %u}",l);
            p += zipmapEncodeLength(NULL,l);
            e = *p++;
            if (l != 0 && fwrite(p,l,1,stdout) == 0) perror("fwrite");
            p += l+e;
            if (e) {
                printf("[");
                while(e--) printf(".");
                printf("]");
            }
        }
    }
    printf("\n");
}

#define UNUSED(x) (void)(x)
int zipmapTest(int argc, char *argv[]) {
    unsigned char *zm;

    UNUSED(argc);
    UNUSED(argv);

    zm = zipmapNew();

    zm = zipmapSet(zm,(unsigned char*) "name",4, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "surname",7, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "age",3, (unsigned char*) "foo",3,NULL);
    zipmapRepr(zm);

    zm = zipmapSet(zm,(unsigned char*) "hello",5, (unsigned char*) "world!",6,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "bar",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "!",1,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "12345",5,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "new",3, (unsigned char*) "xx",2,NULL);
    zm = zipmapSet(zm,(unsigned char*) "noval",5, (unsigned char*) "",0,NULL);
    zipmapRepr(zm);
    zm = zipmapDel(zm,(unsigned char*) "new",3,NULL);
    zipmapRepr(zm);

    printf("\nLook up large key:\n");
    {
        unsigned char buf[512];
        unsigned char *value;
        unsigned int vlen, i;
        for (i = 0; i < 512; i++) buf[i] = 'a';

        zm = zipmapSet(zm,buf,512,(unsigned char*) "long",4,NULL);
        if (zipmapGet(zm,buf,512,&value,&vlen)) {
            printf("  <long key> is associated to the %d bytes value: %.*s\n",
                vlen, vlen, value);
        }
    }

    printf("\nPerform a direct lookup:\n");
    {
        unsigned char *value;
        unsigned int vlen;

        if (zipmapGet(zm,(unsigned char*) "foo",3,&value,&vlen)) {
            printf("  foo is associated to the %d bytes value: %.*s\n",
                vlen, vlen, value);
        }
    }
    printf("\nIterate through elements:\n");
    {
        unsigned char *i = zipmapRewind(zm);
        unsigned char *key, *value;
        unsigned int klen, vlen;

        while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) {
            printf("  %d:%.*s => %d:%.*s\n", klen, klen, key, vlen, vlen, value);
        }
    }
    return 0;
}
#endif
