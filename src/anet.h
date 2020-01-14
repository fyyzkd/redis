/* anet.c -- Basic TCP socket stuff made a bit less boring
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
 * 基于简单的基本TCP的socket连接
 * anet 这个网络操作库是基于C语言系统网络库的又一次简单的封装，都是直接调用C库函数实现的
 */

#ifndef ANET_H
#define ANET_H

#include <sys/types.h>

#define ANET_OK 0
#define ANET_ERR -1
#define ANET_ERR_LEN 256

/* Flags used with certain functions. */
#define ANET_NONE 0
#define ANET_IP_ONLY (1<<0)

#if defined(__sun) || defined(_AIX)
#define AF_LOCAL AF_UNIX
#endif

#ifdef _AIX
#undef ip_len
#endif

// TCP的默认连接
int anetTcpConnect(char *err, char *addr, int port);
// TCP 的非阻塞连接
int anetTcpNonBlockConnect(char *err, char *addr, int port);
// 建立非阻塞式绑定TCP连接
int anetTcpNonBlockBindConnect(char *err, char *addr, int port, char *source_addr);
// 建立非阻塞式绑定TCP连接
int anetTcpNonBlockBestEffortBindConnect(char *err, char *addr, int port, char *source_addr);
// 建立unix domain sockets 连接（本地）
int anetUnixConnect(char *err, char *path);
// 建立非阻塞式unix domain sockets 连接（本地）
int anetUnixNonBlockConnect(char *err, char *path);
// anet 网络读取文件到buffer中的操作
int anetRead(int fd, char *buf, int count);
// 获取主机名的IP地址
int anetResolve(char *err, char *host, char *ipbuf, size_t ipbuf_len);
// 仅解析IP的地址
int anetResolveIP(char *err, char *host, char *ipbuf, size_t ipbuf_len);
// 创建监听socket，并调用bind和listen 启动服务器开始监听端口
int anetTcpServer(char *err, int port, char *bindaddr, int backlog);
int anetTcp6Server(char *err, int port, char *bindaddr, int backlog);
int anetUnixServer(char *err, char *path, mode_t perm, int backlog);
// 调用accept，接受客户端的连接，返回ip和端口号
int anetTcpAccept(char *err, int serversock, char *ip, size_t ip_len, int *port);
// unix domain socket 等待连接
int anetUnixAccept(char *err, int serversock);
//  通过anet网络从buffer中写入文件的操作
int anetWrite(int fd, char *buf, int count);
// anet 设置非阻塞的方式
int anetNonBlock(char *err, int fd);
// anet 设置阻塞的方式
int anetBlock(char *err, int fd);
// 启用TCP没有延时
int anetEnableTcpNoDelay(char *err, int fd);
// 禁用TCP没有延时
int anetDisableTcpNoDelay(char *err, int fd);
// 设置TCP保持活跃连接状态，使用所有系统
int anetTcpKeepAlive(char *err, int fd);
// 设置超时发送时间限制（0 取消超时机制）
int anetSendTimeout(char *err, int fd, long long ms);
// 获取peer地址信息（ip、端口号、unix socket）
int anetPeerToString(int fd, char *ip, size_t ip_len, int *port);
// 设置TCP连接一直存活，用来检测已经失活的节点，interval只适用于Linux系统
int anetKeepAlive(char *err, int fd, int interval);
// 获取当前socket的ip和端口号
int anetSockName(int fd, char *ip, size_t ip_len, int *port);
int anetFormatAddr(char *fmt, size_t fmt_len, char *ip, int port);
int anetFormatPeer(int fd, char *fmt, size_t fmt_len);
//按格式获取连接名称
int anetFormatSock(int fd, char *fmt, size_t fmt_len);

#endif
