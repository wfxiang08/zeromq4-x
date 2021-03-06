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
#define ZMQ_TYPE_UNSAFE

#include "platform.hpp"

#if defined ZMQ_FORCE_SELECT
#define ZMQ_POLL_BASED_ON_SELECT
#elif defined ZMQ_FORCE_POLL
#define ZMQ_POLL_BASED_ON_POLL
#elif defined ZMQ_HAVE_LINUX || defined ZMQ_HAVE_FREEBSD || \
    defined ZMQ_HAVE_OPENBSD || defined ZMQ_HAVE_SOLARIS || \
    defined ZMQ_HAVE_OSX || defined ZMQ_HAVE_QNXNTO || \
    defined ZMQ_HAVE_HPUX || defined ZMQ_HAVE_AIX || \
    defined ZMQ_HAVE_NETBSD
#define ZMQ_POLL_BASED_ON_POLL
#elif defined ZMQ_HAVE_WINDOWS || defined ZMQ_HAVE_OPENVMS || \
     defined ZMQ_HAVE_CYGWIN
#define ZMQ_POLL_BASED_ON_SELECT
#endif

//  On AIX platform, poll.h has to be included first to get consistent
//  definition of pollfd structure (AIX uses 'reqevents' and 'retnevents'
//  instead of 'events' and 'revents' and defines macros to map from POSIX-y
//  names to AIX-specific names).
#if defined ZMQ_POLL_BASED_ON_POLL
#include <poll.h>
#endif

// zmq.h must be included *after* poll.h for AIX to build properly
#include "../include/zmq.h"

#if defined ZMQ_HAVE_WINDOWS
#include "windows.hpp"
#else
#endif


// XSI vector I/O
#if defined ZMQ_HAVE_UIO
#include <sys/uio.h>
#else
struct iovec {
    void *iov_base;
    size_t iov_len;
};
#endif


#include <string.h>
#include <stdlib.h>
#include <new>
#include <sys/poll.h>

#include "proxy.hpp"
#include "socket_base.hpp"
#include "ctx.hpp"

//  Compile time check whether msg_t fits into zmq_msg_t.
typedef char check_msg_t_size[sizeof(zmq::msg_t) == sizeof(zmq_msg_t) ? 1 : -1];


void zmq_version(int *major_, int *minor_, int *patch_) {
    *major_ = ZMQ_VERSION_MAJOR;
    *minor_ = ZMQ_VERSION_MINOR;
    *patch_ = ZMQ_VERSION_PATCH;
}


const char *zmq_strerror(int errnum_) {
    return zmq::errno_to_string(errnum_);
}

int zmq_errno(void) {
    return errno;
}


//  New context API
//
// 如何创建一个Context呢?
//
void *zmq_ctx_new(void) {
    //  Create 0MQ context.
    zmq::ctx_t *ctx = new(std::nothrow) zmq::ctx_t;
    alloc_assert (ctx);
    return ctx;
}

//
// 终止 context
//
int zmq_ctx_term(void *ctx_) {
    if (!ctx_ || !((zmq::ctx_t *) ctx_)->check_tag()) {
        errno = EFAULT;
        return -1;
    }

    int rc = ((zmq::ctx_t *) ctx_)->terminate();
    int en = errno;

    //  Shut down only if termination was not interrupted by a signal.
    if (!rc || en != EINTR) {
    }

    errno = en;
    return rc;
}

int zmq_ctx_shutdown(void *ctx_) {
    if (!ctx_ || !((zmq::ctx_t *) ctx_)->check_tag()) {
        errno = EFAULT;
        return -1;
    }

    return ((zmq::ctx_t *) ctx_)->shutdown();
}

int zmq_ctx_set(void *ctx_, int option_, int optval_) {
    if (!ctx_ || !((zmq::ctx_t *) ctx_)->check_tag()) {
        errno = EFAULT;
        return -1;
    }
    return ((zmq::ctx_t *) ctx_)->set(option_, optval_);
}

int zmq_ctx_get(void *ctx_, int option_) {
    if (!ctx_ || !((zmq::ctx_t *) ctx_)->check_tag()) {
        errno = EFAULT;
        return -1;
    }
    return ((zmq::ctx_t *) ctx_)->get(option_);
}

//  Stable/legacy context API
void *zmq_init(int io_threads_) {
    if (io_threads_ >= 0) {
        void *ctx = zmq_ctx_new();
        // 设置io_threads_
        zmq_ctx_set(ctx, ZMQ_IO_THREADS, io_threads_);
        return ctx;
    }
    errno = EINVAL;
    return NULL;
}

int zmq_term(void *ctx_) {
    return zmq_ctx_term(ctx_);
}

int zmq_ctx_destroy(void *ctx_) {
    return zmq_ctx_term(ctx_);
}


// Sockets

void *zmq_socket(void *ctx_, int type_) {
    if (!ctx_ || !((zmq::ctx_t *) ctx_)->check_tag()) {
        errno = EFAULT;
        return NULL;
    }
    zmq::ctx_t *ctx = (zmq::ctx_t *) ctx_;
    zmq::socket_base_t *s = ctx->create_socket(type_);
    return (void *) s;
}

int zmq_close(void *s_) {
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    ((zmq::socket_base_t *) s_)->close();
    return 0;
}

int zmq_setsockopt(void *s_, int option_, const void *optval_,
                   size_t optvallen_) {
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;
    int result = s->setsockopt(option_, optval_, optvallen_);
    return result;
}

int zmq_getsockopt(void *s_, int option_, void *optval_, size_t *optvallen_) {
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;
    int result = s->getsockopt(option_, optval_, optvallen_);
    return result;
}

int zmq_socket_monitor(void *s_, const char *addr_, int events_) {
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;
    int result = s->monitor(addr_, events_);
    return result;
}

int zmq_bind(void *s_, const char *addr_) {
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;
    int result = s->bind(addr_);
    return result;
}

int zmq_connect(void *s_, const char *addr_) {
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;
    int result = s->connect(addr_);
    return result;
}

int zmq_unbind(void *s_, const char *addr_) {
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;
    return s->term_endpoint(addr_);
}

int zmq_disconnect(void *s_, const char *addr_) {
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;
    return s->term_endpoint(addr_);
}

// Sending functions.

static int
s_sendmsg(zmq::socket_base_t *s_, zmq_msg_t *msg_, int flags_) {
    int sz = (int) zmq_msg_size(msg_);
    int rc = s_->send((zmq::msg_t *) msg_, flags_);
    if (unlikely (rc < 0))
        return -1;
    return sz;
}

/*  To be deprecated once zmq_msg_send() is stable                           */
int zmq_sendmsg(void *s_, zmq_msg_t *msg_, int flags_) {
    return zmq_msg_send(msg_, s_, flags_);
}

int zmq_send(void *s_, const void *buf_, size_t len_, int flags_) {
    // Sock存在问题(这个应该也不是主要问题)
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    
    
    // 正常发送Msg
    zmq_msg_t msg;
    int rc = zmq_msg_init_size(&msg, len_);
    if (rc != 0)
        return -1;
    memcpy(zmq_msg_data(&msg), buf_, len_);

    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;
    rc = s_sendmsg(s, &msg, flags_);
    if (unlikely (rc < 0)) {
        int err = errno;
        int rc2 = zmq_msg_close(&msg);
        errno_assert (rc2 == 0);
        errno = err;
        return -1;
    }

    //  Note the optimisation here. We don't close the msg object as it is
    //  empty anyway. This may change when implementation of zmq_msg_t changes.
    return rc;
}

int zmq_send_const(void *s_, const void *buf_, size_t len_, int flags_) {
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    zmq_msg_t msg;
    int rc = zmq_msg_init_data(&msg, (void *) buf_, len_, NULL, NULL);
    if (rc != 0)
        return -1;

    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;
    rc = s_sendmsg(s, &msg, flags_);
    if (unlikely (rc < 0)) {
        int err = errno;
        int rc2 = zmq_msg_close(&msg);
        errno_assert (rc2 == 0);
        errno = err;
        return -1;
    }

    //  Note the optimisation here. We don't close the msg object as it is
    //  empty anyway. This may change when implementation of zmq_msg_t changes.
    return rc;
}


// Send multiple messages.
// TODO: this function has no man page
//
// If flag bit ZMQ_SNDMORE is set the vector is treated as
// a single multi-part message, i.e. the last message has
// ZMQ_SNDMORE bit switched off.
//
int zmq_sendiov(void *s_, iovec *a_, size_t count_, int flags_) {
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    int rc = 0;
    zmq_msg_t msg;
    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;

    for (size_t i = 0; i < count_; ++i) {
        rc = zmq_msg_init_size(&msg, a_[i].iov_len);
        if (rc != 0) {
            rc = -1;
            break;
        }
        memcpy(zmq_msg_data(&msg), a_[i].iov_base, a_[i].iov_len);
        if (i == count_ - 1)
            flags_ = flags_ & ~ZMQ_SNDMORE;
        rc = s_sendmsg(s, &msg, flags_);
        if (unlikely (rc < 0)) {
            int err = errno;
            int rc2 = zmq_msg_close(&msg);
            errno_assert (rc2 == 0);
            errno = err;
            rc = -1;
            break;
        }
    }
    return rc;
}

// Receiving functions.

static int s_recvmsg(zmq::socket_base_t *s_, zmq_msg_t *msg_, int flags_) {
    // 注意强制类型装换
    int rc = s_->recv((zmq::msg_t *) msg_, flags_);
    
    // 如果读取失败，则返回 -1, 例如: interruped
    if (unlikely (rc < 0))
        return -1;
    return (int) zmq_msg_size(msg_);
}

/*  To be deprecated once zmq_msg_recv() is stable                           */
int zmq_recvmsg(void *s_, zmq_msg_t *msg_, int flags_) {
    return zmq_msg_recv(msg_, s_, flags_);
}


int zmq_recv(void *s_, void *buf_, size_t len_, int flags_) {
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    zmq_msg_t msg;
    int rc = zmq_msg_init(&msg);
    errno_assert (rc == 0);

    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;
    int nbytes = s_recvmsg(s, &msg, flags_);
    if (unlikely (nbytes < 0)) {
        int err = errno;
        rc = zmq_msg_close(&msg);
        errno_assert (rc == 0);
        errno = err;
        return -1;
    }

    //  At the moment an oversized message is silently truncated.
    //  TODO: Build in a notification mechanism to report the overflows.
    size_t to_copy = size_t(nbytes) < len_ ? size_t(nbytes) : len_;
    memcpy(buf_, zmq_msg_data(&msg), to_copy);

    rc = zmq_msg_close(&msg);
    errno_assert (rc == 0);

    return nbytes;
}

// Receive a multi-part message
// 
// Receives up to *count_ parts of a multi-part message.
// Sets *count_ to the actual number of parts read.
// ZMQ_RCVMORE is set to indicate if a complete multi-part message was read.
// Returns number of message parts read, or -1 on error.
//
// Note: even if -1 is returned, some parts of the message
// may have been read. Therefore the client must consult
// *count_ to retrieve message parts successfully read,
// even if -1 is returned.
//
// The iov_base* buffers of each iovec *a_ filled in by this 
// function may be freed using free().
// TODO: this function has no man page
//
int zmq_recviov(void *s_, iovec *a_, size_t *count_, int flags_) {
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;

    size_t count = *count_;
    int nread = 0;
    bool recvmore = true;

    *count_ = 0;

    for (size_t i = 0; recvmore && i < count; ++i) {

        zmq_msg_t msg;
        int rc = zmq_msg_init(&msg);
        errno_assert (rc == 0);

        int nbytes = s_recvmsg(s, &msg, flags_);
        if (unlikely (nbytes < 0)) {
            int err = errno;
            rc = zmq_msg_close(&msg);
            errno_assert (rc == 0);
            errno = err;
            nread = -1;
            break;
        }

        a_[i].iov_len = zmq_msg_size(&msg);
        a_[i].iov_base = malloc(a_[i].iov_len);
        if (unlikely (!a_[i].iov_base)) {
            errno = ENOMEM;
            return -1;
        }
        memcpy(a_[i].iov_base, static_cast<char *> (zmq_msg_data(&msg)),
               a_[i].iov_len);
        // Assume zmq_socket ZMQ_RVCMORE is properly set.
        recvmore = ((zmq::msg_t *) (void *) &msg)->flags() & zmq::msg_t::more;
        rc = zmq_msg_close(&msg);
        errno_assert (rc == 0);
        ++*count_;
        ++nread;
    }
    return nread;
}

// Message manipulators.

int zmq_msg_init(zmq_msg_t *msg_) {
    // 对外的都是假的
    return ((zmq::msg_t *) msg_)->init();
}

int zmq_msg_init_size(zmq_msg_t *msg_, size_t size_) {
    return ((zmq::msg_t *) msg_)->init_size(size_);
}

int zmq_msg_init_data(zmq_msg_t *msg_, void *data_, size_t size_,
                      zmq_free_fn *ffn_, void *hint_) {
    return ((zmq::msg_t *) msg_)->init_data(data_, size_, ffn_, hint_);
}

int zmq_msg_send(zmq_msg_t *msg_, void *s_, int flags_) {
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;
    int result = s_sendmsg(s, msg_, flags_);
    return result;
}

//
// 对外的API
// 例如: zmq.Socket socket
//      socket.RecvBytes() 读取一个message part
//
int zmq_msg_recv(zmq_msg_t *msg_, void *s_, int flags_) {
    // s_ 为某个给定的 socket
    if (!s_ || !((zmq::socket_base_t *) s_)->check_tag()) {
        errno = ENOTSOCK;
        return -1;
    }
    zmq::socket_base_t *s = (zmq::socket_base_t *) s_;
    int result = s_recvmsg(s, msg_, flags_);
    return result;
}

int zmq_msg_close(zmq_msg_t *msg_) {
    return ((zmq::msg_t *) msg_)->close();
}

int zmq_msg_move(zmq_msg_t *dest_, zmq_msg_t *src_) {
    return ((zmq::msg_t *) dest_)->move(*(zmq::msg_t *) src_);
}

int zmq_msg_copy(zmq_msg_t *dest_, zmq_msg_t *src_) {
    return ((zmq::msg_t *) dest_)->copy(*(zmq::msg_t *) src_);
}

void *zmq_msg_data(zmq_msg_t *msg_) {
    return ((zmq::msg_t *) msg_)->data();
}

//
// 返回不同消息的大小
//
size_t zmq_msg_size(zmq_msg_t *msg_) {
    return ((zmq::msg_t *) msg_)->size();
}

int zmq_msg_more(zmq_msg_t *msg_) {
    return zmq_msg_get(msg_, ZMQ_MORE);
}

int zmq_msg_get(zmq_msg_t *msg_, int option_) {
    switch (option_) {
        case ZMQ_MORE:
            return (((zmq::msg_t *) msg_)->flags() & zmq::msg_t::more) ? 1 : 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int zmq_msg_set(zmq_msg_t *, int, int) {
    //  No options supported at present
    errno = EINVAL;
    return -1;
}

// Polling.
// 关于Timeout的作用?
int zmq_poll(zmq_pollitem_t *items_, int nitems_, long timeout_) {
    if (unlikely (nitems_ < 0)) {
        errno = EINVAL;
        return -1;
    }
    
    // 如果 ntimes == 0, 则至少sleep指定的时间(单位为 ms)
    if (unlikely (nitems_ == 0)) {
        if (timeout_ == 0)
            return 0;
        // 有可能是极大的时间浪费
        return usleep (timeout_ * 1000);
    }

    if (!items_) {
        errno = EFAULT;
        return -1;
    }

    zmq::clock_t clock;
    uint64_t now = 0;
    uint64_t end = 0;
    
    // 准备空间
    pollfd spollfds[ZMQ_POLLITEMS_DFLT];
    pollfd *pollfds = spollfds;
    if (nitems_ > ZMQ_POLLITEMS_DFLT) {
        pollfds = (pollfd*) malloc (nitems_ * sizeof (pollfd));
        alloc_assert (pollfds);
    }

    //  Build pollset for poll () system call.
    for (int i = 0; i != nitems_; i++) {

        //  If the poll item is a 0MQ socket, we poll on the file descriptor
        //  retrieved by the ZMQ_FD socket option.
        if (items_ [i].socket) {
            size_t zmq_fd_size = sizeof (zmq::fd_t);
            if (zmq_getsockopt (items_ [i].socket, ZMQ_FD, &pollfds [i].fd,
                &zmq_fd_size) == -1) {
                if (pollfds != spollfds)
                    free (pollfds);
                return -1;
            }
            pollfds [i].events = items_ [i].events ? POLLIN : 0;
        }
        //  Else, the poll item is a raw file descriptor. Just convert the
        //  events to normal POLLIN/POLLOUT for poll ().
        else {
            pollfds [i].fd = items_ [i].fd;
            pollfds [i].events =
                (items_ [i].events & ZMQ_POLLIN ? POLLIN : 0) |
                (items_ [i].events & ZMQ_POLLOUT ? POLLOUT : 0);
        }
    }

    bool first_pass = true;
    int nevents = 0;

    while (true) {
        //  Compute the timeout for the subsequent poll.
        int timeout;
        if (first_pass)
            timeout = 0;
        else
        if (timeout_ < 0)
            timeout = -1;
        else
            timeout = end - now;

        //  Wait for events.
        while (true) {
            int rc = poll (pollfds, nitems_, timeout);
            if (rc == -1 && errno == EINTR) {
                if (pollfds != spollfds)
                    free (pollfds);
                return -1;
            }
            errno_assert (rc >= 0);
            break;
        }
        //  Check for the events.
        for (int i = 0; i != nitems_; i++) {

            items_ [i].revents = 0;

            //  The poll item is a 0MQ socket. Retrieve pending events
            //  using the ZMQ_EVENTS socket option.
            if (items_ [i].socket) {
                size_t zmq_events_size = sizeof (uint32_t);
                uint32_t zmq_events;
                if (zmq_getsockopt (items_ [i].socket, ZMQ_EVENTS, &zmq_events,
                    &zmq_events_size) == -1) {
                    if (pollfds != spollfds)
                        free (pollfds);
                    return -1;
                }
                if ((items_ [i].events & ZMQ_POLLOUT) &&
                      (zmq_events & ZMQ_POLLOUT))
                    items_ [i].revents |= ZMQ_POLLOUT;
                if ((items_ [i].events & ZMQ_POLLIN) &&
                      (zmq_events & ZMQ_POLLIN))
                    items_ [i].revents |= ZMQ_POLLIN;
            }
            //  Else, the poll item is a raw file descriptor, simply convert
            //  the events to zmq_pollitem_t-style format.
            else {
                if (pollfds [i].revents & POLLIN)
                    items_ [i].revents |= ZMQ_POLLIN;
                if (pollfds [i].revents & POLLOUT)
                    items_ [i].revents |= ZMQ_POLLOUT;
                if (pollfds [i].revents & ~(POLLIN | POLLOUT))
                    items_ [i].revents |= ZMQ_POLLERR;
            }

            if (items_ [i].revents)
                nevents++;
        }

        //  If timout is zero, exit immediately whether there are events or not.
        if (timeout_ == 0)
            break;

        //  If there are events to return, we can exit immediately.
        if (nevents)
            break;

        //  At this point we are meant to wait for events but there are none.
        //  If timeout is infinite we can just loop until we get some events.
        if (timeout_ < 0) {
            if (first_pass)
                first_pass = false;
            continue;
        }

        //  The timeout is finite and there are no events. In the first pass
        //  we get a timestamp of when the polling have begun. (We assume that
        //  first pass have taken negligible time). We also compute the time
        //  when the polling should time out.
        if (first_pass) {
            now = clock.now_ms ();
            end = now + timeout_;
            if (now == end)
                break;
            first_pass = false;
            continue;
        }

        //  Find out whether timeout have expired.
        now = clock.now_ms ();
        if (now >= end)
            break;
    }

    if (pollfds != spollfds)
        free (pollfds);
    return nevents;
    
}

#if defined ZMQ_POLL_BASED_ON_SELECT
#undef ZMQ_POLL_BASED_ON_SELECT
#endif
#if defined ZMQ_POLL_BASED_ON_POLL
#undef ZMQ_POLL_BASED_ON_POLL
#endif

//  The proxy functionality

int zmq_proxy(void *frontend_, void *backend_, void *capture_) {
    if (!frontend_ || !backend_) {
        errno = EFAULT;
        return -1;
    }
    return zmq::proxy(
            (zmq::socket_base_t *) frontend_,
            (zmq::socket_base_t *) backend_,
            (zmq::socket_base_t *) capture_);
}

int zmq_proxy_steerable(void *frontend_, void *backend_, void *capture_, void *control_) {
    if (!frontend_ || !backend_) {
        errno = EFAULT;
        return -1;
    }
    return zmq::proxy(
            (zmq::socket_base_t *) frontend_,
            (zmq::socket_base_t *) backend_,
            (zmq::socket_base_t *) capture_,
            (zmq::socket_base_t *) control_);
}

//  The deprecated device functionality

int zmq_device(int /* type */, void *frontend_, void *backend_) {
    return zmq::proxy(
            (zmq::socket_base_t *) frontend_,
            (zmq::socket_base_t *) backend_, NULL);
}
