/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2026
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 */

#include "barrier/AudioStream.h"

#include "arch/Arch.h"
#include "base/Log.h"
#include "mt/Thread.h"
#include "common/common.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

struct AudioQualityProfile {
    const char* m_name;
    UInt32 m_sampleRate;
    UInt16 m_channels;
};

static const UInt16 kSharedAudioBitsPerSample = 16;
static const size_t kSharedAudioPacketsPerSecond = 10;
static const double kSharedAudioMaxSendLeadSeconds = 0.20;
static const double kSharedAudioIdlePollSeconds = 0.500;
static const double kSharedAudioIdleResetSeconds = 0.35;
static const UInt32 kSharedAudioDropLogInterval = 50;

static const AudioQualityProfile kAudioQualityProfiles[] = {
    { "low", 16000, 1 },
    { "medium", 32000, 2 },
    { "high", 48000, 2 }
};

static const AudioQualityProfile&
defaultAudioQualityProfile()
{
    return kAudioQualityProfiles[0];
}

static const AudioQualityProfile&
audioQualityProfile(const String& quality)
{
    for (size_t index = 0; index < sizeof(kAudioQualityProfiles) / sizeof(kAudioQualityProfiles[0]); ++index) {
        if (quality == kAudioQualityProfiles[index].m_name) {
            return kAudioQualityProfiles[index];
        }
    }

    if (!quality.empty()) {
        LOG((CLOG_INFO "unknown audio quality '%s', using low", quality.c_str()));
    }
    return defaultAudioQualityProfile();
}

static AudioFormat
audioFormatForQuality(const String& quality)
{
    const AudioQualityProfile& profile = audioQualityProfile(quality);
    return AudioFormat(profile.m_sampleRate, profile.m_channels,
                       kSharedAudioBitsPerSample);
}

static size_t
audioPacketBytes(const AudioFormat& format)
{
    return static_cast<size_t>(format.m_sampleRate) *
           static_cast<size_t>(format.m_channels) *
           static_cast<size_t>(format.m_bitsPerSample / 8) /
           kSharedAudioPacketsPerSecond;
}

} // namespace

#if SYSAPI_WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <mmsystem.h>

static const GUID kAudioPcmGuid =
    {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID kAudioFloatGuid =
    {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static bool
guidEquals(const GUID& left, const GUID& right)
{
    return std::memcmp(&left, &right, sizeof(GUID)) == 0;
}

static bool
isFloatFormat(const WAVEFORMATEX* format)
{
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE* extensible =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return guidEquals(extensible->SubFormat, kAudioFloatGuid);
    }
    return false;
}

static bool
isPcmFormat(const WAVEFORMATEX* format)
{
    if (format->wFormatTag == WAVE_FORMAT_PCM) {
        return true;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE* extensible =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return guidEquals(extensible->SubFormat, kAudioPcmGuid);
    }
    return false;
}

static SInt16
clampSample(double sample)
{
    if (sample < -1.0) {
        sample = -1.0;
    }
    else if (sample > 1.0) {
        sample = 1.0;
    }
    return static_cast<SInt16>(sample * 32767.0);
}

static SInt16
readPcmSample(const UInt8* frame, UInt16 bitsPerSample)
{
    switch (bitsPerSample) {
    case 16: {
        SInt16 sample = 0;
        std::memcpy(&sample, frame, sizeof(sample));
        return sample;
    }

    case 24: {
        SInt32 sample = (static_cast<SInt32>(frame[0]) |
                         (static_cast<SInt32>(frame[1]) << 8) |
                         (static_cast<SInt32>(frame[2]) << 16));
        if ((sample & 0x00800000) != 0) {
            sample |= 0xff000000;
        }
        return static_cast<SInt16>(sample >> 8);
    }

    case 32: {
        SInt32 sample = 0;
        std::memcpy(&sample, frame, sizeof(sample));
        return static_cast<SInt16>(sample >> 16);
    }
    }

    return 0;
}

static void
writeInt16(std::vector<UInt8>& output, SInt16 sample)
{
    output.push_back(static_cast<UInt8>(sample & 0xff));
    output.push_back(static_cast<UInt8>((sample >> 8) & 0xff));
}

static bool
isSilentPcm(const std::vector<UInt8>& output)
{
    if (output.empty()) {
        return false;
    }

    for (size_t index = 0; index < output.size(); ++index) {
        if (output[index] != 0) {
            return false;
        }
    }

    return true;
}

static SInt16
readChannelSample(const UInt8* frame, const WAVEFORMATEX* format, UInt16 channel)
{
    const UInt16 inputBytesPerSample = format->wBitsPerSample / 8;
    const bool inputIsFloat = isFloatFormat(format);
    const bool inputIsPcm = isPcmFormat(format);

    const UInt8* sampleData = frame + channel * inputBytesPerSample;
    if (inputIsFloat && format->wBitsPerSample == 32) {
        float floatSample = 0.0f;
        std::memcpy(&floatSample, sampleData, sizeof(floatSample));
        return clampSample(floatSample);
    }
    else if (inputIsPcm) {
        return readPcmSample(sampleData, format->wBitsPerSample);
    }

    return 0;
}

static SInt16
readMonoSample(const UInt8* frame, const WAVEFORMATEX* format, bool silent)
{
    if (silent || format->nChannels == 0) {
        return 0;
    }

    SInt32 sum = 0;
    for (UInt16 channel = 0; channel < format->nChannels; ++channel) {
        sum += readChannelSample(frame, format, channel);
    }

    return static_cast<SInt16>(sum / format->nChannels);
}

static void
writeSharedFrame(std::vector<UInt8>& output, const UInt8* frame,
                 const WAVEFORMATEX* format, UInt16 outputChannels)
{
    if (outputChannels <= 1) {
        writeInt16(output, readMonoSample(frame, format, false));
        return;
    }

    if (format->nChannels == 0) {
        for (UInt16 channel = 0; channel < outputChannels; ++channel) {
            writeInt16(output, 0);
        }
        return;
    }

    for (UInt16 channel = 0; channel < outputChannels; ++channel) {
        UInt16 inputChannel = channel;
        if (inputChannel >= format->nChannels) {
            inputChannel = static_cast<UInt16>(format->nChannels - 1);
        }
        writeInt16(output, readChannelSample(frame, format, inputChannel));
    }
}

static void
convertToSharedPcm16(const UInt8* input, UINT32 frames, const WAVEFORMATEX* format,
                     const AudioFormat& outputFormat, double& nextOutputFrame,
                     std::vector<UInt8>& output, bool silent)
{
    output.clear();
    if (format->nSamplesPerSec == 0 || format->nBlockAlign == 0 ||
        outputFormat.m_sampleRate == 0 || outputFormat.m_channels == 0 ||
        outputFormat.m_bitsPerSample != 16) {
        return;
    }
    if (silent) {
        nextOutputFrame = 0.0;
        return;
    }

    const double inputFramesPerOutputFrame =
        static_cast<double>(format->nSamplesPerSec) /
        static_cast<double>(outputFormat.m_sampleRate);
    const double estimatedOutputFrames =
        inputFramesPerOutputFrame > 0.0 ?
        static_cast<double>(frames) / inputFramesPerOutputFrame : 0.0;
    output.reserve(static_cast<size_t>(estimatedOutputFrames + 1.0) *
                   outputFormat.m_channels * sizeof(SInt16));

    for (UINT32 frameIndex = 0; frameIndex < frames; ++frameIndex) {
        const UInt8* frame = input + frameIndex * format->nBlockAlign;
        while (nextOutputFrame <= frameIndex) {
            if (!silent) {
                writeSharedFrame(output, frame, format, outputFormat.m_channels);
            }
            nextOutputFrame += inputFramesPerOutputFrame;
        }
    }

    nextOutputFrame -= frames;
}

class AudioSource::Impl {
public:
    typedef HRESULT (WINAPI *CoInitializeExProc)(LPVOID, DWORD);
    typedef void (WINAPI *CoUninitializeProc)();
    typedef HRESULT (WINAPI *CoCreateInstanceProc)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
    typedef void (WINAPI *CoTaskMemFreeProc)(LPVOID);

    Impl() :
        m_thread(NULL),
        m_running(false),
        m_stop(false),
        m_callback(NULL),
        m_context(NULL),
        m_outputFormat(audioFormatForQuality(""))
    {
    }

    ~Impl()
    {
        stop();
    }

    bool start(AudioSource::ChunkCallback callback, void* context, const String& quality)
    {
        if (m_running) {
            return true;
        }
        if (callback == NULL) {
            return false;
        }

        const AudioQualityProfile& profile = audioQualityProfile(quality);
        m_outputFormat = AudioFormat(profile.m_sampleRate, profile.m_channels,
                                     kSharedAudioBitsPerSample);
        LOG((CLOG_INFO "server audio sharing quality: %s (%u Hz, %u channels, %u-bit)",
             profile.m_name, m_outputFormat.m_sampleRate, m_outputFormat.m_channels,
             m_outputFormat.m_bitsPerSample));

        m_callback = callback;
        m_context = context;
        m_stop = false;
        m_thread = new Thread([this]() { run(); });
        m_running = true;
        return true;
    }

    void stop()
    {
        m_stop = true;
        if (m_thread != NULL) {
            if (!m_thread->wait(2.0)) {
                m_thread->cancel();
                m_thread->wait(1.0);
            }
            delete m_thread;
            m_thread = NULL;
        }
        m_running = false;
    }

    bool isRunning() const
    {
        return m_running;
    }

private:
    void run()
    {
        HMODULE ole32 = LoadLibraryA("ole32.dll");
        if (ole32 == NULL) {
            LOG((CLOG_ERR "server audio sharing unavailable: failed to load ole32.dll"));
            notifyStreamEnded();
            m_running = false;
            return;
        }

        CoInitializeExProc coInitializeEx =
            reinterpret_cast<CoInitializeExProc>(GetProcAddress(ole32, "CoInitializeEx"));
        CoUninitializeProc coUninitialize =
            reinterpret_cast<CoUninitializeProc>(GetProcAddress(ole32, "CoUninitialize"));
        CoCreateInstanceProc coCreateInstance =
            reinterpret_cast<CoCreateInstanceProc>(GetProcAddress(ole32, "CoCreateInstance"));
        CoTaskMemFreeProc coTaskMemFree =
            reinterpret_cast<CoTaskMemFreeProc>(GetProcAddress(ole32, "CoTaskMemFree"));

        if (coInitializeEx == NULL || coUninitialize == NULL ||
            coCreateInstance == NULL || coTaskMemFree == NULL) {
            LOG((CLOG_ERR "server audio sharing unavailable: failed to load COM entry points"));
            FreeLibrary(ole32);
            notifyStreamEnded();
            m_running = false;
            return;
        }

        HRESULT hr = coInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            LOG((CLOG_ERR "server audio sharing unavailable: CoInitializeEx failed 0x%08x", hr));
            FreeLibrary(ole32);
            notifyStreamEnded();
            m_running = false;
            return;
        }

        IMMDeviceEnumerator* enumerator = NULL;
        IMMDevice* device = NULL;
        IAudioClient* audioClient = NULL;
        IAudioCaptureClient* captureClient = NULL;
        WAVEFORMATEX* mixFormat = NULL;
        bool audioClientStarted = false;

        do {
            hr = coCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
            if (FAILED(hr)) {
                LOG((CLOG_ERR "server audio sharing unavailable: CoCreateInstance failed 0x%08x", hr));
                break;
            }

            hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
            if (FAILED(hr)) {
                LOG((CLOG_ERR "server audio sharing unavailable: default render endpoint failed 0x%08x", hr));
                break;
            }

            hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
                                  reinterpret_cast<void**>(&audioClient));
            if (FAILED(hr)) {
                LOG((CLOG_ERR "server audio sharing unavailable: audio client activation failed 0x%08x", hr));
                break;
            }

            hr = audioClient->GetMixFormat(&mixFormat);
            if (FAILED(hr)) {
                LOG((CLOG_ERR "server audio sharing unavailable: GetMixFormat failed 0x%08x", hr));
                break;
            }

            const REFERENCE_TIME bufferDuration = 10000000;
            hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                         AUDCLNT_STREAMFLAGS_LOOPBACK,
                                         bufferDuration, 0, mixFormat, NULL);
            if (FAILED(hr)) {
                LOG((CLOG_ERR "server audio sharing unavailable: loopback Initialize failed 0x%08x", hr));
                break;
            }

            hr = audioClient->GetService(__uuidof(IAudioCaptureClient),
                                         reinterpret_cast<void**>(&captureClient));
            if (FAILED(hr)) {
                LOG((CLOG_ERR "server audio sharing unavailable: GetService failed 0x%08x", hr));
                break;
            }

            hr = audioClient->Start();
            if (FAILED(hr)) {
                LOG((CLOG_ERR "server audio sharing unavailable: audio client Start failed 0x%08x", hr));
                break;
            }
            audioClientStarted = true;

            AudioFormat outputFormat = m_outputFormat;
            captureLoop(captureClient, mixFormat, outputFormat);
        } while (false);

        if (audioClientStarted) {
            audioClient->Stop();
        }
        if (mixFormat != NULL) {
            coTaskMemFree(mixFormat);
        }
        if (captureClient != NULL) {
            captureClient->Release();
        }
        if (audioClient != NULL) {
            audioClient->Release();
        }
        if (device != NULL) {
            device->Release();
        }
        if (enumerator != NULL) {
            enumerator->Release();
        }

        coUninitialize();
        FreeLibrary(ole32);
        notifyStreamEnded();
        m_running = false;
    }

    void notifyStreamEnded()
    {
        if (!m_stop && m_callback != NULL) {
            m_callback(AudioChunk::end(), m_context);
        }
    }

    void captureLoop(IAudioCaptureClient* captureClient, const WAVEFORMATEX* mixFormat,
                     const AudioFormat& outputFormat)
    {
        const size_t packetBytes = audioPacketBytes(outputFormat);
        if (packetBytes == 0) {
            return;
        }

        std::vector<UInt8> output;
        std::vector<UInt8> pendingOutput;
        double nextOutputFrame = 0.0;
        UINT32 packetFrames = 0;
        UInt32 droppedPackets = 0;
        double nextSendTime = ARCH->time();
        double lastAudioPacketTime = nextSendTime;
        bool streamStarted = false;

        while (!m_stop) {
            HRESULT hr = captureClient->GetNextPacketSize(&packetFrames);
            if (FAILED(hr)) {
                LOG((CLOG_ERR "server audio sharing stopped: GetNextPacketSize failed 0x%08x", hr));
                return;
            }

            if (packetFrames == 0) {
                // Silence and paused sources are normal; keep the stream open
                // so playback resumes without forcing a protocol-level end.
                const double now = ARCH->time();
                if (now - lastAudioPacketTime >= kSharedAudioIdleResetSeconds) {
                    pendingOutput.clear();
                    nextOutputFrame = 0.0;
                    nextSendTime = now;
                }
                ARCH->sleep(kSharedAudioIdlePollSeconds);
                continue;
            }

            while (packetFrames != 0 && !m_stop) {
                BYTE* data = NULL;
                UINT32 framesAvailable = 0;
                DWORD flags = 0;

                hr = captureClient->GetBuffer(&data, &framesAvailable, &flags, NULL, NULL);
                if (FAILED(hr)) {
                    LOG((CLOG_ERR "server audio sharing stopped: GetBuffer failed 0x%08x", hr));
                    return;
                }

                bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                convertToSharedPcm16(data, framesAvailable, mixFormat, outputFormat,
                                     nextOutputFrame, output, silent);
                if (isSilentPcm(output)) {
                    output.clear();
                    silent = true;
                }
                if (!output.empty()) {
                    const double now = ARCH->time();
                    if (now - lastAudioPacketTime >= kSharedAudioIdleResetSeconds) {
                        pendingOutput.clear();
                        nextSendTime = now;
                    }
                    lastAudioPacketTime = now;
                    pendingOutput.insert(pendingOutput.end(), output.begin(), output.end());
                    while (pendingOutput.size() >= packetBytes && !m_stop) {
                        const double now = ARCH->time();
                        if (nextSendTime > now + kSharedAudioMaxSendLeadSeconds) {
                            pendingOutput.erase(pendingOutput.begin(),
                                                pendingOutput.begin() + packetBytes);
                            ++droppedPackets;
                            if ((droppedPackets % kSharedAudioDropLogInterval) == 1) {
                                LOG((CLOG_NOTE "server audio sharing dropped stale queued audio"));
                            }
                            continue;
                        }

                        if (!streamStarted) {
                            m_callback(AudioChunk::start(outputFormat), m_context);
                            streamStarted = true;
                        }
                        m_callback(AudioChunk::data(&pendingOutput[0],
                                                    packetBytes),
                                   m_context);
                        pendingOutput.erase(pendingOutput.begin(),
                                            pendingOutput.begin() + packetBytes);
                        if (nextSendTime < now) {
                            nextSendTime = now;
                        }
                        nextSendTime += 1.0 / kSharedAudioPacketsPerSecond;
                    }
                }
                else if (silent) {
                    const double now = ARCH->time();
                    if (now - lastAudioPacketTime >= kSharedAudioIdleResetSeconds) {
                        pendingOutput.clear();
                        nextOutputFrame = 0.0;
                        nextSendTime = now;
                    }
                }

                captureClient->ReleaseBuffer(framesAvailable);

                hr = captureClient->GetNextPacketSize(&packetFrames);
                if (FAILED(hr)) {
                    LOG((CLOG_ERR "server audio sharing stopped: GetNextPacketSize failed 0x%08x", hr));
                    return;
                }
            }
        }
    }

private:
    Thread* m_thread;
    std::atomic<bool> m_running;
    std::atomic<bool> m_stop;
    AudioSource::ChunkCallback m_callback;
    void* m_context;
    AudioFormat m_outputFormat;
};

class AudioPlayer::Impl {
public:
    typedef MMRESULT (WINAPI *WaveOutOpenProc)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
    typedef MMRESULT (WINAPI *WaveOutPrepareHeaderProc)(HWAVEOUT, LPWAVEHDR, UINT);
    typedef MMRESULT (WINAPI *WaveOutUnprepareHeaderProc)(HWAVEOUT, LPWAVEHDR, UINT);
    typedef MMRESULT (WINAPI *WaveOutWriteProc)(HWAVEOUT, LPWAVEHDR, UINT);
    typedef MMRESULT (WINAPI *WaveOutResetProc)(HWAVEOUT);
    typedef MMRESULT (WINAPI *WaveOutCloseProc)(HWAVEOUT);

    struct PendingBuffer {
        WAVEHDR* m_header;
        char* m_data;
    };

    enum {
        kMaxPlaybackQueueBuffers = 6
    };

    Impl() :
        m_winmm(NULL),
        m_waveOut(NULL),
        m_waveOutOpen(NULL),
        m_waveOutPrepareHeader(NULL),
        m_waveOutUnprepareHeader(NULL),
        m_waveOutWrite(NULL),
        m_waveOutReset(NULL),
        m_waveOutClose(NULL),
        m_running(false)
    {
    }

    ~Impl()
    {
        stop();
        if (m_winmm != NULL) {
            FreeLibrary(m_winmm);
        }
    }

    bool start(const AudioFormat& format)
    {
        stop();

        if (!loadWinmm()) {
            return false;
        }

        WAVEFORMATEX waveFormat;
        std::memset(&waveFormat, 0, sizeof(waveFormat));
        waveFormat.wFormatTag = WAVE_FORMAT_PCM;
        waveFormat.nChannels = format.m_channels;
        waveFormat.nSamplesPerSec = format.m_sampleRate;
        waveFormat.wBitsPerSample = format.m_bitsPerSample;
        waveFormat.nBlockAlign = static_cast<WORD>((waveFormat.nChannels * waveFormat.wBitsPerSample) / 8);
        waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

        MMRESULT result = m_waveOutOpen(&m_waveOut, WAVE_MAPPER, &waveFormat, 0, 0, CALLBACK_NULL);
        if (result != MMSYSERR_NOERROR) {
            LOG((CLOG_ERR "client audio playback unavailable: waveOutOpen failed %u", result));
            m_waveOut = NULL;
            return false;
        }

        m_running = true;
        LOG((CLOG_INFO "client audio playback started: %u Hz, %u channels, %u-bit",
             format.m_sampleRate, format.m_channels, format.m_bitsPerSample));
        return true;
    }

    bool play(const char* data, size_t dataSize)
    {
        if (!m_running || data == NULL || dataSize == 0) {
            return false;
        }

        cleanupFinished();
        if (m_pending.size() >= kMaxPlaybackQueueBuffers) {
            LOG((CLOG_NOTE "client audio playback queue is stale; reopening device"));
            return false;
        }

        char* buffer = new char[dataSize];
        std::memcpy(buffer, data, dataSize);

        WAVEHDR* header = new WAVEHDR;
        std::memset(header, 0, sizeof(WAVEHDR));
        header->lpData = buffer;
        header->dwBufferLength = static_cast<DWORD>(dataSize);

        MMRESULT result = m_waveOutPrepareHeader(m_waveOut, header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            delete[] buffer;
            delete header;
            LOG((CLOG_ERR "client audio playback failed: waveOutPrepareHeader failed %u", result));
            stop();
            return false;
        }

        result = m_waveOutWrite(m_waveOut, header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            m_waveOutUnprepareHeader(m_waveOut, header, sizeof(WAVEHDR));
            delete[] buffer;
            delete header;
            LOG((CLOG_ERR "client audio playback failed: waveOutWrite failed %u", result));
            stop();
            return false;
        }

        PendingBuffer pending;
        pending.m_header = header;
        pending.m_data = buffer;
        m_pending.push_back(pending);
        return true;
    }

    void stop()
    {
        if (m_waveOut != NULL) {
            m_waveOutReset(m_waveOut);
            cleanupAll();
            m_waveOutClose(m_waveOut);
            m_waveOut = NULL;
        }
        m_running = false;
    }

    bool isRunning() const
    {
        return m_running;
    }

private:
    bool loadWinmm()
    {
        if (m_winmm != NULL) {
            return true;
        }

        m_winmm = LoadLibraryA("winmm.dll");
        if (m_winmm == NULL) {
            LOG((CLOG_ERR "client audio playback unavailable: failed to load winmm.dll"));
            return false;
        }

        m_waveOutOpen = reinterpret_cast<WaveOutOpenProc>(GetProcAddress(m_winmm, "waveOutOpen"));
        m_waveOutPrepareHeader = reinterpret_cast<WaveOutPrepareHeaderProc>(GetProcAddress(m_winmm, "waveOutPrepareHeader"));
        m_waveOutUnprepareHeader = reinterpret_cast<WaveOutUnprepareHeaderProc>(GetProcAddress(m_winmm, "waveOutUnprepareHeader"));
        m_waveOutWrite = reinterpret_cast<WaveOutWriteProc>(GetProcAddress(m_winmm, "waveOutWrite"));
        m_waveOutReset = reinterpret_cast<WaveOutResetProc>(GetProcAddress(m_winmm, "waveOutReset"));
        m_waveOutClose = reinterpret_cast<WaveOutCloseProc>(GetProcAddress(m_winmm, "waveOutClose"));

        if (m_waveOutOpen == NULL || m_waveOutPrepareHeader == NULL ||
            m_waveOutUnprepareHeader == NULL || m_waveOutWrite == NULL ||
            m_waveOutReset == NULL || m_waveOutClose == NULL) {
            LOG((CLOG_ERR "client audio playback unavailable: failed to load winmm entry points"));
            FreeLibrary(m_winmm);
            m_winmm = NULL;
            return false;
        }

        return true;
    }

    void cleanupFinished()
    {
        std::vector<PendingBuffer>::iterator index = m_pending.begin();
        while (index != m_pending.end()) {
            if ((index->m_header->dwFlags & WHDR_DONE) == 0) {
                ++index;
                continue;
            }

            destroyPending(*index);
            index = m_pending.erase(index);
        }
    }

    void cleanupAll()
    {
        for (std::vector<PendingBuffer>::iterator index = m_pending.begin();
             index != m_pending.end(); ++index) {
            destroyPending(*index);
        }
        m_pending.clear();
    }

    void destroyPending(PendingBuffer& pending)
    {
        m_waveOutUnprepareHeader(m_waveOut, pending.m_header, sizeof(WAVEHDR));
        delete[] pending.m_data;
        delete pending.m_header;
    }

private:
    HMODULE m_winmm;
    HWAVEOUT m_waveOut;
    WaveOutOpenProc m_waveOutOpen;
    WaveOutPrepareHeaderProc m_waveOutPrepareHeader;
    WaveOutUnprepareHeaderProc m_waveOutUnprepareHeader;
    WaveOutWriteProc m_waveOutWrite;
    WaveOutResetProc m_waveOutReset;
    WaveOutCloseProc m_waveOutClose;
    bool m_running;
    std::vector<PendingBuffer> m_pending;
};

#elif defined(__APPLE__)

#include <AudioToolbox/AudioToolbox.h>
#include <limits>
#include <mutex>

class AudioSource::Impl {
public:
    bool start(AudioSource::ChunkCallback, void*, const String&)
    {
        LOG((CLOG_INFO "server audio sharing is not supported on this platform"));
        return false;
    }

    void stop()
    {
    }

    bool isRunning() const
    {
        return false;
    }
};

class AudioPlayer::Impl {
public:
    Impl() :
        m_queue(NULL),
        m_running(false),
        m_queuedBuffers(0)
    {
    }

    ~Impl()
    {
        stop();
    }

    bool start(const AudioFormat& format)
    {
        stop();

        if (format.m_sampleRate == 0 || format.m_channels == 0 ||
            format.m_bitsPerSample != 16) {
            LOG((CLOG_ERR "client audio playback unavailable: unsupported format %u Hz, %u channels, %u-bit",
                 format.m_sampleRate, format.m_channels, format.m_bitsPerSample));
            return false;
        }

        AudioStreamBasicDescription streamFormat;
        std::memset(&streamFormat, 0, sizeof(streamFormat));
        streamFormat.mSampleRate = format.m_sampleRate;
        streamFormat.mFormatID = kAudioFormatLinearPCM;
        streamFormat.mFormatFlags =
            kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
#if __BIG_ENDIAN__
        streamFormat.mFormatFlags |= kLinearPCMFormatFlagIsBigEndian;
#endif
        streamFormat.mChannelsPerFrame = format.m_channels;
        streamFormat.mBitsPerChannel = format.m_bitsPerSample;
        streamFormat.mFramesPerPacket = 1;
        streamFormat.mBytesPerFrame =
            (streamFormat.mChannelsPerFrame * streamFormat.mBitsPerChannel) / 8;
        streamFormat.mBytesPerPacket = streamFormat.mBytesPerFrame;

        AudioQueueRef queue = NULL;
        OSStatus status = AudioQueueNewOutput(
            &streamFormat,
            &AudioPlayer::Impl::audioQueueOutputCallback,
            this,
            NULL,
            NULL,
            0,
            &queue);
        if (status != noErr) {
            LOG((CLOG_ERR "client audio playback unavailable: AudioQueueNewOutput failed %i",
                 static_cast<int>(status)));
            return false;
        }

        status = AudioQueueStart(queue, NULL);
        if (status != noErr) {
            LOG((CLOG_ERR "client audio playback unavailable: AudioQueueStart failed %i",
                 static_cast<int>(status)));
            AudioQueueDispose(queue, true);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue = queue;
            m_running = true;
            m_queuedBuffers = 0;
        }

        LOG((CLOG_INFO "client audio playback started: %u Hz, %u channels, %u-bit",
             format.m_sampleRate, format.m_channels, format.m_bitsPerSample));
        return true;
    }

    bool play(const char* data, size_t dataSize)
    {
        if (data == NULL || dataSize == 0) {
            return false;
        }

        if (dataSize > static_cast<size_t>(std::numeric_limits<UInt32>::max())) {
            LOG((CLOG_DEBUG "dropping audio chunk because it is too large"));
            return true;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running || m_queue == NULL) {
            return false;
        }

        if (m_queuedBuffers >= kMaxQueuedBuffers) {
            LOG((CLOG_NOTE "client audio playback queue is stale; reopening device"));
            return false;
        }

        AudioQueueBufferRef buffer = NULL;
        OSStatus status = AudioQueueAllocateBuffer(
            m_queue, static_cast<UInt32>(dataSize), &buffer);
        if (status != noErr || buffer == NULL) {
            LOG((CLOG_ERR "client audio playback failed: AudioQueueAllocateBuffer failed %i",
                 static_cast<int>(status)));
            return false;
        }

        std::memcpy(buffer->mAudioData, data, dataSize);
        buffer->mAudioDataByteSize = static_cast<UInt32>(dataSize);

        ++m_queuedBuffers;
        status = AudioQueueEnqueueBuffer(m_queue, buffer, 0, NULL);
        if (status != noErr) {
            --m_queuedBuffers;
            AudioQueueFreeBuffer(m_queue, buffer);
            LOG((CLOG_ERR "client audio playback failed: AudioQueueEnqueueBuffer failed %i",
                 static_cast<int>(status)));
            return false;
        }
        return true;
    }

    void stop()
    {
        AudioQueueRef queue = NULL;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            queue = m_queue;
            m_queue = NULL;
            m_running = false;
            m_queuedBuffers = 0;
        }

        if (queue != NULL) {
            AudioQueueStop(queue, true);
            AudioQueueDispose(queue, true);
        }
    }

    bool isRunning() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_running;
    }

private:
    static void audioQueueOutputCallback(void* context, AudioQueueRef queue,
                                         AudioQueueBufferRef buffer)
    {
        static_cast<AudioPlayer::Impl*>(context)->onBufferComplete(queue, buffer);
    }

    void onBufferComplete(AudioQueueRef queue, AudioQueueBufferRef buffer)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue != queue) {
            return;
        }

        AudioQueueFreeBuffer(queue, buffer);
        if (m_queuedBuffers > 0) {
            --m_queuedBuffers;
        }
    }

private:
    enum {
        kMaxQueuedBuffers = 6
    };

    AudioQueueRef m_queue;
    bool m_running;
    size_t m_queuedBuffers;
    mutable std::mutex m_mutex;
};

#else

class AudioSource::Impl {
public:
    bool start(AudioSource::ChunkCallback, void*, const String&)
    {
        LOG((CLOG_INFO "server audio sharing is not supported on this platform"));
        return false;
    }

    void stop()
    {
    }

    bool isRunning() const
    {
        return false;
    }
};

class AudioPlayer::Impl {
public:
    bool start(const AudioFormat&)
    {
        LOG((CLOG_INFO "client audio playback is not supported on this platform"));
        return false;
    }

    bool play(const char*, size_t)
    {
        return false;
    }

    void stop()
    {
    }

    bool isRunning() const
    {
        return false;
    }
};

#endif

AudioSource::AudioSource() :
    m_impl(new Impl)
{
}

AudioSource::~AudioSource()
{
    delete m_impl;
}

bool
AudioSource::start(AudioSource::ChunkCallback callback, void* context, const String& quality)
{
    return m_impl->start(callback, context, quality);
}

void
AudioSource::stop()
{
    m_impl->stop();
}

bool
AudioSource::isRunning() const
{
    return m_impl->isRunning();
}

AudioPlayer::AudioPlayer() :
    m_impl(new Impl)
{
}

AudioPlayer::~AudioPlayer()
{
    delete m_impl;
}

bool
AudioPlayer::start(const AudioFormat& format)
{
    return m_impl->start(format);
}

bool
AudioPlayer::play(const char* data, size_t dataSize)
{
    return m_impl->play(data, dataSize);
}

void
AudioPlayer::stop()
{
    m_impl->stop();
}

bool
AudioPlayer::isRunning() const
{
    return m_impl->isRunning();
}
