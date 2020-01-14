/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * 一个简单的事件驱动的编程库，程序库封装了网络模型：evport、epoll、kqueue、select
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
 *
 * AE 事件循环器：主要用于处理一些事件，
 */

#ifndef __AE_H__
#define __AE_H__

#include <time.h>

#define AE_OK 0
#define AE_ERR -1

// 文件事件码
#define AE_NONE 0       /* No events registered. 没有注册事件*/
#define AE_READABLE 1   /* Fire when descriptor is readable. 可读取时触发*/
#define AE_WRITABLE 2   /* Fire when descriptor is writable. 可写入时触发*/

/*
 * 对于可写，如果在同一事件循环迭代中已触发可读事件，则永远不要触发该事件。
 * 当您希望在发送回复之前将内容保留到磁盘上并且希望以组的方式进行操作时很有用。
 * 就是读的时候不要改动
 */
#define AE_BARRIER 4    /* With WRITABLE, never fire the event if the
                           READABLE event already fired in the same event
                           loop iteration. Useful when you want to persist
                           things to disk before sending replies, and want
                           to do that in a group fashion. */

#define AE_FILE_EVENTS 1        // 文件相关事件
#define AE_TIME_EVENTS 2        // 时间相关事件
#define AE_ALL_EVENTS (AE_FILE_EVENTS|AE_TIME_EVENTS)   // ALL包括文件和时间相关事件
#define AE_DONT_WAIT 4                                      // 能不sleep就不sleep，如果没有定时事件，会一直sleep直到IO事件发生
#define AE_CALL_AFTER_SLEEP 8

#define AE_NOMORE -1
#define AE_DELETED_EVENT_ID -1

/* Macros */
#define AE_NOTUSED(V) ((void) V)

struct aeEventLoop;

/* Types and data structures */
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);
typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *clientData);
typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);

/* File event structure 文件事件,主要是用于网络IO事件,触发后会生成事件结构aeFiredEvent，用于后续处理*/
typedef struct aeFileEvent {
    int mask; /* one of AE_(READABLE|WRITABLE|BARRIER) */
    aeFileProc *rfileProc;      // 读函数
    aeFileProc *wfileProc;      // 写函数
    void *clientData;          // 客户端数据
} aeFileEvent;

/* Time event structure 时间事件，主要用于一些后台事件*/
typedef struct aeTimeEvent {
    long long id;                           /* 事件Id ,递增的 time event identifier. */
    long when_sec;                          /* 执行事件的时间(秒数) seconds */
    long when_ms;                           /* 执行事件的时间(毫秒数) milliseconds */
    aeTimeProc *timeProc;                   // 处理的函数
    aeEventFinalizerProc *finalizerProc;    // 时间事件从链表中移除时执行的函数，非必须
    void *clientData;                       // 客户端传入的数据
    struct aeTimeEvent *prev;               // 前一个时间事件的指针 ，时间事件用的是链表，文件事件用的是数组
    struct aeTimeEvent *next;               // 下一个时间事件的指针
} aeTimeEvent;

/* A fired event aeFileEvent事件触发后会创建事件结构aeFiredEvent，用于后续处理 */
typedef struct aeFiredEvent {
    int fd;     // 描述符
    int mask;
} aeFiredEvent; // 就绪事件

/* State of an event based program */
// ae 事件的总结构体
typedef struct aeEventLoop {
    int maxfd;                          /* 当前注册的最高的fd .highest file descriptor currently registered */
    int setsize;                        /* 允许的fd的最大数量 max number of file descriptors tracked */
    long long timeEventNextId;          //
    time_t lastTime;                    /* Used to detect system clock skew */
    aeFileEvent *events;                /* 注册的事件数组指针 Registered events 注册的事件组*/
    aeFiredEvent *fired;                /* 待处理的事件数组指针Fired events 触发的事件组 */
    aeTimeEvent *timeEventHead;         // 时间事件链表头指针
    int stop;                           // 事件的开关
    void *apidata;                      /* 多路复用库的私有数据 This is used for polling API specific data */
    aeBeforeSleepProc *beforesleep;     // 事件执行前的前置函数
    aeBeforeSleepProc *aftersleep;      // 事件执行后的后置函数
} aeEventLoop;

/* Prototypes */
// Redis通过将对应事件注册到eventloop中，然后不断循环检测有无事件触发
// 目前Redis支持超时事件和网络IO读写事件的注册

// 主要负责eventloop结构的创建和初始化以及模型的初始化
aeEventLoop *aeCreateEventLoop(int setsize);
// 释放event loop
void aeDeleteEventLoop(aeEventLoop *eventLoop);
// 停止事件分配
void aeStop(aeEventLoop *eventLoop);
// 创建监听事件
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData);
// 删除文件事件
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask);
// 获取fd文件事件对应的类型(读或写)
int aeGetFileEvents(aeEventLoop *eventLoop, int fd);
// 创建时间事件
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc);
// 删除时间事件
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id);
// 分配事件
int aeProcessEvents(aeEventLoop *eventLoop, int flags);
// 等待milliseconds直到fd对应的事件可读或可写
int aeWait(int fd, int mask, long long milliseconds);
// ae事件轮询主函数
void aeMain(aeEventLoop *eventLoop);
// 获取当前网络模型
char *aeGetApiName(void);
// 事件执行前的回调函数
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep);
// 事件执行前的回调函数
void aeSetAfterSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *aftersleep);
// 获取eventloop的事件个数
int aeGetSetSize(aeEventLoop *eventLoop);
// 重置eventloop 事件个数
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize);

#endif
