/*
 * Repository:  https://github.com/kingkybel/FixDecoder
 * File Name:   src/fix_controller_demo.cc
 * Description: Minimal TCP demo for FIX controller container testing.
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
 * @date: 2026-02-11
 * @author: Dieter J Kybelksties
 */

#include "fix_controller.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{

using namespace std::chrono_literals;

std::string envOrDefault(const char *name, const std::string &fallback)
{
    const char *value = std::getenv(name);
    if(value == nullptr || *value == '\0')
    {
        return fallback;
    }
    return value;
}

int envOrDefaultInt(const char *name, const int fallback)
{
    const char *value = std::getenv(name);
    if(value == nullptr || *value == '\0')
    {
        return fallback;
    }

    try
    {
        return std::stoi(value);
    }
    catch(...)
    {
        return fallback;
    }
}

int listenSocket(const int port)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
    {
        return -1;
    }

    int reuse = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));

    if(::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
    {
        ::close(fd);
        return -1;
    }

    if(::listen(fd, 1) != 0)
    {
        ::close(fd);
        return -1;
    }
    return fd;
}

int connectSocket(const std::string &host, const int port)
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *res = nullptr;
    const int rc = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    if(rc != 0 || res == nullptr)
    {
        return -1;
    }

    int fd = -1;
    for(addrinfo *it = res; it != nullptr; it = it->ai_next)
    {
        fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if(fd < 0)
        {
            continue;
        }
        if(::connect(fd, it->ai_addr, it->ai_addrlen) == 0)
        {
            break;
        }
        ::close(fd);
        fd = -1;
    }

    ::freeaddrinfo(res);
    return fd;
}

bool sendAll(const int fd, const std::string &message)
{
    std::size_t total = 0;
    while(total < message.size())
    {
        const auto written = ::send(fd, message.data() + total, message.size() - total, 0);
        if(written <= 0)
        {
            return false;
        }
        total += static_cast<std::size_t>(written);
    }
    return true;
}

void printSafeFix(const std::string &message)
{
    std::string pretty = message;
    for(char &ch : pretty)
    {
        if(ch == 0x01)
        {
            ch = '|';
        }
    }
    std::cout << pretty << '\n';
}

}  // namespace

int main()
{
    const std::string role = envOrDefault("FIX_ROLE", "acceptor");
    const std::string host = envOrDefault("FIX_HOST", "fix-acceptor");
    const int port = envOrDefaultInt("FIX_PORT", 5001);
    const std::string scenario = envOrDefault("FIX_SCENARIO", "handshake");

    const bool initiator = (role == "initiator");

    fix::Controller controller(initiator ? "INITIATOR" : "ACCEPTOR",
                               initiator ? "ACCEPTOR" : "INITIATOR",
                               initiator ? fix::Controller::Role::kInitiator : fix::Controller::Role::kAcceptor);

    int fd = -1;
    if(initiator)
    {
        for(int attempt = 0; attempt < 30 && fd < 0; ++attempt)
        {
            fd = connectSocket(host, port);
            if(fd < 0)
            {
                std::this_thread::sleep_for(1s);
            }
        }
        if(fd < 0)
        {
            std::cerr << "Unable to connect to " << host << ':' << port << '\n';
            return 2;
        }
        const std::string logon = controller.buildLogon(false);
        std::cout << "[initiator] -> ";
        printSafeFix(logon);
        if(!sendAll(fd, logon))
        {
            return 3;
        }
    }
    else
    {
        const int listener = listenSocket(port);
        if(listener < 0)
        {
            std::cerr << "Unable to listen on port " << port << '\n';
            return 2;
        }
        fd = ::accept(listener, nullptr, nullptr);
        ::close(listener);
        if(fd < 0)
        {
            std::cerr << "Accept failed: " << std::strerror(errno) << '\n';
            return 2;
        }
    }

    bool handshake_complete = false;
    bool scenario_sent = false;
    auto deadline = std::chrono::steady_clock::now() + 12s;

    while(std::chrono::steady_clock::now() < deadline)
    {
        char buffer[2048];
        const auto n = ::recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if(n > 0)
        {
            const auto frames = controller.consume(std::string_view(buffer, static_cast<std::size_t>(n)));
            for(const auto &frame : frames)
            {
                const auto action = controller.onMessage(frame);
                std::cout << '[' << role << "] <- ";
                printSafeFix(frame);
                for(const auto &event : action.events)
                {
                    std::cout << '[' << role << "] event: " << event << '\n';
                }
                for(const auto &out : action.outbound_messages)
                {
                    std::cout << '[' << role << "] -> ";
                    printSafeFix(out);
                    if(!sendAll(fd, out))
                    {
                        ::close(fd);
                        return 4;
                    }
                }
            }
        }
        else if(n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            std::cerr << "recv failed: " << std::strerror(errno) << '\n';
            break;
        }

        if(controller.state() == fix::Controller::SessionState::kEstablished)
        {
            handshake_complete = true;
            if(initiator && !scenario_sent)
            {
                if(scenario == "out_of_sync")
                {
                    controller.skipOutboundSequence(4);
                    const std::string out_of_sync_heartbeat = controller.buildHeartbeat();
                    std::cout << "[initiator] -> ";
                    printSafeFix(out_of_sync_heartbeat);
                    if(!sendAll(fd, out_of_sync_heartbeat))
                    {
                        ::close(fd);
                        return 4;
                    }
                }
                else if(scenario == "garbled")
                {
                    const std::string garbled = "8=FIX.4.4|9=10|35=0|34=2|10=000|";
                    std::cout << "[initiator] -> " << "garbled_frame\n";
                    if(!sendAll(fd, garbled))
                    {
                        ::close(fd);
                        return 4;
                    }
                }
                scenario_sent = true;
            }
        }

        std::this_thread::sleep_for(50ms);
    }

    const std::string logout = controller.buildLogout("Demo complete");
    (void)sendAll(fd, logout);
    ::close(fd);

    return handshake_complete ? 0 : 1;
}
