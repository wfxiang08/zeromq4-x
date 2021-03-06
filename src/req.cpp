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

#include "req.hpp"
#include "err.hpp"
#include "msg.hpp"
#include "wire.hpp"
#include "random.hpp"
#include "likely.hpp"

zmq::req_t::req_t(class ctx_t *parent_, uint32_t tid_, int sid_) :
        dealer_t(parent_, tid_, sid_),
        receiving_reply(false),
        message_begins(true),
        reply_pipe(NULL),
        request_id_frames_enabled(false),
        request_id(generate_random()),
        strict(true) {
    options.type = ZMQ_REQ;
}

zmq::req_t::~req_t() {
}

//
// REQ如何发送数据呢?
//
int zmq::req_t::xsend(msg_t *msg_) {
    //  If we've sent a request and we still haven't got the reply,
    //  we can't send another request unless the strict option is disabled.
    // 0. 状态机管理
    if (receiving_reply) {
        if (strict) {
            errno = EFSM;
            return -1;
        }

        // 如果不是 strict, 则终结 reply_pipe, 开始一个新的 message
        if (reply_pipe)
            reply_pipe->terminate(false);
        receiving_reply = false;
        message_begins = true;
    }

    //  First part of the request is the request identity.
    // 1. 上一个消息结束后的状态:  receiving_reply = false, message_begins: true
    if (message_begins) {
        reply_pipe = NULL;

//        if (request_id_frames_enabled) {
//            request_id++;
//
//            msg_t id;
//            int rc = id.init_data(&request_id, sizeof(request_id), NULL, NULL);
//            errno_assert (rc == 0);
//            id.set_flags(msg_t::more);
//
//            rc = dealer_t::sendpipe(&id, &reply_pipe);
//            if (rc != 0)
//                return -1;
//        }
        
        // 似乎发送了一个空的消息
        // <identity, "", data>
        msg_t bottom;
        int rc = bottom.init();
        errno_assert (rc == 0);
        bottom.set_flags(msg_t::more);

        rc = dealer_t::sendpipe(&bottom, &reply_pipe);
        if (rc != 0)
            return -1;
        assert (reply_pipe);

        message_begins = false;

        // Eat all currently avaliable messages before the request is fully
        // sent. This is done to avoid:
        //   REQ sends request to A, A replies, B replies too.
        //   A's reply was first and matches, that is used.
        //   An hour later REQ sends a request to B. B's old reply is used.
        msg_t drop;
        while (true) {
            rc = drop.init();
            errno_assert (rc == 0);
            rc = dealer_t::xrecv(&drop);
            if (rc != 0)
                break;
            drop.close();
        }
    }

    // 2. 获取msg的状态(并且发送消息)
    bool more = msg_->flags() & msg_t::more ? true : false;

    // 3. 发送msg
    int rc = dealer_t::xsend(msg_);
    if (rc != 0)
        return rc;

    //  If the request was fully sent, flip the FSM into reply-receiving state.
    // 4. 控制 receiving_reply, message_begins
    // 消息发送完毕，状态机转移
    if (!more) {
        receiving_reply = true;
        message_begins = true;
    }

    return 0;
}

int zmq::req_t::xrecv(msg_t *msg_) {
    // 也就是xrecv, xsend的工作状态必须固定
    // 0. 状态机管理。如果不是准备接受阶段就不能接受数据
    //  If request wasn't send, we can't wait for reply.
    if (!receiving_reply) {
        errno = EFSM; // Error Finite State Machine
        return -1;
    }

    //  Skip messages until one with the right first frames is found.
    // 如果上一个消息没有读取完毕，则需要清理干净
    while (message_begins) {
//        //  If enabled, the first frame must have the correct request_id.
//        if (request_id_frames_enabled) {
//            int rc = recv_reply_pipe(msg_);
//            if (rc != 0)
//                return rc;
//
//            if (unlikely (!(msg_->flags() & msg_t::more) ||
//                          msg_->size() != sizeof(request_id) ||
//                          *static_cast<uint32_t *> (msg_->data()) != request_id)) {
//                //  Skip the remaining frames and try the next message
//                while (msg_->flags() & msg_t::more) {
//                    rc = recv_reply_pipe(msg_);
//                    errno_assert (rc == 0);
//                }
//                continue;
//            }
//        }

        //  The next frame must be 0.
        // TODO: Failing this check should also close the connection with the peer!
        // 1. 读取一个消息
        int rc = recv_reply_pipe(msg_);
        if (rc != 0)
            return rc;
        
        // 这个消息可能是 <identity_xxx, "", data>
        // 如果没有更多，或者msg非空，这是什么情况? 和预取不一样
        // 如何消息不合法，则直接跳过
        if (unlikely (!(msg_->flags() & msg_t::more) || msg_->size() != 0)) {
            // 如果还有没有读取的消息，继续读取，直到完毕
            //  Skip the remaining frames and try the next message
            while (msg_->flags() & msg_t::more) {
                rc = recv_reply_pipe(msg_);
                errno_assert (rc == 0);
            }
            continue;
        }

        message_begins = false;
    }

    // 到达理想状态:
    int rc = recv_reply_pipe(msg_);
    if (rc != 0)
        return rc;

    // 如果数据读取完毕，则转换状态机
    // 准备发送数据，并且开始下一个消息
    //  If the reply is fully received, flip the FSM into request-sending state.
    if (!(msg_->flags() & msg_t::more)) {
        receiving_reply = false;
        message_begins = true;
    }

    return 0;
}

//
// 是否有数据可读
//
bool zmq::req_t::xhas_in() {
    //  TODO: Duplicates should be removed here.

    // 在发送状态下，肯定没有数据可读
    if (!receiving_reply)
        return false;

    // 其他状态通 dealer_t
    return dealer_t::xhas_in();
}

bool zmq::req_t::xhas_out() {
    if (receiving_reply)
        return false;

    return dealer_t::xhas_out();
}

int zmq::req_t::xsetsockopt(int option_, const void *optval_, size_t optvallen_) {
    bool is_int = (optvallen_ == sizeof(int));
    int value = is_int ? *((int *) optval_) : 0;
    switch (option_) {
        case ZMQ_REQ_CORRELATE:
            if (is_int && value >= 0) {
                request_id_frames_enabled = (value != 0);
                return 0;
            }
            break;

        case ZMQ_REQ_RELAXED:
            if (is_int && value >= 0) {
                strict = (value == 0);
                return 0;
            }
            break;

        default:
            break;
    }

    return dealer_t::xsetsockopt(option_, optval_, optvallen_);
}

void zmq::req_t::xpipe_terminated(pipe_t *pipe_) {
    if (reply_pipe == pipe_)
        reply_pipe = NULL;
    dealer_t::xpipe_terminated(pipe_);
}

int zmq::req_t::recv_reply_pipe(msg_t *msg_) {
    while (true) {
        pipe_t *pipe = NULL;
        int rc = dealer_t::recvpipe(msg_, &pipe);
        if (rc != 0)
            return rc;
        if (!reply_pipe || pipe == reply_pipe)
            return 0;
    }
}

zmq::req_session_t::req_session_t(io_thread_t *io_thread_, bool connect_,
                                  socket_base_t *socket_, const options_t &options_,
                                  const address_t *addr_) :
        session_base_t(io_thread_, connect_, socket_, options_, addr_),
        state(bottom) {
}

zmq::req_session_t::~req_session_t() {
}

int zmq::req_session_t::push_msg(msg_t *msg_) {
    switch (state) {
        case bottom:
            // 在bottom状态下，只接受长度为0的消息?
            if (msg_->flags() == msg_t::more && msg_->size() == 0) {
                state = body;
                return session_base_t::push_msg(msg_);
            }
            break;
        case body:
            if (msg_->flags() == msg_t::more)
                return session_base_t::push_msg(msg_);
            if (msg_->flags() == 0) {
                state = bottom;
                return session_base_t::push_msg(msg_);
            }
            break;
    }
    errno = EFAULT;
    return -1;
}

void zmq::req_session_t::reset() {
    session_base_t::reset();
    state = bottom;
}
