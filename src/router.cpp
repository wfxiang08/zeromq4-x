
#include "router.hpp"
#include "wire.hpp"
#include "random.hpp"

zmq::router_t::router_t(class ctx_t *parent_, uint32_t tid_, int sid_) : socket_base_t(parent_, tid_, sid_),
        prefetched(false),
        identity_sent(false),
        more_in(false),
        current_out(NULL),
        more_out(false),
        next_peer_id(generate_random()),
        mandatory(false),
        //  raw_sock functionality in ROUTER is deprecated
        raw_sock(false),
        probe_router(false) {
    options.type = ZMQ_ROUTER;
    options.recv_identity = true;
    options.raw_sock = false;

    prefetched_id.init();
    prefetched_msg.init();
}

zmq::router_t::~router_t() {
    zmq_assert (anonymous_pipes.empty());;
    zmq_assert (outpipes.empty());
    prefetched_id.close();
    prefetched_msg.close();
}

void zmq::router_t::xattach_pipe(pipe_t *pipe_, bool subscribe_to_all_) {
    // subscribe_to_all_ is unused
    (void) subscribe_to_all_;

    zmq_assert (pipe_);

    if (probe_router) {
        msg_t probe_msg_;
        int rc = probe_msg_.init();
        errno_assert (rc == 0);

        rc = pipe_->write(&probe_msg_);
        // zmq_assert (rc) is not applicable here, since it is not a bug.
        pipe_->flush();

        rc = probe_msg_.close();
        errno_assert (rc == 0);
    }

    bool identity_ok = identify_peer(pipe_);
    if (identity_ok)
        fq.attach(pipe_);
    else
        anonymous_pipes.insert(pipe_);
}

int zmq::router_t::xsetsockopt(int option_, const void *optval_,
                               size_t optvallen_) {
    bool is_int = (optvallen_ == sizeof(int));
    int value = is_int ? *((int *) optval_) : 0;

    switch (option_) {
        case ZMQ_ROUTER_RAW:
            if (is_int && value >= 0) {
                raw_sock = (value != 0);
                if (raw_sock) {
                    options.recv_identity = false;
                    options.raw_sock = true;
                }
                return 0;
            }
            break;

        case ZMQ_ROUTER_MANDATORY:
            if (is_int && value >= 0) {
                mandatory = (value != 0);
                return 0;
            }
            break;

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


void zmq::router_t::xpipe_terminated(pipe_t *pipe_) {
    std::set<pipe_t *>::iterator it = anonymous_pipes.find(pipe_);
    if (it != anonymous_pipes.end())
        anonymous_pipes.erase(it);
    else {
        outpipes_t::iterator it = outpipes.find(pipe_->get_identity());
        zmq_assert (it != outpipes.end());
        outpipes.erase(it);
        fq.pipe_terminated(pipe_);
        if (pipe_ == current_out)
            current_out = NULL;
    }
}

void zmq::router_t::xread_activated(pipe_t *pipe_) {
    std::set<pipe_t *>::iterator it = anonymous_pipes.find(pipe_);
    // 激活 pipe_
    // 1. 如果没有找到，则激活
    if (it == anonymous_pipes.end())
        fq.activated(pipe_);
    else {
        // 重新attch
        bool identity_ok = identify_peer(pipe_);
        if (identity_ok) {
            anonymous_pipes.erase(it);
            fq.attach(pipe_);
        }
    }
}

void zmq::router_t::xwrite_activated(pipe_t *pipe_) {
    outpipes_t::iterator it;
    for (it = outpipes.begin(); it != outpipes.end(); ++it)
        if (it->second.pipe == pipe_)
            break;

    zmq_assert (it != outpipes.end());
    zmq_assert (!it->second.active);
    it->second.active = true;
}

// router的xsend必须包含 pipe的id
// 如果包含合适的路由，则选择 current_out, 然后输出数据
//        不包含，则扔掉路msg， 知道整个message都背扔掉，然后开始新的消息的处理
// 有哪些地方调用  router_t::xsend函数，并且友好地处理了Msg
//
int zmq::router_t::xsend(msg_t *msg_) {
    //  If this is the first part of the message it's the ID of the
    //  peer to send the message to.
    
    // 如果是第一个msg part, 则它一定是路由信息，最终找到 current_out就成功了
    if (!more_out) {
        zmq_assert (!current_out);

        //  If we have malformed message (prefix with no subsequent message)
        //  then just silently ignore it.
        //  TODO: The connections should be killed instead.
        if (msg_->flags() & msg_t::more) {

            more_out = true;

            //  Find the pipe associated with the identity stored in the prefix.
            //  If there's no such pipe just silently ignore the message, unless
            //  router_mandatory is set.
            // XXX: 通过map来实现identity到outpipe的映射
            blob_t identity((unsigned char *) msg_->data(), msg_->size());
            outpipes_t::iterator it = outpipes.find(identity);

            if (it != outpipes.end()) {
                current_out = it->second.pipe;
                if (!current_out->check_write()) {
                    it->second.active = false;
                    current_out = NULL;
                    
                    // 发送失败，再来一次
                    if (mandatory) {
                        more_out = false;
                        errno = EAGAIN;
                        return -1;
                    }
                }
            } else if (mandatory) {
                // 路由失败: 
                more_out = false;
                errno = EHOSTUNREACH;
                return -1;
            }
        }

        int rc = msg_->close();
        errno_assert (rc == 0);
        rc = msg_->init();
        errno_assert (rc == 0);
        return 0;
    }

//    //  Ignore the MORE flag for raw-sock or assert?
//    if (options.raw_sock)
//        msg_->reset_flags(msg_t::more);

    //  Check whether this is the last part of the message.
    more_out = msg_->flags() & msg_t::more ? true : false;

    //  Push the message into the pipe. If there's no out pipe, just drop it.
    if (current_out) {
        // 如果成功地找到路由?
        // 
        // Close the remote connection if user has asked to do so
        // by sending zero length message.
        // Pending messages in the pipe will be dropped (on receiving term- ack)
//        if (raw_sock && msg_->size() == 0) {
//            current_out->terminate(false);
//            int rc = msg_->close();
//            errno_assert (rc == 0);
//            rc = msg_->init();
//            errno_assert (rc == 0);
//            current_out = NULL;
//            return 0;
//        }

        // 直接将msg写入到pipe中
        bool ok = current_out->write(msg_);
        if (unlikely (!ok))
            current_out = NULL;
        else if (!more_out) {
            // 如果没有数据了，则flush, 并且准备下一次路由
            current_out->flush();
            current_out = NULL;
        }
    }
    else {
        // 其他情况，直接扔掉消息
        int rc = msg_->close();
        errno_assert (rc == 0);
    }

    //  Detach the message from the data buffer.
    int rc = msg_->init();
    errno_assert (rc == 0);

    return 0;
}

//
// router如何接受数据呢? identity是什么时候添加的?
//
// <identity, "", msg>
// 空格哪去了呢?
int zmq::router_t::xrecv(msg_t *msg_) {
    if (prefetched) {
        // 4. identity之后的msg已经被预取了, 缓存在: prefetched_msg中
        // 
        if (!identity_sent) {
            // 将id从: preftched_id中拷贝到msg中
            int rc = msg_->move(prefetched_id);
            errno_assert (rc == 0);
            identity_sent = true;
        }  else {
            // 将prefetch的消息返回
            int rc = msg_->move(prefetched_msg);
            errno_assert (rc == 0);
            prefetched = false;
        }
        // 接下来才需要考虑是否为 more_in(有更多的输入)
        more_in = msg_->flags() & msg_t::more ? true : false;
        return 0;
    }

    // 1. 5. 按照fq策略找一个pipe来读取数据
    pipe_t *pipe = NULL;
    int rc = fq.recvpipe(msg_, &pipe);

    //  It's possible that we receive peer's identity. That happens
    //  after reconnection. The current implementation assumes that
    //  the peer always uses the same identity.
    // 2. 可能连续有好多msg都发送 identity
    while (rc == 0 && msg_->is_identity())
        rc = fq.recvpipe(msg_, &pipe);
    
    if (rc != 0)
        return -1;

    zmq_assert (pipe != NULL);
    
    // 现在应该读取了一个正常消息
    
    // 假设之前的more_in 为false
    //  If we are in the middle of reading a message, just return the next part.
    if (more_in)
        // 6. 如果more_in为false, 则下一次的数据处理为: step 3
        //    如果more_in为true, 则下一步的处理为 5, 6
        more_in = msg_->flags() & msg_t::more ? true : false;
    else {
        //  3. We are at the beginning of a message.
        //  Keep the message part we have in the prefetch buffer
        //  and return the ID of the peer instead.
        // 首先我们将消息保存，将identity作为第一个msg part返回
        rc = prefetched_msg.move(*msg_);
        errno_assert (rc == 0);
        prefetched = true;

        blob_t identity = pipe->get_identity();
        rc = msg_->init_size(identity.size());
        errno_assert (rc == 0);
        memcpy(msg_->data(), identity.data(), identity.size());
        msg_->set_flags(msg_t::more);
        identity_sent = true;
    }

    return 0;
}

int zmq::router_t::rollback(void) {
    if (current_out) {
        current_out->rollback();
        current_out = NULL;
        more_out = false;
    }
    return 0;
}

bool zmq::router_t::xhas_in() {
    //  If we are in the middle of reading the messages, there are
    //  definitely more parts available.
    if (more_in)
        return true;

    //  We may already have a message pre-fetched.
    if (prefetched)
        return true;

    //  Try to read the next message.
    //  The message, if read, is kept in the pre-fetch buffer.
    pipe_t *pipe = NULL;
    int rc = fq.recvpipe(&prefetched_msg, &pipe);

    //  It's possible that we receive peer's identity. That happens
    //  after reconnection. The current implementation assumes that
    //  the peer always uses the same identity.
    //  TODO: handle the situation when the peer changes its identity.
    while (rc == 0 && prefetched_msg.is_identity())
        rc = fq.recvpipe(&prefetched_msg, &pipe);

    if (rc != 0)
        return false;

    zmq_assert (pipe != NULL);

    blob_t identity = pipe->get_identity();
    rc = prefetched_id.init_size(identity.size());
    errno_assert (rc == 0);
    memcpy(prefetched_id.data(), identity.data(), identity.size());
    prefetched_id.set_flags(msg_t::more);

    prefetched = true;
    identity_sent = false;

    return true;
}

bool zmq::router_t::xhas_out() {
    //  In theory, ROUTER socket is always ready for writing. Whether actual
    //  attempt to write succeeds depends on whitch pipe the message is going
    //  to be routed to.
    return true;
}

bool zmq::router_t::identify_peer(pipe_t *pipe_) {
    msg_t msg;
    blob_t identity;
    bool ok;

    if (options.raw_sock) { //  Always assign identity for raw-socket
        unsigned char buf[5];
        buf[0] = 0;
        put_uint32(buf + 1, next_peer_id++);
        identity = blob_t(buf, sizeof buf);
    }
    else {
        msg.init();
        ok = pipe_->read(&msg);
        if (!ok)
            return false;

        if (msg.size() == 0) {
            //  Fall back on the auto-generation
            unsigned char buf[5];
            buf[0] = 0;
            put_uint32(buf + 1, next_peer_id++);
            identity = blob_t(buf, sizeof buf);
            msg.close();
        }
        else {
            identity = blob_t((unsigned char *) msg.data(), msg.size());
            outpipes_t::iterator it = outpipes.find(identity);
            msg.close();

            //  Ignore peers with duplicate ID.
            if (it != outpipes.end())
                return false;
        }
    }

    pipe_->set_identity(identity);
    //  Add the record into output pipes lookup table
    outpipe_t outpipe = {pipe_, true};
    ok = outpipes.insert(outpipes_t::value_type(identity, outpipe)).second;
    zmq_assert (ok);

    return true;
}
