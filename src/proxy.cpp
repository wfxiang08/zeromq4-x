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

#include <stddef.h>
#include "platform.hpp"
#include "proxy.hpp"
#include "likely.hpp"

#define ZMQ_POLL_BASED_ON_POLL

#include <poll.h>
// These headers end up pulling in zmq.h somewhere in their include
// dependency chain
#include "socket_base.hpp"

// zmq.h must be included *after* poll.h for AIX to build properly


int zmq::proxy(class socket_base_t *frontend_, class socket_base_t *backend_, class socket_base_t *capture_, class socket_base_t *control_) {
    msg_t msg;
    int rc = msg.init();
    if (rc != 0)
        return -1;

    //  The algorithm below assumes ratio of requests and replies processed
    //  under full load to be 1:1.

    int more;
    size_t moresz;
    
    // 通过监控三路数据?
    zmq_pollitem_t items[] = {
            {frontend_, 0, ZMQ_POLLIN, 0},
            {backend_,  0, ZMQ_POLLIN, 0},
    };
    int qt_poll_items = 2; // (control_ ? 3 : 2);

    //  Proxy can be in these three states
    enum {
        active,
        paused,
        terminated
    } state = active;

    while (state != terminated) {
        //  Wait while there are either requests or replies to process.
        // poll所有的输入，怎么没有timeout呢?
        // &items[0] <---> items ??
        rc = zmq_poll(&items[0], qt_poll_items, -1);
        
        // 似乎是出现了interrupt
        if (unlikely (rc < 0))
            return -1;



        // 通过front有输入，并且backend有输出，则
        //  Process a request
        if (state == active && items[0].revents & ZMQ_POLLIN && items[1].revents & ZMQ_POLLOUT) {
            // 一口气读取一个完整的Message, 从 frontend ---> backend
            while (true) {
                // 1. 读取一个msg part
                rc = frontend_->recv(&msg, 0);
                if (unlikely (rc < 0))
                    return -1;

                // 2. 读取msg的状态，是否有 more
                moresz = sizeof more;
                rc = frontend_->getsockopt(ZMQ_RCVMORE, &more, &moresz);
                if (unlikely (rc < 0))
                    return -1;
                
                // 将数据拷贝到backend中
                rc = backend_->send(&msg, more ? ZMQ_SNDMORE : 0);
                if (unlikely (rc < 0))
                    return -1;
                if (more == 0)
                    break;
            }
        }
        //  Process a reply
        if (state == active  && items[1].revents & ZMQ_POLLIN  && items[0].revents & ZMQ_POLLOUT) {
            // 从: backend ---> frontend
            while (true) {
                rc = backend_->recv(&msg, 0);
                if (unlikely (rc < 0))
                    return -1;

                moresz = sizeof more;
                rc = backend_->getsockopt(ZMQ_RCVMORE, &more, &moresz);
                if (unlikely (rc < 0))
                    return -1;

                //  Copy message to capture socket if any
                if (capture_) {
                    msg_t ctrl;
                    rc = ctrl.init();
                    if (unlikely (rc < 0))
                        return -1;
                    rc = ctrl.copy(msg);
                    if (unlikely (rc < 0))
                        return -1;
                    rc = capture_->send(&ctrl, more ? ZMQ_SNDMORE : 0);
                    if (unlikely (rc < 0))
                        return -1;
                }
                rc = frontend_->send(&msg, more ? ZMQ_SNDMORE : 0);
                if (unlikely (rc < 0))
                    return -1;
                if (more == 0)
                    break;
            }
        }

    }
    return 0;
}
