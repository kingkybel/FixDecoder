/*
 * Repository:  https://github.com/kingkybel/FixDecoder
 * File Name:   include/fix_controller.h
 * Description: Session-level FIX controller interface.
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

#ifndef FIXDECODER_FIX_CONTROLLER_H_INCLUDED
#define FIXDECODER_FIX_CONTROLLER_H_INCLUDED

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fix
{

/**
 * @brief Session-level FIX controller for logon, sequencing, and basic validation.
 */
class Controller
{
    public:
    /** @brief Endpoint role in the FIX session. */
    enum class Role
    {
        /** Session initiator that dials and sends initial logon. */
        kInitiator,
        /** Session acceptor that listens and responds to logon. */
        kAcceptor
    };

    /** @brief High-level controller lifecycle state. */
    enum class SessionState
    {
        /** No active session yet. */
        kDisconnected,
        /** Logon has started but counterpart logon is still pending. */
        kAwaitingLogon,
        /** Session is established and application flow is allowed. */
        kEstablished,
        /** Logout has been emitted and shutdown is in progress. */
        kLogoutSent,
        /** Session is terminated. */
        kTerminated
    };

    /** @brief Classification of how an inbound FIX frame was handled. */
    enum class MessageDisposition
    {
        /** Message passed session checks and was accepted. */
        kAccepted,
        /** Message sequence is out-of-sync with expected incoming sequence. */
        kOutOfSync,
        /** Message is malformed or fails structural checks. */
        kGarbled
    };

    /** @brief Controller reaction to an inbound message. */
    struct Action
    {
        /** Final acceptance classification for the processed inbound message. */
        MessageDisposition disposition = MessageDisposition::kAccepted;
        /** Outbound frames that should be sent on the wire in order. */
        std::vector<std::string> outbound_messages;
        /** Human-readable events emitted during processing (for logs/tests). */
        std::vector<std::string> events;
    };

    /** @brief `(tag, value)` FIX field pair used when building custom messages. */
    using Field = std::pair<int, std::string>;

    /**
     * @brief Constructs a controller endpoint with identity and session defaults.
     * @param sender_comp_id Value for tag 49 in outbound messages.
     * @param target_comp_id Value for tag 56 in outbound messages.
     * @param role Initiator or acceptor behavior.
     * @param begin_string FIX BeginString, usually `FIX.4.4`.
     * @param heartbeat_interval_seconds Value for tag 108 in logon.
     */
    Controller(std::string sender_comp_id,
               std::string target_comp_id,
               Role        role,
               std::string begin_string               = "FIX.4.4",
               int         heartbeat_interval_seconds = 30);

    /** @brief Builds a logon (`35=A`) and transitions state to awaiting logon. */
    std::string buildLogon(bool reset_seq_num = false);
    /** @brief Builds a heartbeat (`35=0`), optionally echoing `TestReqID` (112). */
    std::string buildHeartbeat(std::string test_req_id = {});
    /** @brief Builds a test request (`35=1`) with required `TestReqID` (112). */
    std::string buildTestRequest(std::string test_req_id);
    /** @brief Builds a logout (`35=5`) and transitions state to logout-sent. */
    std::string buildLogout(std::string text = {});
    /** @brief Builds an arbitrary application message (`35=<msg_type>`). */
    std::string buildApplicationMessage(std::string msg_type, std::vector<Field> fields = {});
    /** @brief Builds a resend request (`35=2`) for the requested sequence range. */
    std::string buildResendRequest(std::uint32_t begin_seq_no, std::uint32_t end_seq_no = 0);

    /**
     * @brief Splits raw inbound bytes into full SOH-delimited FIX frames.
     * @param incoming_bytes Byte stream chunk from transport.
     * @return Zero or more complete FIX messages ready for `onMessage`.
     */
    std::vector<std::string> consume(std::string_view incoming_bytes);
    /**
     * @brief Processes one complete inbound FIX message and returns generated actions.
     * @param raw_message One full FIX frame.
     * @return Disposition, outbound responses, and emitted events.
     */
    Action onMessage(const std::string &raw_message);

    /** @brief Returns current controller session state. */
    SessionState state() const
    {
        return state_;
    }
    /** @brief Returns next expected inbound `MsgSeqNum` (34). */
    std::uint32_t expectedIncomingSeqNum() const
    {
        return expected_incoming_seq_num_;
    }
    /** @brief Returns next outbound `MsgSeqNum` (34) that will be assigned. */
    std::uint32_t nextOutgoingSeqNum() const
    {
        return next_outgoing_seq_num_;
    }
    /** @brief Advances outbound sequence counter by `delta` (test/simulation helper). */
    void skipOutboundSequence(std::uint32_t delta)
    {
        next_outgoing_seq_num_ += delta;
    }

    private:
    struct ParsedField
    {
        int         tag = 0;
        std::string value;
    };

    struct ParsedMessage
    {
        std::vector<ParsedField> ordered_fields;
        std::string              msg_type;
        std::uint32_t            sequence_number     = 0;
        bool                     has_sequence_number = false;
    };

    enum class ParseErrorCode : std::int32_t
    {
        kNone = 0,
        kMissingFieldTerminator,
        kMalformedTagValue,
        kTagNotNumeric,
        kInvalidMsgSeqNum,
        kMissingMsgType,
        kMissingMsgSeqNum
    };

    struct ParseError
    {
        ParseErrorCode code  = ParseErrorCode::kNone;
        std::int32_t   field = 0;
    };

    std::string buildMessage(std::string msg_type, const std::vector<Field> &fields);
    std::string buildMessageWithSeqNum(std::string msg_type, const std::vector<Field> &fields, std::uint32_t seq_num) const;
    static std::string utcTimestamp();
    static std::string normalize(std::string_view message);
    static bool        parseMessage(const std::string &normalized_message, ParsedMessage &parsed, ParseError &error);
    static std::string parseErrorText(const ParseError &error);
    static bool        validateChecksum(const std::string &normalized_message);
    static bool        validateBodyLength(const std::string &normalized_message);
    static std::string fieldValue(const ParsedMessage &parsed, int tag);

    std::string   sender_comp_id_;
    std::string   target_comp_id_;
    Role          role_ = Role::kInitiator;
    std::string   begin_string_;
    int           heartbeat_interval_seconds_ = 30;
    SessionState  state_                      = SessionState::kDisconnected;
    std::uint32_t expected_incoming_seq_num_  = 1;
    std::uint32_t next_outgoing_seq_num_      = 1;
    bool          logon_sent_                 = false;
    bool          logon_received_             = false;
    std::string   stream_buffer_;
};

}  // namespace fix

#endif  // FIXDECODER_FIX_CONTROLLER_H_INCLUDED
