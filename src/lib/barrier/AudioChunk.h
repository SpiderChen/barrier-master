/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2026
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 */

#pragma once

#include "barrier/Chunk.h"

#include <cstdint>

#include "base/String.h"
#include "common/basic_types.h"

namespace barrier { class IStream; }

#define AUDIO_CHUNK_META_SIZE 2

struct AudioFormat {
    AudioFormat();
    AudioFormat(UInt32 sampleRate, UInt16 channels, UInt16 bitsPerSample);

    UInt32 m_sampleRate;
    UInt16 m_channels;
    UInt16 m_bitsPerSample;
};

class AudioChunk : public Chunk {
public:
    AudioChunk(size_t size);

    static AudioChunk* start(const AudioFormat& format);
    static AudioChunk* data(const UInt8* data, size_t dataSize);
    static AudioChunk* end();

    static bool read(barrier::IStream* stream, UInt8& mark, String& content);
    static void send(barrier::IStream* stream, UInt8 mark, const char* data, size_t dataSize);

    static String formatToString(const AudioFormat& format);
    static bool parseFormat(const String& data, AudioFormat& format);
};
