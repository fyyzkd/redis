/* adlist.c - A generic doubly linked list implementation
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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


#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"

/* Create a new list. The created list can be freed with
 * AlFreeList(), but private value of every node need to be freed
 * by the user before to call AlFreeList().
 *
 * On error, NULL is returned. Otherwise the pointer to the new list.
 * 创建一个新的list
 * */
list *listCreate(void)
{
    // 定义一个list结构体
    struct list *list;
    // 为list分配内存，分配失败则直接返回NULL
    // zmalloc 是redis定义的分配内存的函数，对应释放内存的是zfree函数
    if ((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;
    // 初始化操作，头部、尾部均指向NULL，长度为0，复制、释放、匹配函数指针全为NULL
    list->head = list->tail = NULL;
    list->len = 0;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;
    return list;
}

/* Remove all the elements from the list without destroying the list itself. */
// 将list 列表置位空，但不释放list
void listEmpty(list *list)
{
    unsigned long len;
    listNode *current, *next;

    // 获取list长度、头结点
    current = list->head;
    len = list->len;
    // 逐个释放list节点
    while(len--) {
        next = current->next;
        // 设置释放函数指针？？？
        //如果列表有free释放方法定义，每个结点都必须调用自己内部的value方法
        if (list->free) list->free(current->value);
        // 释放当前节点，对应redis 自定义的zmalloc方法
        zfree(current);
        current = next;
    }
    // 最后重新设置头尾结点、长度为0
    list->head = list->tail = NULL;
    list->len = 0;
}

/* Free the whole list.
 * 释放list列表
 * This function can't fail. */
void listRelease(list *list)
{
    listEmpty(list);    // 列表置空，逐个释放节点，但保留指向list 的指针
    zfree(list);        // 释放内存，对应的是redis 自定义的zmalloc方法
}

/* Add a new node to the list, to head, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned.
 * 在list头部添加节点
 * */
list *listAddNodeHead(list *list, void *value)
{
    listNode *node;
    //定义新的listNode，并赋值函数指针
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    node->value = value;
    // 如果当前list长度为0，即头尾节点=null
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    // 节点数++
    list->len++;
    return list;
}

/* Add a new node to the list, to tail, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned.
 * 在list尾部添加节点，同listAddNodeHead
 * */
list *listAddNodeTail(list *list, void *value)
{
    // 新建list 节点
    listNode *node;

    // 分配内存
    // sizeof(*node)是第一个节点,listNode节点多少字节
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    node->value = value;
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }
    list->len++;
    return list;
}

// 在old_node节点的前面或后面添加节点
list *listInsertNode(list *list, listNode *old_node, void *value, int after) {
    // 创建新的节点
    listNode *node;
    // 为新节点分配内存空间
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    // 如果是 C 语言不存在布尔变量，用0和非0替代
    // 在old_node的后面添加节点
    if (after) {
        node->prev = old_node;
        node->next = old_node->next;
        // 如果old_node是尾节点
        if (list->tail == old_node) {
            list->tail = node;
        }
    } else {
        // 在 old_node的前面添加节点
        node->next = old_node;
        node->prev = old_node->prev;
        // 如果old_node是头结点
        if (list->head == old_node) {
            list->head = node;
        }
    }
    // 继续完成node的前后指针未连接的操作
    if (node->prev != NULL) {
        node->prev->next = node;
    }
    if (node->next != NULL) {
        node->next->prev = node;
    }
    // list 长度++
    list->len++;
    return list;
}

/* Remove the specified node from the specified list.
 * It's up to the caller to free the private value of the node.
 *
 * This function can't fail.
 * 删除list指定的节点
 * */
void listDelNode(list *list, listNode *node)
{
    //如果node结点的prev结点存在，prev的结点的下一个节点指向Node的next结点
    if (node->prev)
        node->prev->next = node->next;
    else
        //如果node结点的prev结点不存在，则重新设置头结点node节点的下一个节点
        list->head = node->next;
    //如果node结点的next结点存在，next的结点的上一个节点指向Node的prev结点
    if (node->next)
        node->next->prev = node->prev;
    else    // 否则，重新设置尾节点
        list->tail = node->prev;
    // 如果存在,则调用free
    if (list->free) list->free(node->value);
    zfree(node);    // 释放节点内存空间
    list->len--;    // 长度减一
}

/* Returns a list iterator 'iter'. After the initialization every
 * call to listNext() will return the next element of the list.
 *
 * This function can't fail.
 * 获取list的迭代器
 * */
listIter *listGetIterator(list *list, int direction)
{
    // 声明迭代器
    listIter *iter;

    // 申请内存空间的分配
    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;
    if (direction == AL_START_HEAD) {
        // 如果方向是从头开始，迭代器的next指针指向head
        iter->next = list->head;
    }
    else
        // 方向从尾部开始，迭代器的next指针指向tail
        iter->next = list->tail;
    // 设置迭代器的方向
    iter->direction = direction;
    return iter;
}

/* Release the iterator memory */
void listReleaseIterator(listIter *iter) {
    zfree(iter);        // 释放迭代器的空间
}

/* Create an iterator in the list private iterator structure */
// 重置迭代器的方向，从尾部开始
void listRewindTail(list *list, listIter *li) {
    li->next = list->tail;
    li->direction = AL_START_TAIL;      // 迭代器方向，从尾部开始
}

// 重置迭代器的方向，从头开始
void listRewind(list *list, listIter *li) {
    li->next = list->head;
    li->direction = AL_START_HEAD;      // 迭代器方向 ，从头开始
}

/* Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * listDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the list,
 * or NULL if there are no more elements, so the classical usage patter
 * is:
 * 迭代器的推荐用法
 * iter = listGetIterator(list,<direction>);
 * while ((node = listNext(iter)) != NULL) {
 *     doSomethingWith(listNodeValue(node));
 * }
 *
 * 利用迭代器获取的下一个节点
 * */
listNode *listNext(listIter *iter)
{
    // 获取迭代器的当前元素
    listNode *current = iter->next;

    // 当前节点不为null
    if (current != NULL) {
        if (iter->direction == AL_START_HEAD)
            // 如果迭代器的方向为从头开始，则下一个元素为当前节点的next节点
            iter->next = current->next;
        else
            // 如果迭代器的方向为从尾开始，则下一个元素为当前节点的prev节点
            iter->next = current->prev;
    }
    return current;
}

/* Duplicate the whole list. On out of memory NULL is returned.
 * On success a copy of the original list is returned.
 *
 * The 'Dup' method set with listSetDupMethod() function is used
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 *
 * The original list both on success or error is never modified.
 * 列表复制方法：传入的参数为原始列表
 * */
list *listDup(list *orig)
{
    list *copy;     // 新的copy的list
    listIter iter;  // 迭代器
    listNode *node; // 节点

    // 申请一个新的list
    if ((copy = listCreate()) == NULL)
        return NULL;
    // 设置copy的dup,free,match 函数指针
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;
    //  重置迭代器的迭代方向
    listRewind(orig, &iter);
    // 从前往后逐个遍历节点
    while((node = listNext(&iter)) != NULL) {
        void *value;
        if (copy->dup) {
            // 如果定义了列表复制方法，则调用dup函数
            value = copy->dup(node->value);
            // 如果复制失败
            if (value == NULL) {
                // 释放list内存空间
                listRelease(copy);
                return NULL;
            }
        } else
            // 否则直接复制value
            value = node->value;
        //  在尾部添加节点，
        if (listAddNodeTail(copy, value) == NULL) {
            // 若失败，则释放内存空间
            // listRelease 先将list置为空 ，后释放内存空间
            listRelease(copy);
            return NULL;
        }
    }
    return copy;
}

/* Search the list for a node matching a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned.
 * 根据关键字key 查找list中对应的节点，用到match()
 * */
listNode *listSearchKey(list *list, void *key)
{
    listIter iter;  // 迭代器
    listNode *node; //

    listRewind(list, &iter);  // 重置迭代器，从头开始
    while((node = listNext(&iter)) != NULL) {
        // 逐个遍历节点，
        if (list->match) {
            // 如果定义了match方法，则调用该方法
            if (list->match(node->value, key)) {
                // 如果找到对应节点，则返回
                return node;
            }
        } else {
            // 如果没有定义match方法，则直接比较函数指针
            if (key == node->value) {
                // 若相等，则返回对应节点
                return node;
            }
        }
    }
    return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimate
 * and so on. If the index is out of range NULL is returned.
 * 获取list中下标为index的节点
 * 有两种方式：从头开始和从尾部开始
 * */
listNode *listIndex(list *list, long index) {
    listNode *n;
    if (index < 0) {
        // 从尾部开始
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    } else {
        // 从头部开始 
        n = list->head;
        while(index-- && n) n = n->next;
    }
    return n;
}

/* Rotate the list removing the tail node and inserting it to the head. */
// list 翻转：就是将head 和tail的指针                ???
void listRotate(list *list) {
    listNode *tail = list->tail;

    // 如果list节点长度不超过1，则不进行翻转，adlist.h 中的宏定义
    if (listLength(list) <= 1) return;

    /* Detach current tail */
    // 将原本倒数第二个节点置为尾节点
    list->tail = tail->prev;
    list->tail->next = NULL;
    /* Move it as head */
    // 
    list->head->prev = tail;
    tail->prev = NULL;
    tail->next = list->head;
    list->head = tail;
}

/* Add all the elements of the list 'o' at the end of the
 * list 'l'. The list 'other' remains empty but otherwise valid.
 * 两个list进行连接
 * */
void listJoin(list *l, list *o) {
    if (o->head)
        // 如果o->head 不为null，即o不为空的list，则o->head的prev节点指向l->tail节点
        o->head->prev = l->tail;

    if (l->tail)
        // 如果l->tail不为null，则l->tail的next节点指向o->head节点
        l->tail->next = o->head;
    else
        // 如果l->tail为null，即l为null,则l的head指向o的head节点
        l->head = o->head;

    // 如果o的tail节点不为null，l的tail节点指向o的tail
    if (o->tail) l->tail = o->tail;
    // 节点长度 为两者相加
    l->len += o->len;

    /* Setup other as an empty list. */
    // 将另一个list设置为空的list
    o->head = o->tail = NULL;
    o->len = 0;
}