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
#include "fix_socket_connection.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <utility>
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

std::string longToken(const std::string &prefix, const int index, const int payload_size)
{
    std::string token = prefix + '-' + std::to_string(index) + '-';
    if(payload_size <= 0)
    {
        return token;
    }

    static constexpr std::string_view pattern = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    token.reserve(static_cast<std::size_t>(payload_size));
    for(int i = 0; static_cast<int>(token.size()) < payload_size; ++i)
    {
        token.push_back(pattern[static_cast<std::size_t>(i) % pattern.size()]);
    }
    return token;
}

std::string trimCopy(std::string value)
{
    const auto is_not_space = [](const unsigned char ch) { return !std::isspace(ch); };
    const auto begin        = std::find_if(value.begin(), value.end(), is_not_space);
    const auto end          = std::find_if(value.rbegin(), value.rend(), is_not_space).base();
    if(begin >= end)
    {
        return {};
    }
    return std::string(begin, end);
}

std::vector<std::string> splitCsv(const std::string &input)
{
    std::vector<std::string> tokens;
    std::size_t              start = 0;
    while(start <= input.size())
    {
        const std::size_t comma = input.find(',', start);
        const std::size_t end   = (comma == std::string::npos) ? input.size() : comma;
        const std::string item  = trimCopy(input.substr(start, end - start));
        if(!item.empty())
        {
            tokens.push_back(item);
        }
        if(comma == std::string::npos)
        {
            break;
        }
        start = comma + 1;
    }
    return tokens;
}

std::vector<int> splitCsvInts(const std::string &input)
{
    std::vector<int> values;
    for(const std::string &token: splitCsv(input))
    {
        try
        {
            values.push_back(std::stoi(token));
        }
        catch(...)
        {
        }
    }
    return values;
}

std::string normalizeVersionToken(std::string begin_string)
{
    begin_string.erase(
     std::remove_if(begin_string.begin(), begin_string.end(), [](const unsigned char ch) { return !std::isalnum(ch); }),
     begin_string.end());
    std::transform(begin_string.begin(), begin_string.end(), begin_string.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return begin_string;
}

std::string extractPayloadSeed(const std::string &line)
{
    if(line.empty())
    {
        return {};
    }

    std::string normalized = line;
    for(char &ch: normalized)
    {
        if(ch == 0x01)
        {
            ch = '|';
        }
    }

    std::vector<std::pair<std::string, std::string>> tags;
    std::size_t                                       start = 0;
    while(start <= normalized.size())
    {
        const std::size_t next = normalized.find('|', start);
        const std::size_t end  = (next == std::string::npos) ? normalized.size() : next;
        const std::string token = normalized.substr(start, end - start);
        const std::size_t sep   = token.find('=');
        if(sep != std::string::npos && sep > 0 && sep + 1 < token.size())
        {
            tags.emplace_back(token.substr(0, sep), token.substr(sep + 1));
        }
        if(next == std::string::npos)
        {
            break;
        }
        start = next + 1;
    }

    static const char *preferred_tags[] = {"112", "58", "11", "55", "48", "22", "167", "1"};
    for(const char *tag: preferred_tags)
    {
        for(const auto &[k, v]: tags)
        {
            if(k != tag)
            {
                continue;
            }
            std::string payload = trimCopy(v);
            payload.erase(std::remove(payload.begin(), payload.end(), '|'), payload.end());
            payload.erase(std::remove(payload.begin(), payload.end(), '\x01'), payload.end());
            if(!payload.empty())
            {
                return payload;
            }
        }
    }
    return {};
}

std::vector<std::string> loadPayloadSeeds(const std::string &begin_string,
                                          const std::string &message_file,
                                          const std::string &message_dir)
{
    std::string resolved_file = trimCopy(message_file);
    if(resolved_file.empty() && !trimCopy(message_dir).empty())
    {
        const std::string token = normalizeVersionToken(begin_string);
        if(!token.empty())
        {
            resolved_file = trimCopy(message_dir);
            if(!resolved_file.empty() && resolved_file.back() != '/')
            {
                resolved_file.push_back('/');
            }
            resolved_file += token + "_realistic_200.messages";
        }
    }
    if(resolved_file.empty())
    {
        return {};
    }

    std::ifstream input(resolved_file);
    if(!input.is_open())
    {
        std::cerr << "Warning: unable to open FIX_MESSAGE_FILE '" << resolved_file
                  << "'. Falling back to synthetic payloads.\n";
        return {};
    }

    std::vector<std::string> seeds;
    std::string              line;
    while(std::getline(input, line))
    {
        const std::string payload = extractPayloadSeed(trimCopy(line));
        if(!payload.empty())
        {
            seeds.push_back(payload);
        }
    }

    if(seeds.empty())
    {
        std::cerr << "Warning: no usable payload seeds found in '" << resolved_file
                  << "'. Falling back to synthetic payloads.\n";
    }
    else
    {
        std::cout << "[client] loaded " << seeds.size() << " realistic payload seeds from " << resolved_file << '\n';
    }
    return seeds;
}

std::string buildRequestId(const std::string &scenario,
                           const std::vector<std::string> &payload_seeds,
                           const int index,
                           const int perf_payload_size)
{
    if(payload_seeds.empty())
    {
        return longToken("LOAD", index, perf_payload_size);
    }

    const std::string &seed = payload_seeds[static_cast<std::size_t>(index - 1) % payload_seeds.size()];
    if(scenario == "performance")
    {
        return longToken(seed, index, perf_payload_size);
    }
    return seed + '-' + std::to_string(index);
}

void printSafeFix(const std::string &message);

int runSingleSession(const std::string &role,
                     const std::string &host,
                     const int          port,
                     const std::string &begin_string,
                     const std::string &scenario,
                     const int          conversation_messages,
                     const int          perf_payload_size,
                     const int          runtime_seconds,
                     const std::string &message_file,
                     const std::string &message_dir)
{
    const bool client_role        = (role == "client");
    const bool load_test_scenario = (scenario == "conversation" || scenario == "performance");

    fix::Controller controller(client_role ? "CLIENT" : "EXCHANGE",
                               client_role ? "EXCHANGE" : "CLIENT",
                               client_role ? fix::Controller::Role::kInitiator : fix::Controller::Role::kAcceptor,
                               begin_string);

    fix::SocketConnection connection;
    if(client_role)
    {
        for(int attempt = 0; attempt < 30 && !connection.valid(); ++attempt)
        {
            if(!connection.connectTo(host, port))
            {
                std::this_thread::sleep_for(1s);
            }
        }
        if(!connection.valid())
        {
            std::cerr << "Unable to connect to " << host << ':' << port << '\n';
            return 2;
        }
        const std::string logon = controller.buildLogon(false);
        std::cout << "[client] -> ";
        printSafeFix(logon);
        if(!connection.sendAll(logon))
        {
            return 3;
        }
    }
    else
    {
        fix::SocketConnection listener;
        if(!listener.listenOn(port))
        {
            std::cerr << "Unable to listen on port " << port << '\n';
            return 2;
        }
        auto accepted = listener.acceptClient();
        listener.close();
        if(!accepted.has_value())
        {
            std::cerr << "Accept failed: " << fix::SocketConnection::errorText(errno) << '\n';
            return 2;
        }
        connection = std::move(*accepted);
    }

    bool handshake_complete = false;
    bool scenario_sent      = false;
    bool scenario_complete  = !(load_test_scenario && client_role);
    int  sent_requests      = 0;
    int  received_replies   = 0;
    int  next_request_index = 1;
    auto deadline           = std::chrono::steady_clock::now() + std::chrono::seconds(runtime_seconds);
    const std::vector<std::string> payload_seeds =
     (client_role && load_test_scenario) ? loadPayloadSeeds(begin_string, message_file, message_dir)
                                         : std::vector<std::string>{};
    const bool loop_until_runtime =
     (client_role && load_test_scenario && !payload_seeds.empty()
      && envOrDefaultInt("FIX_LOOP_PAYLOADS_UNTIL_RUNTIME", 0) > 0);
    const int max_in_flight = std::max(1, envOrDefaultInt("FIX_MAX_IN_FLIGHT", 64));

    while(std::chrono::steady_clock::now() < deadline)
    {
        char                                       buffer[2048];
        const fix::SocketConnection::ReceiveResult received = connection.receive(buffer, sizeof(buffer), MSG_DONTWAIT);
        if(received.bytes_read > 0)
        {
            const auto frames =
             controller.consume(std::string_view(buffer, static_cast<std::size_t>(received.bytes_read)));
            for(const auto &frame: frames)
            {
                const auto action = controller.onMessage(frame);
                std::cout << '[' << role << "] <- ";
                printSafeFix(frame);
                for(const auto &event: action.events)
                {
                    std::cout << '[' << role << "] event: " << event << '\n';
                    if(client_role && load_test_scenario && event == "heartbeat")
                    {
                        ++received_replies;
                    }
                }
                for(const auto &out: action.outbound_messages)
                {
                    std::cout << '[' << role << "] -> ";
                    printSafeFix(out);
                    if(!connection.sendAll(out))
                    {
                        return 4;
                    }
                }
            }
        }
        else if(received.bytes_read == 0)
        {
            break;
        }
        else if(received.bytes_read < 0 && received.error_number != EAGAIN && received.error_number != EWOULDBLOCK)
        {
            std::cerr << "recv failed: " << fix::SocketConnection::errorText(received.error_number) << '\n';
            break;
        }

        if(controller.state() == fix::Controller::SessionState::kEstablished)
        {
            handshake_complete = true;
            if(client_role && !scenario_sent)
            {
                if(scenario == "out_of_sync")
                {
                    controller.skipOutboundSequence(4);
                    const std::string out_of_sync_heartbeat = controller.buildHeartbeat();
                    std::cout << "[client] -> ";
                    printSafeFix(out_of_sync_heartbeat);
                    if(!connection.sendAll(out_of_sync_heartbeat))
                    {
                        return 4;
                    }
                }
                else if(scenario == "garbled")
                {
                    const std::string garbled = "8=FIX.4.4|9=10|35=0|34=2|10=000|";
                    std::cout << "[client] -> " << "garbled_frame\n";
                    if(!connection.sendAll(garbled))
                    {
                        return 4;
                    }
                }
                else if(load_test_scenario)
                {
                    const int total_requests = loop_until_runtime ? max_in_flight : conversation_messages;
                    for(int i = 0; i < total_requests; ++i)
                    {
                        const std::string test_req_id =
                         buildRequestId(scenario, payload_seeds, next_request_index, perf_payload_size);
                        const std::string request     = controller.buildTestRequest(test_req_id);
                        std::cout << "[client] -> ";
                        printSafeFix(request);
                        if(!connection.sendAll(request))
                        {
                            return 4;
                        }
                        ++sent_requests;
                        ++next_request_index;
                    }
                }
                scenario_sent = true;
            }
            else if(client_role && load_test_scenario && loop_until_runtime)
            {
                while((sent_requests - received_replies) < max_in_flight
                      && std::chrono::steady_clock::now() < deadline)
                {
                    const std::string test_req_id =
                     buildRequestId(scenario, payload_seeds, next_request_index, perf_payload_size);
                    const std::string request = controller.buildTestRequest(test_req_id);
                    std::cout << "[client] -> ";
                    printSafeFix(request);
                    if(!connection.sendAll(request))
                    {
                        return 4;
                    }
                    ++sent_requests;
                    ++next_request_index;
                }
            }
        }

        if(scenario == "handshake" && handshake_complete)
        {
            scenario_complete = true;
            break;
        }

        if(controller.state() == fix::Controller::SessionState::kTerminated)
        {
            break;
        }

        if(load_test_scenario && scenario_sent && !loop_until_runtime && received_replies >= sent_requests)
        {
            scenario_complete = true;
            break;
        }

        std::this_thread::sleep_for(50ms);
    }

    if(loop_until_runtime && handshake_complete)
    {
        scenario_complete = true;
    }

    const std::string logout = controller.buildLogout("Demo complete");
    (void)connection.sendAll(logout);
    connection.close();

    return (handshake_complete && scenario_complete) ? 0 : 1;
}

int runExchangeServer(const int port, const std::string &begin_string, const int runtime_seconds)
{
    fix::SocketConnection listener;
    if(!listener.listenOn(port, 32))
    {
        std::cerr << "Unable to listen on port " << port << '\n';
        return 2;
    }

    const int flags = ::fcntl(listener.fd(), F_GETFL, 0);
    if(flags >= 0)
    {
        (void)::fcntl(listener.fd(), F_SETFL, flags | O_NONBLOCK);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(runtime_seconds);

    while(std::chrono::steady_clock::now() < deadline)
    {
        auto accepted = listener.acceptClient();
        if(!accepted.has_value())
        {
            std::this_thread::sleep_for(50ms);
            continue;
        }

        fix::Controller       controller("EXCHANGE", "CLIENT", fix::Controller::Role::kAcceptor, begin_string);
        fix::SocketConnection connection = std::move(*accepted);
        auto session_deadline            = std::chrono::steady_clock::now() + std::chrono::seconds(runtime_seconds);

        while(std::chrono::steady_clock::now() < session_deadline)
        {
            char                                       buffer[2048];
            const fix::SocketConnection::ReceiveResult received =
             connection.receive(buffer, sizeof(buffer), MSG_DONTWAIT);
            if(received.bytes_read > 0)
            {
                const auto frames =
                 controller.consume(std::string_view(buffer, static_cast<std::size_t>(received.bytes_read)));
                for(const auto &frame: frames)
                {
                    const auto action = controller.onMessage(frame);
                    std::cout << "[exchange] <- ";
                    printSafeFix(frame);
                    for(const auto &event: action.events)
                    {
                        std::cout << "[exchange] event: " << event << '\n';
                    }
                    for(const auto &out: action.outbound_messages)
                    {
                        std::cout << "[exchange] -> ";
                        printSafeFix(out);
                        if(!connection.sendAll(out))
                        {
                            listener.close();
                            return 4;
                        }
                    }
                }
            }
            else if(received.bytes_read == 0)
            {
                break;
            }
            else if(received.bytes_read < 0 && received.error_number != EAGAIN && received.error_number != EWOULDBLOCK)
            {
                std::cerr << "recv failed: " << fix::SocketConnection::errorText(received.error_number) << '\n';
                break;
            }

            if(controller.state() == fix::Controller::SessionState::kTerminated)
            {
                break;
            }

            std::this_thread::sleep_for(50ms);
        }

        const std::string logout = controller.buildLogout("Demo complete");
        (void)connection.sendAll(logout);
        connection.close();
    }

    listener.close();
    return 0;
}

void printSafeFix(const std::string &message)
{
    std::string pretty = message;
    for(char &ch: pretty)
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
    std::string role = envOrDefault("FIX_ROLE", "exchange");
    if(role == "initiator")
    {
        role = "client";
    }
    else if(role == "acceptor")
    {
        role = "exchange";
    }
    if(role != "client" && role != "exchange")
    {
        std::cerr << "Unsupported FIX_ROLE '" << role << "'. Use client or exchange.\n";
        return 5;
    }

    const std::string host                  = envOrDefault("FIX_HOST", "fix-exchange-1");
    const int         port                  = envOrDefaultInt("FIX_PORT", 5001);
    const std::string begin_string          = envOrDefault("FIX_BEGIN_STRING", "FIX.4.4");
    const std::string hosts_csv             = envOrDefault("FIX_HOSTS", host);
    const std::string ports_csv             = envOrDefault("FIX_PORTS", std::to_string(port));
    const std::string scenario              = envOrDefault("FIX_SCENARIO", "handshake");
    const int         conversation_messages = std::max(0, envOrDefaultInt("FIX_CONVERSATION_MESSAGES", 100));
    const int         perf_payload_size     = std::max(32, envOrDefaultInt("FIX_PERF_PAYLOAD_SIZE", 512));
    const int         runtime_seconds       = std::max(1, envOrDefaultInt("FIX_RUNTIME_SECONDS", 30));
    const std::string message_file          = envOrDefault("FIX_MESSAGE_FILE", "");
    const std::string message_dir           = envOrDefault("FIX_REALISTIC_MESSAGES_DIR", "");

    if(role == "exchange")
    {
        return runExchangeServer(port, begin_string, runtime_seconds);
    }

    const std::vector<std::string> hosts = splitCsv(hosts_csv);
    std::vector<int>               ports = splitCsvInts(ports_csv);
    if(hosts.empty())
    {
        std::cerr << "No valid hosts configured in FIX_HOSTS.\n";
        return 5;
    }
    if(ports.empty())
    {
        ports.push_back(port);
    }
    if(ports.size() == 1 && hosts.size() > 1)
    {
        ports.resize(hosts.size(), ports.front());
    }
    if(ports.size() != hosts.size())
    {
        std::cerr << "FIX_HOSTS and FIX_PORTS must have matching counts, or FIX_PORTS must be a single value.\n";
        return 5;
    }

    int final_rc = 0;
    for(std::size_t i = 0; i < hosts.size(); ++i)
    {
        std::cout << "[client] session " << (i + 1) << '/' << hosts.size() << " -> " << hosts[i] << ':' << ports[i]
                  << '\n';
        const int rc = runSingleSession(role,
                                        hosts[i],
                                        ports[i],
                                        begin_string,
                                        scenario,
                                        conversation_messages,
                                        perf_payload_size,
                                        runtime_seconds,
                                        message_file,
                                        message_dir);
        if(rc != 0)
        {
            final_rc = rc;
            break;
        }
    }
    return final_rc;
}
