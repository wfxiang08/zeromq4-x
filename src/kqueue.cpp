/*
    Copyright (c) 2007-2013 Contributors as noted in the AUTHORS file

    This file is part of 0MQ.

    0MQ is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    0MQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "kqueue.hpp"


#include <sys/time.h>
#include <sys/types.h>
#include <sys/event.h>
#include <stdlib.h>
#include <unistd.h>
#include <algorithm>
#include <new>

#include "kqueue.hpp"
#include "err.hpp"
#include "config.hpp"
#include "i_poll_events.hpp"
#include "likely.hpp"

//  NetBSD defines (struct kevent).udata as intptr_t, everyone else
//  as void *.
#if defined ZMQ_HAVE_NETBSD
#define kevent_udata_t intptr_t
#else
#define kevent_udata_t void *
#endif

zmq::kqueue_t::kqueue_t() :
        stopping(false) {
    //  Create event queue
    kqueue_fd = kqueue();
    errno_assert (kqueue_fd != -1);
#ifdef HAVE_FORK
    pid = getpid();
#endif
}

zmq::kqueue_t::~kqueue_t() {
    worker.stop();
    close(kqueue_fd);
}


void zmq::kqueue_t::kevent_add(fd_t fd_, short filter_, void *udata_) {
    struct kevent ev;

    EV_SET (&ev, fd_, filter_, EV_ADD, 0, 0, (kevent_udata_t) udata_);
    int rc = kevent(kqueue_fd, &ev, 1, NULL, 0, NULL);
    errno_assert (rc != -1);
}

void zmq::kqueue_t::kevent_delete(fd_t fd_, short filter_) {
    struct kevent ev;

    EV_SET (&ev, fd_, filter_, EV_DELETE, 0, 0, 0);
    int rc = kevent(kqueue_fd, &ev, 1, NULL, 0, NULL);
    errno_assert (rc != -1);
}

//
// 只是创建了一个 handle_t, 并且增加了系统的 load, 并没有做额外的事情，例如: kqueue的注册，删除
//
zmq::kqueue_t::handle_t zmq::kqueue_t::add_fd(fd_t fd_, i_poll_events *reactor_) {
    poll_entry_t *pe = new(std::nothrow) poll_entry_t;
    alloc_assert (pe);

    // 注意区分不同的flag
    pe->fd = fd_;
    pe->flag_pollin = 0;
    pe->flag_pollout = 0;

    // 就是当有读写消息之后，回调函数是怎么样的？
    pe->reactor = reactor_;

    // 增加了一个poll的负载
    adjust_load(1);

    return pe;
}

//
// 删除一个Handle
//
void zmq::kqueue_t::rm_fd(handle_t handle_) {
    poll_entry_t *pe = (poll_entry_t *) handle_;
    
    // 删除fd
    if (pe->flag_pollin)
        kevent_delete(pe->fd, EVFILT_READ);
    if (pe->flag_pollout)
        kevent_delete(pe->fd, EVFILT_WRITE);
    
    // pe保存起来是为了复用?
    // 重置fd
    pe->fd = retired_fd;
    retired.push_back(pe);

    // 调整负载: -1
    adjust_load(-1);
}

//
// 将handle 注册到 kqueue中(以READ的方式注册)
//
void zmq::kqueue_t::set_pollin(handle_t handle_) {
    poll_entry_t *pe = (poll_entry_t *) handle_;
    if (likely (!pe->flag_pollin)) {
        pe->flag_pollin = true;
        kevent_add(pe->fd, EVFILT_READ, pe);
    }
}

//
// 将handle 取消注册到 kqueue中(以READ的方式注册)
//
void zmq::kqueue_t::reset_pollin(handle_t handle_) {
    poll_entry_t *pe = (poll_entry_t *) handle_;
    if (likely (pe->flag_pollin)) {
        pe->flag_pollin = false;
        kevent_delete(pe->fd, EVFILT_READ);
    }
}

//
// 将handle 注册到 kqueue中(以WRITE的方式注册)
//
void zmq::kqueue_t::set_pollout(handle_t handle_) {
    poll_entry_t *pe = (poll_entry_t *) handle_;
    if (likely (!pe->flag_pollout)) {
        pe->flag_pollout = true;
        kevent_add(pe->fd, EVFILT_WRITE, pe);
    }
}

//
// 将handle 取消注册到 kqueue中(以WRITE的方式注册)
//
void zmq::kqueue_t::reset_pollout(handle_t handle_) {
    poll_entry_t *pe = (poll_entry_t *) handle_;
    if (likely (pe->flag_pollout)) {
        pe->flag_pollout = false;
        kevent_delete(pe->fd, EVFILT_WRITE);
    }
}

void zmq::kqueue_t::start() {
    worker.start(worker_routine, this);
}

void zmq::kqueue_t::stop() {
    stopping = true;
}

int zmq::kqueue_t::max_fds() {
    return -1;
}

//
// 如何处理Loop呢?
//
void zmq::kqueue_t::loop() {
    while (!stopping) {

        //  1. Execute any due timers.
        int timeout = (int) execute_timers();

        //  Wait for events.
        struct kevent ev_buf[max_io_events];
        
        // timeout单位为ms
        timespec ts = {timeout / 1000, (timeout % 1000) * 1000000};
        
        // &ev_buf[0] 和 ev_buf 有什么区别呢?
        // 2. 如果有 timeout, 则设置 ts, 如果没有则一直等待(ts是为了保证下一个timer能按时执行)
        int n = kevent(kqueue_fd, NULL, 0, &ev_buf[0], max_io_events, timeout ? &ts : NULL);
        
        // 如果没有有效的Event, 则继续
        if (n == -1) {
            errno_assert (errno == EINTR);
            continue;
        }

        
        // 3. 处理每个 event
        for (int i = 0; i < n; i++) {
            poll_entry_t *pe = (poll_entry_t *) ev_buf[i].udata;

            if (pe->fd == retired_fd)
                continue;
            if (ev_buf[i].flags & EV_EOF)
                pe->reactor->in_event();

            if (pe->fd == retired_fd)
                continue;
            
            // WriteBuffer有空了，可以写数据了
            if (ev_buf[i].filter == EVFILT_WRITE)
                pe->reactor->out_event();

            // 有数据可以读取了
            if (pe->fd == retired_fd)
                continue;
            if (ev_buf[i].filter == EVFILT_READ)
                pe->reactor->in_event();
        }

        // 为什么要集中处理retired呢?
        //  Destroy retired event sources.
        for (retired_t::iterator it = retired.begin(); it != retired.end();
             ++it)
            delete *it;
        retired.clear();
    }
}

void zmq::kqueue_t::worker_routine(void *arg_) {
    ((kqueue_t *) arg_)->loop();
}


