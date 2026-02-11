/*
 * Repository:  https://github.com/kingkybel/FixDecoder
 * File Name:   test/fix_controller_tests.cc
 * Description: GoogleTest suite for FIX controller.
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

#include <gtest/gtest.h>

namespace
{

void deliver(const std::string &wire_message, fix::Controller &receiver, fix::Controller::Action *last_action)
{
    const auto frames = receiver.consume(wire_message);
    ASSERT_FALSE(frames.empty());
    for(const auto &frame : frames)
    {
        *last_action = receiver.onMessage(frame);
    }
}

}  // namespace

TEST(FixControllerTest, PerformsLogonHandshake)
{
    fix::Controller initiator("INITIATOR", "ACCEPTOR", fix::Controller::Role::kInitiator);
    fix::Controller acceptor("ACCEPTOR", "INITIATOR", fix::Controller::Role::kAcceptor);

    const std::string logon = initiator.buildLogon(false);

    fix::Controller::Action acceptor_action;
    deliver(logon, acceptor, &acceptor_action);
    ASSERT_EQ(acceptor_action.disposition, fix::Controller::MessageDisposition::kAccepted);
    ASSERT_EQ(acceptor_action.outbound_messages.size(), 1U);

    fix::Controller::Action initiator_action;
    deliver(acceptor_action.outbound_messages.front(), initiator, &initiator_action);
    ASSERT_EQ(initiator_action.disposition, fix::Controller::MessageDisposition::kAccepted);

    EXPECT_EQ(initiator.state(), fix::Controller::SessionState::kEstablished);
    EXPECT_EQ(acceptor.state(), fix::Controller::SessionState::kEstablished);
}

TEST(FixControllerTest, DetectsOutOfSyncSequenceGap)
{
    fix::Controller initiator("INITIATOR", "ACCEPTOR", fix::Controller::Role::kInitiator);
    fix::Controller acceptor("ACCEPTOR", "INITIATOR", fix::Controller::Role::kAcceptor);

    fix::Controller::Action action;
    deliver(initiator.buildLogon(false), acceptor, &action);
    deliver(action.outbound_messages.front(), initiator, &action);

    initiator.skipOutboundSequence(4);
    const std::string gapped_heartbeat = initiator.buildHeartbeat();

    fix::Controller::Action acceptor_action;
    deliver(gapped_heartbeat, acceptor, &acceptor_action);

    ASSERT_EQ(acceptor_action.disposition, fix::Controller::MessageDisposition::kOutOfSync);
    ASSERT_FALSE(acceptor_action.outbound_messages.empty());

    bool saw_resend_request = false;
    for(const auto &msg : acceptor_action.outbound_messages)
    {
        if(msg.find("35=2") != std::string::npos)
        {
            saw_resend_request = true;
            break;
        }
    }
    EXPECT_TRUE(saw_resend_request);
}

TEST(FixControllerTest, RejectsGarbledMessage)
{
    fix::Controller initiator("INITIATOR", "ACCEPTOR", fix::Controller::Role::kInitiator);
    fix::Controller acceptor("ACCEPTOR", "INITIATOR", fix::Controller::Role::kAcceptor);

    fix::Controller::Action action;
    deliver(initiator.buildLogon(false), acceptor, &action);
    deliver(action.outbound_messages.front(), initiator, &action);

    const std::string garbled = "8=FIX.4.4|9=10|35=0|34=2|10=000|";
    fix::Controller::Action garbled_action;
    deliver(garbled, acceptor, &garbled_action);

    EXPECT_EQ(garbled_action.disposition, fix::Controller::MessageDisposition::kGarbled);
    ASSERT_FALSE(garbled_action.outbound_messages.empty());

    bool saw_reject = false;
    for(const auto &msg : garbled_action.outbound_messages)
    {
        if(msg.find("35=3") != std::string::npos)
        {
            saw_reject = true;
            break;
        }
    }
    EXPECT_TRUE(saw_reject);
}
