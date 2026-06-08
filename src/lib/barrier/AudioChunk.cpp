/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2026
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 */

#include "barrier/AudioChunk.h"

#include "barrier/ProtocolUtil.h"
#include "barrier/protocol_types.h"
#include "io/IStream.h"
#include "base/Log.h"

#include <cstring>
#include <sstream>

AudioFormat::AudioFormat() :
    m_sampleRate(0),
    m_channels(0),
    m_bitsPerSample(0)
{
}

AudioFormat::AudioFormat(UInt32 sampleRate, UInt16 channels, UInt16 bitsPerSample) :
    m_sampleRate(sampleRate),
    m_channels(channels),
    m_bitsPerSample(bitsPerSample)
{
}

AudioChunk::AudioChunk(size_t size) :
    Chunk(size)
{
    m_dataSize = size - AUDIO_CHUNK_META_SIZE;
}

AudioChunk*
AudioChunk::start(const AudioFormat& format)
{
    String data = formatToString(format);
    size_t dataSize = data.size();
    AudioChunk* start = new AudioChunk(dataSize + AUDIO_CHUNK_META_SIZE);
    char* chunk = start->m_chunk;
    chunk[0] = kDataStart;
    memcpy(&chunk[1], data.c_str(), dataSize);
    chunk[dataSize + 1] = '\0';

    return start;
}

AudioChunk*
AudioChunk::data(const UInt8* data, size_t dataSize)
{
    AudioChunk* chunk = new AudioChunk(dataSize + AUDIO_CHUNK_META_SIZE);
    char* chunkData = chunk->m_chunk;
    chunkData[0] = kDataChunk;
    memcpy(&chunkData[1], data, dataSize);
    chunkData[dataSize + 1] = '\0';

    return chunk;
}

AudioChunk*
AudioChunk::end()
{
    AudioChunk* end = new AudioChunk(AUDIO_CHUNK_META_SIZE);
    char* chunk = end->m_chunk;
    chunk[0] = kDataEnd;
    chunk[1] = '\0';

    return end;
}

bool
AudioChunk::read(barrier::IStream* stream, UInt8& mark, String& content)
{
    return ProtocolUtil::readf(stream, kMsgDAudioTransfer + 4, &mark, &content);
}

void
AudioChunk::send(barrier::IStream* stream, UInt8 mark, const char* data, size_t dataSize)
{
    String chunk;
    if (data != NULL && dataSize > 0) {
        chunk.assign(data, dataSize);
    }

    switch (mark) {
    case kDataStart:
        LOG((CLOG_DEBUG "sending audio stream start: %s", chunk.c_str()));
        break;

    case kDataChunk:
        LOG((CLOG_DEBUG2 "sending audio chunk: size=%i", chunk.size()));
        break;

    case kDataEnd:
        LOG((CLOG_DEBUG "sending audio stream end"));
        break;
    }

    ProtocolUtil::writef(stream, kMsgDAudioTransfer, mark, &chunk);
}

String
AudioChunk::formatToString(const AudioFormat& format)
{
    std::ostringstream stream;
    stream << format.m_sampleRate << ' '
           << format.m_channels << ' '
           << format.m_bitsPerSample;
    return stream.str();
}

bool
AudioChunk::parseFormat(const String& data, AudioFormat& format)
{
    UInt32 sampleRate = 0;
    UInt16 channels = 0;
    UInt16 bitsPerSample = 0;

    std::istringstream stream(data);
    stream >> sampleRate >> channels >> bitsPerSample;
    if (!stream || sampleRate == 0 || channels == 0 || bitsPerSample == 0) {
        return false;
    }

    format = AudioFormat(sampleRate, channels, bitsPerSample);
    return true;
}
