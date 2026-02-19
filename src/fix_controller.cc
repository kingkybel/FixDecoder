/*
 * Repository:  https://github.com/kingkybel/FixDecoder
 * File Name:   src/fix_controller.cc
 * Description: Session-level FIX controller implementation.
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

#include <cctype>
#include <charconv>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace fix
{

namespace
{

    constexpr char kSoh = 0x01;

    std::string toChecksum(const std::string &message_without_checksum)
    {
        int checksum = 0;
        for(const unsigned char ch: message_without_checksum)
        {
            checksum = (checksum + static_cast<int>(ch)) % 256;
        }

        std::ostringstream out;
        out << std::setw(3) << std::setfill('0') << checksum;
        return out.str();
    }

    std::string toString(const std::uint32_t value)
    {
        return std::to_string(value);
    }

    bool parseUint(const std::string &value, std::uint32_t &out)
    {
        if(value.empty())
        {
            return false;
        }

        std::uint32_t parsed = 0;
        const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if(ec != std::errc{} || ptr != value.data() + value.size())
        {
            return false;
        }
        out = parsed;
        return true;
    }

}  // namespace

Controller::Controller(std::string sender_comp_id,
                       std::string target_comp_id,
                       const Role  role,
                       std::string begin_string,
                       const int   heartbeat_interval_seconds)
: sender_comp_id_(std::move(sender_comp_id))
, target_comp_id_(std::move(target_comp_id))
, role_(role)
, begin_string_(std::move(begin_string))
, heartbeat_interval_seconds_(heartbeat_interval_seconds)
{
}

std::string Controller::buildMessageWithSeqNum(std::string               msg_type,
                                               const std::vector<Field> &fields,
                                               const std::uint32_t       seq_num) const
{
    std::string body;
    body.reserve(256);

    body += "35=" + msg_type + kSoh;
    body += "34=" + toString(seq_num) + kSoh;
    body += "49=" + sender_comp_id_ + kSoh;
    body += "56=" + target_comp_id_ + kSoh;
    body += "52=" + utcTimestamp() + kSoh;

    for(const auto &[tag, value]: fields)
    {
        body += std::to_string(tag);
        body += '=';
        body += value;
        body += kSoh;
    }

    std::string message = "8=" + begin_string_ + kSoh;
    message += "9=" + std::to_string(body.size()) + kSoh;
    message += body;

    const std::string checksum = toChecksum(message);
    message += "10=";
    message += checksum;
    message += kSoh;

    return message;
}

std::string Controller::buildMessage(std::string msg_type, const std::vector<Field> &fields)
{
    return buildMessageWithSeqNum(std::move(msg_type), fields, next_outgoing_seq_num_++);
}

std::string Controller::buildLogon(const bool reset_seq_num)
{
    std::vector<Field> fields;
    fields.emplace_back(98, "0");
    fields.emplace_back(108, std::to_string(heartbeat_interval_seconds_));
    if(reset_seq_num)
    {
        fields.emplace_back(141, "Y");
        expected_incoming_seq_num_ = 1;
        next_outgoing_seq_num_     = 1;
    }

    logon_sent_ = true;
    state_      = SessionState::kAwaitingLogon;
    return buildMessage("A", fields);
}

std::string Controller::buildHeartbeat(std::string test_req_id)
{
    std::vector<Field> fields;
    if(!test_req_id.empty())
    {
        fields.emplace_back(112, std::move(test_req_id));
    }
    return buildMessage("0", fields);
}

std::string Controller::buildTestRequest(std::string test_req_id)
{
    std::vector<Field> fields;
    fields.emplace_back(112, std::move(test_req_id));
    return buildMessage("1", fields);
}

std::string Controller::buildLogout(std::string text)
{
    state_ = SessionState::kLogoutSent;
    std::vector<Field> fields;
    if(!text.empty())
    {
        fields.emplace_back(58, std::move(text));
    }
    return buildMessage("5", fields);
}

std::string Controller::buildApplicationMessage(std::string msg_type, std::vector<Field> fields)
{
    return buildMessage(std::move(msg_type), fields);
}

std::string Controller::buildResendRequest(const std::uint32_t begin_seq_no, const std::uint32_t end_seq_no)
{
    std::vector<Field> fields;
    fields.emplace_back(7, toString(begin_seq_no));
    fields.emplace_back(16, toString(end_seq_no));
    return buildMessage("2", fields);
}

std::string Controller::utcTimestamp()
{
    using namespace std::chrono;

    const auto        now = system_clock::now();
    const auto        ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t t   = system_clock::to_time_t(now);

    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << (tm.tm_year + 1900) << std::setw(2) << (tm.tm_mon + 1) << std::setw(2)
        << tm.tm_mday << '-' << std::setw(2) << tm.tm_hour << ':' << std::setw(2) << tm.tm_min << ':' << std::setw(2)
        << tm.tm_sec << '.' << std::setw(3) << static_cast<int>(ms.count());
    return out.str();
}

std::string Controller::normalize(const std::string_view message)
{
    std::string normalized;
    normalized.reserve(message.size());
    for(const char ch: message)
    {
        normalized.push_back(ch == '|' ? kSoh : ch);
    }
    return normalized;
}

std::vector<std::string> Controller::consume(const std::string_view incoming_bytes)
{
    stream_buffer_ += normalize(incoming_bytes);

    std::vector<std::string> messages;
    while(true)
    {
        const std::size_t begin = stream_buffer_.find("8=");
        if(begin == std::string::npos)
        {
            stream_buffer_.clear();
            break;
        }
        if(begin > 0)
        {
            stream_buffer_.erase(0, begin);
        }

        const std::size_t trailer = stream_buffer_.find(std::string(1, kSoh) + "10=");
        if(trailer == std::string::npos)
        {
            break;
        }
        if(trailer + 8 > stream_buffer_.size())
        {
            break;
        }

        const char c1  = stream_buffer_[trailer + 4];
        const char c2  = stream_buffer_[trailer + 5];
        const char c3  = stream_buffer_[trailer + 6];
        const char end = stream_buffer_[trailer + 7];
        if(!std::isdigit(static_cast<unsigned char>(c1)) || !std::isdigit(static_cast<unsigned char>(c2))
           || !std::isdigit(static_cast<unsigned char>(c3)) || end != kSoh)
        {
            stream_buffer_.erase(0, trailer + 1);
            continue;
        }

        messages.emplace_back(stream_buffer_.substr(0, trailer + 8));
        stream_buffer_.erase(0, trailer + 8);
    }

    return messages;
}

bool Controller::parseMessage(const std::string &normalized_message, ParsedMessage &parsed, ParseError &error)
{
    error = ParseError{};

    ParsedMessage result;

    std::size_t pos = 0;
    while(pos < normalized_message.size())
    {
        const std::size_t end = normalized_message.find(kSoh, pos);
        if(end == std::string::npos)
        {
            error = ParseError{ParseErrorCode::kMissingFieldTerminator, 0};
            return false;
        }
        const std::size_t eq = normalized_message.find('=', pos);
        if(eq == std::string::npos || eq >= end)
        {
            error = ParseError{ParseErrorCode::kMalformedTagValue, 0};
            return false;
        }

        int tag = 0;
        const auto [tag_end, tag_ec] =
         std::from_chars(normalized_message.data() + pos, normalized_message.data() + eq, tag);
        if(tag_ec != std::errc{} || tag_end != normalized_message.data() + eq)
        {
            error = ParseError{ParseErrorCode::kTagNotNumeric, 0};
            return false;
        }

        std::string value = normalized_message.substr(eq + 1, end - eq - 1);
        result.ordered_fields.push_back(ParsedField{tag, std::move(value)});
        pos = end + 1;
    }

    for(const ParsedField &field: result.ordered_fields)
    {
        if(field.tag == 35)
        {
            result.msg_type = field.value;
        }
        else if(field.tag == 34)
        {
            if(!parseUint(field.value, result.sequence_number))
            {
                error = ParseError{ParseErrorCode::kInvalidMsgSeqNum, 34};
                return false;
            }
            result.has_sequence_number = true;
        }
    }

    if(result.msg_type.empty())
    {
        error = ParseError{ParseErrorCode::kMissingMsgType, 35};
        return false;
    }
    if(!result.has_sequence_number)
    {
        error = ParseError{ParseErrorCode::kMissingMsgSeqNum, 34};
        return false;
    }

    parsed = std::move(result);
    return true;
}

std::string Controller::parseErrorText(const ParseError &error)
{
    const auto with_field = [&](const std::string &base) -> std::string
    {
        if(error.field > 0)
        {
            return base + " (tag " + std::to_string(error.field) + ")";
        }
        return base;
    };

    switch(error.code)
    {
        case ParseErrorCode::kMissingFieldTerminator:
            return with_field("Missing SOH-delimited field terminator");
        case ParseErrorCode::kMalformedTagValue:
            return with_field("Malformed tag=value field");
        case ParseErrorCode::kTagNotNumeric:
            return with_field("Tag is not numeric");
        case ParseErrorCode::kInvalidMsgSeqNum:
            return with_field("Invalid MsgSeqNum");
        case ParseErrorCode::kMissingMsgType:
            return with_field("Missing MsgType");
        case ParseErrorCode::kMissingMsgSeqNum:
            return with_field("Missing MsgSeqNum");
        case ParseErrorCode::kNone:
        default:
            return with_field("Malformed FIX message");
    }
}

bool Controller::validateChecksum(const std::string &normalized_message)
{
    const std::size_t trailer = normalized_message.rfind(std::string(1, kSoh) + "10=");
    if(trailer == std::string::npos || trailer + 8 != normalized_message.size())
    {
        return false;
    }

    int expected = 0;
    for(std::size_t i = trailer + 4; i < trailer + 7; ++i)
    {
        if(!std::isdigit(static_cast<unsigned char>(normalized_message[i])))
        {
            return false;
        }
        expected = expected * 10 + (normalized_message[i] - '0');
    }

    int actual = 0;
    for(std::size_t i = 0; i <= trailer; ++i)
    {
        actual = (actual + static_cast<unsigned char>(normalized_message[i])) % 256;
    }
    return actual == expected;
}

bool Controller::validateBodyLength(const std::string &normalized_message)
{
    const std::size_t begin_field_end = normalized_message.find(kSoh);
    if(begin_field_end == std::string::npos)
    {
        return false;
    }

    const std::size_t body_field_end = normalized_message.find(kSoh, begin_field_end + 1);
    if(body_field_end == std::string::npos)
    {
        return false;
    }

    const std::size_t body_len_eq = normalized_message.find('=', begin_field_end + 1);
    if(body_len_eq == std::string::npos || body_len_eq > body_field_end)
    {
        return false;
    }

    if(normalized_message.compare(begin_field_end + 1, 2, "9=") != 0)
    {
        return false;
    }

    std::uint32_t expected_len = 0;
    if(!parseUint(normalized_message.substr(body_len_eq + 1, body_field_end - body_len_eq - 1), expected_len))
    {
        return false;
    }

    const std::size_t trailer = normalized_message.rfind(std::string(1, kSoh) + "10=");
    if(trailer == std::string::npos || trailer < body_field_end)
    {
        return false;
    }

    const std::size_t actual_len = trailer - body_field_end;
    return actual_len == expected_len;
}

std::string Controller::fieldValue(const ParsedMessage &parsed, const int tag)
{
    for(const ParsedField &field: parsed.ordered_fields)
    {
        if(field.tag == tag)
        {
            return field.value;
        }
    }
    return {};
}

Controller::Action Controller::onMessage(const std::string &raw_message)
{
    Action action{};

    const std::string normalized = normalize(raw_message);
    if(!validateBodyLength(normalized) || !validateChecksum(normalized))
    {
        action.disposition = MessageDisposition::kGarbled;
        action.events.emplace_back("garbled_message");
        action.outbound_messages.emplace_back(buildMessage("3", {{58, "Invalid BodyLength or CheckSum"}}));
        return action;
    }

    ParsedMessage parsed;
    ParseError    parse_error;
    if(!parseMessage(normalized, parsed, parse_error))
    {
        action.disposition = MessageDisposition::kGarbled;
        action.events.emplace_back("garbled_message");
        action.outbound_messages.emplace_back(buildMessage("3", {{58, parseErrorText(parse_error)}}));
        return action;
    }

    const std::string sender = fieldValue(parsed, 49);
    const std::string target = fieldValue(parsed, 56);
    if(sender != target_comp_id_ || target != sender_comp_id_)
    {
        action.disposition = MessageDisposition::kGarbled;
        action.events.emplace_back("comp_id_mismatch");
        action.outbound_messages.emplace_back(buildLogout("CompID mismatch"));
        state_ = SessionState::kTerminated;
        return action;
    }

    if(parsed.sequence_number > expected_incoming_seq_num_)
    {
        action.disposition = MessageDisposition::kOutOfSync;
        action.events.emplace_back("sequence_gap");
        action.outbound_messages.emplace_back(buildResendRequest(expected_incoming_seq_num_, 0));
        return action;
    }

    if(parsed.sequence_number < expected_incoming_seq_num_)
    {
        action.disposition = MessageDisposition::kOutOfSync;
        action.events.emplace_back("sequence_too_low");
        action.outbound_messages.emplace_back(buildLogout("MsgSeqNum too low"));
        state_ = SessionState::kTerminated;
        return action;
    }

    ++expected_incoming_seq_num_;

    if(parsed.msg_type == "A")
    {
        logon_received_ = true;
        if(!logon_sent_ && role_ == Role::kAcceptor)
        {
            action.outbound_messages.emplace_back(buildLogon(false));
        }
        state_ = SessionState::kEstablished;
        action.events.emplace_back("logon");
        return action;
    }

    if(!logon_received_ && parsed.msg_type != "5")
    {
        action.disposition = MessageDisposition::kOutOfSync;
        action.events.emplace_back("logon_required");
        action.outbound_messages.emplace_back(buildLogout("Expected Logon"));
        state_ = SessionState::kTerminated;
        return action;
    }

    if(parsed.msg_type == "1")
    {
        action.events.emplace_back("test_request");
        action.outbound_messages.emplace_back(buildHeartbeat(fieldValue(parsed, 112)));
        return action;
    }

    if(parsed.msg_type == "5")
    {
        action.events.emplace_back("logout");
        if(state_ != SessionState::kLogoutSent)
        {
            action.outbound_messages.emplace_back(buildLogout("Logout Ack"));
        }
        state_ = SessionState::kTerminated;
        return action;
    }

    if(parsed.msg_type == "2")
    {
        action.events.emplace_back("resend_request");
        return action;
    }

    if(parsed.msg_type == "4")
    {
        std::uint32_t new_seq = 0;
        if(parseUint(fieldValue(parsed, 36), new_seq) && new_seq >= expected_incoming_seq_num_)
        {
            expected_incoming_seq_num_ = new_seq;
            action.events.emplace_back("sequence_reset");
        }
        return action;
    }

    if(parsed.msg_type == "0")
    {
        action.events.emplace_back("heartbeat");
        return action;
    }

    action.events.emplace_back("application_message");
    return action;
}

}  // namespace fix
