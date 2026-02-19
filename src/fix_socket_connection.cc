/*
 * Repository:  https://github.com/kingkybel/FixDecoder
 * File Name:   src/fix_socket_connection.cc
 * Description: Minimal TCP socket wrapper implementation.
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

#include "fix_socket_connection.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fix
{

SocketConnection::SocketConnection(const int fd)
: fd_(fd)
{
}

SocketConnection::~SocketConnection()
{
    close();
}

SocketConnection::SocketConnection(SocketConnection &&other) noexcept
: fd_(other.fd_)
{
    other.fd_ = -1;
}

SocketConnection &SocketConnection::operator=(SocketConnection &&other) noexcept
{
    if(this == &other)
    {
        return *this;
    }

    close();
    fd_       = other.fd_;
    other.fd_ = -1;
    return *this;
}

bool SocketConnection::connectTo(const std::string &host, const int port)
{
    close();

    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *res = nullptr;
    const int rc  = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    if(rc != 0 || res == nullptr)
    {
        return false;
    }

    for(addrinfo *it = res; it != nullptr; it = it->ai_next)
    {
        const int attempt_fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if(attempt_fd < 0)
        {
            continue;
        }
        if(::connect(attempt_fd, it->ai_addr, it->ai_addrlen) == 0)
        {
            fd_ = attempt_fd;
            break;
        }
        ::close(attempt_fd);
    }

    ::freeaddrinfo(res);
    return fd_ >= 0;
}

bool SocketConnection::listenOn(const int port, const int backlog)
{
    close();

    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if(fd_ < 0)
    {
        return false;
    }

    int reuse = 1;
    (void)::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(static_cast<std::uint16_t>(port));

    if(::bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
    {
        close();
        return false;
    }

    if(::listen(fd_, backlog) != 0)
    {
        close();
        return false;
    }

    return true;
}

std::optional<SocketConnection> SocketConnection::acceptClient() const
{
    if(fd_ < 0)
    {
        return std::nullopt;
    }

    const int accepted = ::accept(fd_, nullptr, nullptr);
    if(accepted < 0)
    {
        return std::nullopt;
    }

    return SocketConnection(accepted);
}

bool SocketConnection::sendAll(const std::string_view message) const
{
    if(fd_ < 0)
    {
        return false;
    }

    std::size_t total = 0;
    while(total < message.size())
    {
        const auto written = ::send(fd_, message.data() + total, message.size() - total, 0);
        if(written <= 0)
        {
            return false;
        }
        total += static_cast<std::size_t>(written);
    }

    return true;
}

SocketConnection::ReceiveResult SocketConnection::receive(void *buffer, const std::size_t length, const int flags) const
{
    if(fd_ < 0)
    {
        return ReceiveResult{-1, EBADF};
    }

    const auto bytes_read = ::recv(fd_, buffer, length, flags);
    return ReceiveResult{bytes_read, bytes_read < 0 ? errno : 0};
}

bool SocketConnection::valid() const
{
    return fd_ >= 0;
}

int SocketConnection::fd() const
{
    return fd_;
}

void SocketConnection::close()
{
    if(fd_ >= 0)
    {
        (void)::close(fd_);
        fd_ = -1;
    }
}

std::string SocketConnection::errorText(const int error_number)
{
    return std::strerror(error_number);
}

}  // namespace fix
