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

#ifndef __ZMQ_PROXY_HPP_INCLUDED__
#define __ZMQ_PROXY_HPP_INCLUDED__

namespace zmq {
    int proxy(
            class socket_base_t *frontend_,
            class socket_base_t *backend_,
            class socket_base_t *capture_,
            class socket_base_t *control_ = NULL); // backward compatibility without this argument);
}

#endif
