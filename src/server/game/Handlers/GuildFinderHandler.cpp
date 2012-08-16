/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "WorldSession.h"
#include "WorldPacket.h"
#include "Object.h"
#include "SharedDefines.h"

void HandleGuildFinderBrowse(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_LF_GUILD_BROWSE");
}

void HandleGuildFinderSetGuildPost(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_LF_GUILD_SET_GUILD_POST");
}

void HandleGuildFinderPostUpdated(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received SMSG_LF_GUILD_POST_UPDATED");
}

void HandleGuildFinderCommandResult(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received SMSG_LF_GUILD_COMMAND_RESULT");
}

void HandlerLFGuildBrowseUpdated(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received SMSG_LF_GUILD_BROWSE_UPDATED");
}

void HandlerLFGuildGetRecruits(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_LF_GUILD_GET_RECRUITS");
}

void HandleLFGuildRecruitListUpdated(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received SMSG_LF_GUILD_RECRUIT_LIST_UPDATED");
}

void HandleLFGuildMembershipListUpdated(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received SMSG_LF_GUILD_MEMBERSHIP_LIST_UPDATED");
}

void HandleLFGuildDeclineRecruit(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_LF_GUILD_DECLINE_RECRUIT");

    ObjectGuid playerGuid;

    playerGuid[1] = recvData.ReadBit(); 
    playerGuid[4] = recvData.ReadBit();
    playerGuid[5] = recvData.ReadBit();
    playerGuid[2] = recvData.ReadBit();
    playerGuid[6] = recvData.ReadBit();
    playerGuid[7] = recvData.ReadBit();
    playerGuid[0] = recvData.ReadBit();
    playerGuid[3] = recvData.ReadBit();

    recvData.ReadByteSeq(playerGuid[5]);
    recvData.ReadByteSeq(playerGuid[7]);
    recvData.ReadByteSeq(playerGuid[2]);
    recvData.ReadByteSeq(playerGuid[3]);
    recvData.ReadByteSeq(playerGuid[4]);
    recvData.ReadByteSeq(playerGuid[1]);
    recvData.ReadByteSeq(playerGuid[0]);
    recvData.ReadByteSeq(playerGuid[6]);
}

void HandleLFGuildRemoveRecruit(WorldPacket & recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Received CMSG_LF_GUILD_REMOVE_RECRUIT");

    ObjectGuid playerGuid;

    playerGuid[0] = recvData.ReadBit(); 
    playerGuid[4] = recvData.ReadBit();
    playerGuid[3] = recvData.ReadBit();
    playerGuid[5] = recvData.ReadBit();
    playerGuid[7] = recvData.ReadBit();
    playerGuid[6] = recvData.ReadBit();
    playerGuid[2] = recvData.ReadBit();
    playerGuid[1] = recvData.ReadBit();

    recvData.ReadByteSeq(playerGuid[4]);
    recvData.ReadByteSeq(playerGuid[0]);
    recvData.ReadByteSeq(playerGuid[3]);
    recvData.ReadByteSeq(playerGuid[6]);
    recvData.ReadByteSeq(playerGuid[5]);
    recvData.ReadByteSeq(playerGuid[1]);
    recvData.ReadByteSeq(playerGuid[2]);
    recvData.ReadByteSeq(playerGuid[7]);
}
