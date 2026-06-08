/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2026
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 */

#include "server/ClientProxy1_7.h"

#include "server/Server.h"
#include "barrier/AudioChunk.h"
#include "barrier/ProtocolUtil.h"
#include "barrier/protocol_types.h"
#include "io/IStream.h"

#include <cstring>

ClientProxy1_7::ClientProxy1_7(const std::string& name, barrier::IStream* stream,
                               Server* server, IEventQueue* events) :
    ClientProxy1_6(name, stream, server, events)
{
}

ClientProxy1_7::~ClientProxy1_7()
{
}

void
ClientProxy1_7::audioChunkSending(UInt8 mark, const char* data, size_t dataSize)
{
    AudioChunk::send(getStream(), mark, data, dataSize);
}

void
ClientProxy1_7::sendFileClipboard(UInt32 fileCount, const char* info, size_t size)
{
    std::string data(info, size);
    ProtocolUtil::writef(getStream(), kMsgDFileClipboard, fileCount, &data);
}

bool
ClientProxy1_7::parseMessage(const UInt8* code)
{
    if (memcmp(code, kMsgDFileClipboard, 4) == 0) {
        fileClipboardReceived();
    }
    else {
        return ClientProxy1_6::parseMessage(code);
    }

    return true;
}

void
ClientProxy1_7::fileClipboardReceived()
{
    UInt32 fileNum = 0;
    std::string content;
    ProtocolUtil::readf(getStream(), kMsgDFileClipboard + 4, &fileNum, &content);

    getServer()->fileClipboardReceived(fileNum, content);
}
