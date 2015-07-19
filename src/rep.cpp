#include "rep.hpp"

zmq::rep_t::rep_t(class ctx_t *parent_, uint32_t tid_, int sid_) :
        router_t(parent_, tid_, sid_),
        sending_reply(false),
        request_begins(true) {
    options.type = ZMQ_REP;
}

zmq::rep_t::~rep_t() {
}

int zmq::rep_t::xsend(msg_t *msg_) {
    //  If we are in the middle of receiving a request, we cannot send reply.
    if (!sending_reply) {
        errno = EFSM;
        return -1;
    }

    bool more = msg_->flags() & msg_t::more ? true : false;

    //  Push message to the reply pipe.
    int rc = router_t::xsend(msg_);
    if (rc != 0)
        return rc;

    //  If the reply is complete flip the FSM back to request receiving state.
    if (!more)
        sending_reply = false;

    return 0;
}

int zmq::rep_t::xrecv(msg_t *msg_) {
    //  If we are in middle of sending a reply, we cannot receive next request.
    if (sending_reply) {
        errno = EFSM;
        return -1;
    }

    //  First thing to do when receiving a request is to copy all the labels
    //  to the reply pipe.
    // 消息的格式
    // <path1, "", path2, "", data>
    // 其中: path1, path2等信息为: traceback stack
    if (request_begins) {
        // REP如何处理信息呢?
        // 碰到消息之后，先读取 TraceBack, 然后直接写入pipes中, router_t:xsend(msg_)
        while (true) {
            int rc = router_t::xrecv(msg_);
            if (rc != 0)
                return rc;

            // 正常数据:
            // 要么没有traceback
            // 
            // 要么有
            if ((msg_->flags() & msg_t::more)) {
                //  Empty message part delimits the traceback stack.
                bool bottom = (msg_->size() == 0);

                //  Push it to the reply pipe.
                rc = router_t::xsend(msg_);
                errno_assert (rc == 0);

                if (bottom)
                    break;
            } else {
                //  If the traceback stack is malformed, discard anything
                //  already sent to pipe (we're at end of invalid message).
                rc = router_t::rollback();
                errno_assert (rc == 0);
            }
        }
        request_begins = false;
    }

    //  Get next message part to return to the user.
    int rc = router_t::xrecv(msg_);
    if (rc != 0)
        return rc;

    //  If whole request is read, flip the FSM to reply-sending state.
    if (!(msg_->flags() & msg_t::more)) {
        sending_reply = true;
        request_begins = true;
    }

    return 0;
}

bool zmq::rep_t::xhas_in() {
    if (sending_reply)
        return false;

    return router_t::xhas_in();
}

bool zmq::rep_t::xhas_out() {
    if (!sending_reply)
        return false;

    return router_t::xhas_out();
}
