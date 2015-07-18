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

#include "lb.hpp"
#include "pipe.hpp"
#include "err.hpp"
#include "msg.hpp"

zmq::lb_t::lb_t() :
        active(0),
        current(0),
        more(false),
        dropping(false) {
    // 初始状态下没有 active, current也设置为0
}

zmq::lb_t::~lb_t() {
    zmq_assert (pipes.empty());
}

void zmq::lb_t::attach(pipe_t *pipe_) {
    pipes.push_back(pipe_);
    
    // 将pipe_加入到队列最尾部
    // 然后再交换，扩充 active的个数
    activated(pipe_);
}

//
// 标记 pipe_ terminated
//
void zmq::lb_t::pipe_terminated(pipe_t *pipe_) {
    pipes_t::size_type index = pipes.index(pipe_);

    //  If we are in the middle of multipart message and current pipe
    //  have disconnected, we have to drop the remainder of the message.
    if (index == current && more)
        dropping = true;

    //  Remove the pipe from the list; adjust number of active pipes
    //  accordingly.
    if (index < active) {
        active--;
        pipes.swap(index, active);
        if (current == active)
            current = 0;
    }
    
    // 从 pipes中删除 pipe_
    pipes.erase(pipe_);
}

//
// 将 pipe_ 激活，增加了当前 active pipes
//
void zmq::lb_t::activated(pipe_t *pipe_) {
    //  Move the pipe to the list of active pipes.
    pipes.swap(pipes.index(pipe_), active);
    active++;
}

//
// 通过 load balance 发送 msg_
//
int zmq::lb_t::send(msg_t *msg_) {
    return sendpipe(msg_, NULL);
}

//
// 发送一个msg, 而且当前的 more状态应该不成立
// 只处理整个Message的第一个mst_t
//
int zmq::lb_t::sendpipe(msg_t *msg_, pipe_t **pipe_) {
    //  Drop the message if required. If we are at the end of the message
    //  switch back to non-dropping mode.
    if (dropping) {

        more = msg_->flags() & msg_t::more ? true : false;
        dropping = more;

        int rc = msg_->close();
        errno_assert (rc == 0);
        rc = msg_->init();
        errno_assert (rc == 0);
        return 0;
    }

    // 
    // 可以认为: active是当前可用的 pipes的个数
    //
    while (active > 0) {
        // 如果往 current 中写出数据，并且成功，则结束; 并且 pipe_记录使用的 pipe
        if (pipes[current]->write(msg_)) {
            // 写成功了，就跳过下面的步骤
            if (pipe_)
                *pipe_ = pipes[current];
            break;
        }

        // 写失败了，为什么 more不应为false
        zmq_assert (!more);
        
        // 将 current切换到 active的位置，表示pipe不可用
        active--;
        if (current < active)
            pipes.swap(current, active);
        else
            // 如果没有可用的pipes, 则: active也似乎为0了
            current = 0;
    }

    //  If there are no pipes, we cannot send the message.
    if (active == 0) {
        errno = EAGAIN;
        return -1;
    }

    //  If it's final part of the message we can flush it downstream and
    //  continue round-robining (load balance).
    more = msg_->flags() & msg_t::more ? true : false;
    if (!more) {
        // 如果数据写出去成功，则切换到下一个 active的pipes, 并且flush
        pipes[current]->flush();
        current = (current + 1) % active;
    }

    //  Detach the message from the data buffer.
    int rc = msg_->init();
    errno_assert (rc == 0);

    return 0;
}

bool zmq::lb_t::has_out() {
    //  If one part of the message was already written we can definitely
    //  write the rest of the message.
    if (more)
        return true;

    // 检查当前的 pipe是否可用
    while (active > 0) {

        //  Check whether a pipe has room for another message.
        if (pipes[current]->check_write())
            return true;

        //  Deactivate the pipe.
        active--;
        pipes.swap(current, active);
        if (current == active)
            current = 0;
    }

    return false;
}
