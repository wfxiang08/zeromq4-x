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

#include "dealer.hpp"
#include "err.hpp"
#include "msg.hpp"

zmq::dealer_t::dealer_t(class ctx_t *parent_, uint32_t tid_, int sid_) :
        socket_base_t(parent_, tid_, sid_),
        probe_router(false) {
    // 注意: options.type的定义
    options.type = ZMQ_DEALER;
}

zmq::dealer_t::~dealer_t() {
}

//
// 将 dealer 和 pipe_关联起来，采用 fq 来从pipes中读取数据，采用 lb 来往 pipes中写出数据
//
void zmq::dealer_t::xattach_pipe(pipe_t *pipe_, bool subscribe_to_all_) {
    // subscribe_to_all_ is unused
    (void) subscribe_to_all_;

    zmq_assert (pipe_);

    // 空消息的作用?
    // router如何使用? 如何跳过？
    if (probe_router) {
        // 发送一个空消息到 peering router
        msg_t probe_msg_;
        int rc = probe_msg_.init();
        errno_assert (rc == 0);

        rc = pipe_->write(&probe_msg_);
        // zmq_assert (rc) is not applicable here, since it is not a bug.
        pipe_->flush();

        rc = probe_msg_.close();
        errno_assert (rc == 0);
    }

    // 输入使用 fq
    fq.attach(pipe_);
    
    // 输出采用 lb
    lb.attach(pipe_);
}

int zmq::dealer_t::xsetsockopt(int option_, const void *optval_,
                               size_t optvallen_) {
    // 直接按照长度来判断是否为int
    bool is_int = (optvallen_ == sizeof(int));
    int value = is_int ? *((int *) optval_) : 0;

    switch (option_) {
        case ZMQ_PROBE_ROUTER:
            if (is_int && value >= 0) {
                probe_router = (value != 0);
                return 0;
            }
            break;

        default:
            break;
    }

    errno = EINVAL;
    return -1;
}

// DEALER的发送很直接，没有多余的动作
int zmq::dealer_t::xsend(msg_t *msg_) {
    return sendpipe(msg_, NULL);
}

// DEALER的接受很直接，没有多余的动作
int zmq::dealer_t::xrecv(msg_t *msg_) {
    return recvpipe(msg_, NULL);
}

bool zmq::dealer_t::xhas_in() {
    return fq.has_in();
}

bool zmq::dealer_t::xhas_out() {
    return lb.has_out();
}

void zmq::dealer_t::xread_activated(pipe_t *pipe_) {
    fq.activated(pipe_);
}

void zmq::dealer_t::xwrite_activated(pipe_t *pipe_) {
    lb.activated(pipe_);
}

void zmq::dealer_t::xpipe_terminated(pipe_t *pipe_) {
    fq.pipe_terminated(pipe_);
    lb.pipe_terminated(pipe_);
}

//
// 通过 lb 将msg输出
//
int zmq::dealer_t::sendpipe(msg_t *msg_, pipe_t **pipe_) {
    return lb.sendpipe(msg_, pipe_);
}

//
// 通过 fq 来读取msg
//
int zmq::dealer_t::recvpipe(msg_t *msg_, pipe_t **pipe_) {
    return fq.recvpipe(msg_, pipe_);
}
