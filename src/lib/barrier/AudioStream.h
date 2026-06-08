/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2026
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 */

#pragma once

#include "barrier/AudioChunk.h"
#include "base/String.h"

#include <cstddef>

class AudioSource {
public:
    typedef void (*ChunkCallback)(AudioChunk* chunk, void* context);

    AudioSource();
    ~AudioSource();

    bool start(ChunkCallback callback, void* context, const String& quality = "low");
    void stop();
    bool isRunning() const;

private:
    class Impl;
    Impl* m_impl;
};

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    bool start(const AudioFormat& format);
    void play(const char* data, size_t dataSize);
    void stop();
    bool isRunning() const;

private:
    class Impl;
    Impl* m_impl;
};
