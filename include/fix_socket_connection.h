/*
 * Repository:  https://github.com/kingkybel/FixDecoder
 * File Name:   include/fix_socket_connection.h
 * Description: Minimal TCP socket wrapper used by demo/controller integration.
 *
 * Copyright (C) 2026 Dieter J Kybelksties <github@kybelksties.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * @date: 2026-02-19
 * @author: Dieter J Kybelksties
 */

#ifndef FIXDECODER_FIX_SOCKET_CONNECTION_H_INCLUDED
#define FIXDECODER_FIX_SOCKET_CONNECTION_H_INCLUDED

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>

namespace fix
{

class SocketConnection
{
    public:
    struct ReceiveResult
    {
        ssize_t bytes_read   = -1;
        int     error_number = 0;
    };

    SocketConnection() = default;
    ~SocketConnection();

    SocketConnection(const SocketConnection &)            = delete;
    SocketConnection &operator=(const SocketConnection &) = delete;

    SocketConnection(SocketConnection &&other) noexcept;
    SocketConnection &operator=(SocketConnection &&other) noexcept;

    bool connectTo(const std::string &host, int port);
    bool listenOn(int port, int backlog = 1);
    std::optional<SocketConnection> acceptClient() const;

    bool          sendAll(std::string_view message) const;
    ReceiveResult receive(void *buffer, std::size_t length, int flags) const;

    bool valid() const;
    int  fd() const;
    void close();

    static std::string errorText(int error_number);

    private:
    explicit SocketConnection(int fd);

    int fd_ = -1;
};

}  // namespace fix

#endif  // FIXDECODER_FIX_SOCKET_CONNECTION_H_INCLUDED
