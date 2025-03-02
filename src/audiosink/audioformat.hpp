//
// Created by ColorsWind on 2022/5/15.
//
#pragma once

#include <QtCore>
#include <utility>
#include "portaudio.h"

INCLUDE_FFMPEG_BEGIN
#include "libavutil/samplefmt.h"
INCLUDE_FFMPEG_END



struct AudioDataInfo {
    qint32 origLength;
    qint32 processedLength;
    qreal speedUpRate;
};

struct PonySampleFormat {
private:
    using TransformFunc = std::function<void(std::byte *, qreal, unsigned long)>;

    int m_index;
    PaSampleFormat m_paSampleFormat;
    AVSampleFormat m_ffmpegSampleFormat;
    int m_bytesPerSample;
    std::function<void(std::byte *, qreal, unsigned long)> m_transform;


    PonySampleFormat(
            int mIndex,
            PaSampleFormat paSampleFormat,
            AVSampleFormat ffmpegSampleFormat,
            int bytesPerSample,
            TransformFunc transformFunc
    ) : m_index(mIndex),
        m_paSampleFormat(paSampleFormat),
        m_ffmpegSampleFormat(ffmpegSampleFormat),
        m_bytesPerSample(bytesPerSample),
        m_transform(std::move(transformFunc)) {}

public:
    template<class T>
    static PonySampleFormat of(PaSampleFormat paSample, AVSampleFormat ffmpegSample) noexcept {
        static int id = 0;
        TransformFunc transform;
        size_t size;
        if constexpr(std::is_same<T, void>()) {
            transform = [](std::byte *src_, qreal factor, unsigned long samples) {
                throw std::runtime_error("Unsupported samples format.");
            };
            size = 0xABCDEF;
        } else {
            transform = [](std::byte *src_, qreal factor, unsigned long samples) {
                T *src = static_cast<T *>(static_cast<void *>(src_));
                for (size_t sampleOffset = 0; sampleOffset < samples; sampleOffset++) {
                    src[sampleOffset] = static_cast<T>(src[sampleOffset] * factor);
                }
            };
            size = sizeof(T);
        }
        return {id, paSample, ffmpegSample, static_cast<int>(size), transform};
    }


    void transformSampleVolume(std::byte *src, qreal factor, unsigned long samples) const {
        m_transform(src, factor, samples);
    }

    bool operator==(const PonySampleFormat &rhs) const {
        return this->m_index == rhs.m_index;
    }

    bool operator!=(const PonySampleFormat &rhs) const {
        return !(rhs == *this);
    }

    [[nodiscard]] PaSampleFormat getPaSampleFormat() const {
        return m_paSampleFormat;
    }

    [[nodiscard]] AVSampleFormat getFFmpegSampleFormat() const {
        return m_ffmpegSampleFormat;
    }

    [[nodiscard]] int getBytesPerSample() const {
        return m_bytesPerSample;
    }

};



class PonyAudioFormat {
private:
    PonySampleFormat m_sampleFormat;
    int m_sampleRate;
    int m_channelCount;

public:

    PonyAudioFormat(
            PonySampleFormat sampleFormat,
            int sampleRate,
            int channelCount
    ) noexcept: m_sampleFormat(std::move(sampleFormat)), m_sampleRate(sampleRate), m_channelCount(channelCount) {}


    [[nodiscard]] const PonySampleFormat &getSampleFormat() const { return m_sampleFormat; }

    [[nodiscard]] PaSampleFormat getSampleFormatForPA() const {
        return m_sampleFormat.getPaSampleFormat();
    }

    [[nodiscard]] AVSampleFormat getSampleFormatForFFmpeg() const {
        return m_sampleFormat.getFFmpegSampleFormat();
    }

    [[nodiscard]] qreal durationOfBytes(int64_t bytes) const {
        return static_cast<qreal>(bytes) / (m_sampleRate * m_channelCount * getBytesPerSample());
    }

    [[nodiscard]] int64_t bytesOfDuration(qreal duration) const {
        return static_cast<int64_t>(duration * m_sampleRate * m_channelCount * getBytesPerSample());
    }

    [[nodiscard]] int getBytesPerSample() const {
        return m_sampleFormat.getBytesPerSample();
    }

    [[nodiscard]] int getBytesPerSampleChannels() const {
        return m_sampleFormat.getBytesPerSample() * m_channelCount;
    }

    [[nodiscard]] int getSampleRate() const {
        return m_sampleRate;
    }

    [[nodiscard]] int getChannelCount() const {
        return m_channelCount;
    }

    [[nodiscard]] int64_t suggestedRingBuffer(qreal speedFactor) const {
        return qBound<int64_t>(
                static_cast<int64_t>(2 * 1024 * m_channelCount * m_sampleFormat.getBytesPerSample()),
                bytesOfDuration(0.2 * speedFactor),
                256 << 20
        );
    }
};

namespace AnytMusic {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
    const PonySampleFormat Unknown = PonySampleFormat::of<void>(paNonInterleaved, AV_SAMPLE_FMT_NONE);
    const PonySampleFormat UInt8 = PonySampleFormat::of<uint8_t>(paUInt8, AV_SAMPLE_FMT_U8);
    const PonySampleFormat Int16 = PonySampleFormat::of<int16_t>(paInt16, AV_SAMPLE_FMT_S16);
    const PonySampleFormat Int32 = PonySampleFormat::of<int32_t>(paInt32, AV_SAMPLE_FMT_S32);
    const PonySampleFormat Float = PonySampleFormat::of<float_t>(paFloat32, AV_SAMPLE_FMT_FLT);
#pragma GCC diagnostic pop
    const PonyAudioFormat DEFAULT_AUDIO_FORMAT = {Int16, 44100, 2};

    static PonySampleFormat valueOf(AVSampleFormat ffmpegFormat) {
        switch (ffmpegFormat) {
            case AV_SAMPLE_FMT_U8:
            case AV_SAMPLE_FMT_U8P:
                return UInt8;
            case AV_SAMPLE_FMT_S16:
            case AV_SAMPLE_FMT_S16P:
                return Int16;
            case AV_SAMPLE_FMT_S32:
            case AV_SAMPLE_FMT_S32P:
                return Int32;
            case AV_SAMPLE_FMT_FLT:
            case AV_SAMPLE_FMT_FLTP:
                return Float;
            default:
                return Unknown;
        }
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
    static PonySampleFormat valueOf(PaSampleFormat paSampleFormat) {
        switch (paSampleFormat) {
            case paUInt8:
                return UInt8;
            case paInt16:
                return Int16;
            case paInt32:
                return Int32;
            case paFloat32:
                return Float;
            default:
                return Unknown;
        }

    }
#pragma GCC diagnostic pop
}
