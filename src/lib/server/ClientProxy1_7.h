/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2026
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 */

#pragma once

#include "server/ClientProxy1_6.h"

class Server;
class IEventQueue;

//! Proxy for client implementing protocol version 1.7
class ClientProxy1_7 : public ClientProxy1_6 {
public:
    ClientProxy1_7(const std::string& name, barrier::IStream* adoptedStream, Server* server,
                   IEventQueue* events);
    ~ClientProxy1_7();

    virtual void        audioChunkSending(UInt8 mark, const char* data, size_t dataSize);
    virtual void        sendFileClipboard(UInt32 fileCount, const char* info, size_t size);
    virtual bool        parseMessage(const UInt8* code);

private:
    void                fileClipboardReceived();
};
