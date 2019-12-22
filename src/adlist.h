/* adlist.h - A generic doubly linked list implementation
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
#ifndef __ADLIST_H__
#define __ADLIST_H__

/* Node, List, and Iterator are the only data structures used currently. */
// 定义listNode节点
typedef struct listNode {
    // 前一节点
    struct listNode *prev;
    // 后一节点
    struct listNode *next;
    // node的值，此处是指函数指针
    void *value;
} listNode;

// list 迭代器，单向
typedef struct listIter {
    // 当前迭代器的下一个元素
    listNode *next;
    // 迭代器的方向
    int direction;
} listIter;

// 定义list列表
typedef struct list {
    // list列表的头节点
    listNode *head;
    // list列表的尾节点
    listNode *tail;

    // dup、free、match 均用于回调使用
    // 复制函数指针
    void *(*dup)(void *ptr);
    // 释放函数指针
    void (*free)(void *ptr);
    // 匹配函数指针
    int (*match)(void *ptr, void *key);
    // list列表的长度
    unsigned long len;
} list;

/* Functions implemented as macros */
// 宏定义的一些操作
#define listLength(l) ((l)->len)    // list长度
#define listFirst(l) ((l)->head)    // 获取list 头部
#define listLast(l) ((l)->tail)     // 获取list 尾部
#define listPrevNode(n) ((n)->prev)     // 获取某节点的上一个节点
#define listNextNode(n) ((n)->next)     // 获取某节点下一个节点
#define listNodeValue(n) ((n)->value)   // 获取某节点的值，对应的是个函数指针

// 分别设置和获取dup、free、match
#define listSetDupMethod(l,m) ((l)->dup = (m))      // list的复制函数的设置
#define listSetFreeMethod(l,m) ((l)->free = (m))    // list的释放函数的设置
#define listSetMatchMethod(l,m) ((l)->match = (m))  // list的匹配函数的设置

#define listGetDupMethod(l) ((l)->dup)              // list的复制函数的获取
#define listGetFree(l) ((l)->free)                  // list的释放函数的获取
#define listGetMatchMethod(l) ((l)->match)          // list的释放函数的获取

/* Prototypes */
// 方法原型
list *listCreate(void);                                                             //创建一个list列表
void listRelease(list *list);                                                       // 释放一个list
void listEmpty(list *list);                                                         // 将list置空
list *listAddNodeHead(list *list, void *value);                                     // 在list头部添加节点
list *listAddNodeTail(list *list, void *value);                                     // 在list尾部添加节点
list *listInsertNode(list *list, listNode *old_node, void *value, int after);       // 在list某个节点后面插入节点
void listDelNode(list *list, listNode *node);                                       // 删除指定节点
listIter *listGetIterator(list *list, int direction);                               // 获取list的迭代器
listNode *listNext(listIter *iter);                                                 // 利用list迭代器获取的下一个节点
void listReleaseIterator(listIter *iter);                                           // 释放list迭代器
list *listDup(list *orig);                                                          // 列表的复制
listNode *listSearchKey(list *list, void *key);                                     // 在list列表中查找某个值对应的节点
listNode *listIndex(list *list, long index);                                        // 获取list列表中某个位置的节点
void listRewind(list *list, listIter *li);                                          // 重置迭代器方向从头部开始
void listRewindTail(list *list, listIter *li);                                      // 重置迭代器方向从尾部开始
void listRotate(list *list);                                                        // list 翻转
void listJoin(list *l, list *o);                                                    // 将o 列表对应的节点插入到l 列表中

/* Directions for iterators */
// 宏定义 迭代器的方向 0：从头部向尾部，1：从尾部向头部
#define AL_START_HEAD 0
#define AL_START_TAIL 1

#endif /* __ADLIST_H__ */