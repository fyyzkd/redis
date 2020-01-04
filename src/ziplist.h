/*
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#ifndef _ZIPLIST_H
#define _ZIPLIST_H

/* 分别表示ziplist的头尾节点标识 */
#define ZIPLIST_HEAD 0
#define ZIPLIST_TAIL 1

/* 新建一个压缩列表，时间复杂度：O(1) */
unsigned char *ziplistNew(void);
/* 合并两个压缩列表 */
unsigned char *ziplistMerge(unsigned char **first, unsigned char **second);
/* 向ziplist中添加数据，返回的是一个新的ziplist
 * ziplist是一个连续空间，执行追加操作会引发realloc,ziplist的内存位置可能发生变化
 * */
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where);
/* 获取列表某个索引位置的值 */
unsigned char *ziplistIndex(unsigned char *zl, int index);
/* 返回压缩列表当前位置的前一值 */
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p);
/* 返回压缩列表当前位置的下一值 */
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p);
/* 获取列表的信息*/
unsigned int ziplistGet(unsigned char *p, unsigned char **sval, unsigned int *slen, long long *lval);
/* 向列表中插入数据 */
unsigned char *ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen);
/* 删除列表中某个节点 */
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p);
/* 从列表索引为index的位置开始，删除num个节点 */
unsigned char *ziplistDeleteRange(unsigned char *zl, int index, unsigned int num);
/* 列表比较*/
unsigned int ziplistCompare(unsigned char *p, unsigned char *s, unsigned int slen);
/* 压缩列表中查找某个值并返回包含改值的节点
 * skip参数表示查找的时候每次比较要跳过的数据项个数
 * ziplist表示hash 结构时，是按照一个field，一个value依次存储的，即偶数索引的数据项存field，奇数索引的数据项存value
 * 按照field的值进行查找的时候，需要跳过value数据项
 * */
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip);
/* 返回列表包含的节点数 */
unsigned int ziplistLen(unsigned char *zl);
/* 返回压缩列表目前所占用的内存字节数  */
size_t ziplistBlobLen(unsigned char *zl);
/* */
void ziplistRepr(unsigned char *zl);

#ifdef REDIS_TEST
int ziplistTest(int argc, char *argv[]);
#endif

#endif /* _ZIPLIST_H */
