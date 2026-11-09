#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

// DISK STREAMING (PR7-D 0.28.0): a sampled kit's hits played from disk, as big sampled drum libraries are, so a whole
// multi-mic kit at full length (every hit, every mic, every ring of a cymbal) fits in a computer's memory. Our own
// design and code.
//
//   - Each hit keeps only its first part (its HEAD, 0.2 s) in memory, so it starts at once.
//   - When a hit starts, it claims a STREAM: a background thread reads the rest of its file, ahead of where it's
//     playing, into a ring (about 0.34 s) for each of its mics. A hit's mics come from one multichannel file, so the
//     file is read once for all of them (each read split into the mics' channels as the hit's recipe says: a
//     stereo pair, a centre mic in both sides, two toms' mics summed...).
//   - The audio thread never waits for the disk while playing live: if the disk ever falls behind, the missing
//     samples are silent (and counted). Bouncing (non-realtime) waits for them instead, so a bounce is always whole.
//   - A stream is freed when no voice plays it any more.
namespace sampler
{
    // Where a streamed hit's audio comes from, and how its file's channels make its mics.
    struct StreamSource
    {
        juce::File file;
        double sampleRate = 48000.0;
        juce::int64 length = 0;                 // frames in the file (the longest, with `files`)
        int headFrames = 0;                     // frames each mic's sample holds in memory (from the start)
        // 0.33.0: a hit whose mics are each their own file (a sample pack's "_OH", "_RM"...): `files` (when set; else
        // `file` alone), each part reading its `fileIndex`. A file shorter than the rest is silent past its end.
        std::vector<juce::File> files;
        struct Part { int fileChannel = 0; int output = 0; int channel = 0; float gain = 1.0f; int fileIndex = 0; };
        std::vector<Part> parts;                // file channel (from 0) -> a mic's (output's) channel, at a gain
        const juce::File& fileAt (int index) const noexcept
        {
            return index >= 0 && index < (int) files.size() ? files[(size_t) index] : file;
        }
        int numOutputs = 0;
    };

    class Streamer
    {
    public:
        static constexpr int maxStreams = 48;    // as many as the kit's hits at once (Kit::maxHits)
        static constexpr int maxOutputs = 12;    // mics per hit
        static constexpr int ringFrames = 1 << 14;   // 0.34 s at 48 kHz, past the slowest voice
        static constexpr int chunkFrames = 2048;

        Streamer();
        ~Streamer();

        // Audio thread: a stream for a hit (-1: none free: the hit plays its head only); a voice playing one of its
        // mics; where that voice has read up to; a frame of a mic's channel (0: not read yet, and counted, unless
        // `waitForDisk`, which reads it there and then: bouncing).
        int claim (const StreamSource&) noexcept;
        void attach (int stream, int output) noexcept;
        void consumed (int stream, int output, juce::int64 frame) noexcept;
        float sample (int stream, int output, int channel, juce::int64 frame, bool waitForDisk) noexcept;
        // 0.56.0: the frames from `frame` on that are ready and lie in one stretch of the ring (to whichever comes first
        // of what's been read, the file's end and the ring's wrap), as each channel's pointer to `frame`'s value. Returns
        // how many (0: none ready: `sample` then counts or waits). What `sample` gives, without a call per frame.
        int span (int stream, int output, juce::int64 frame, const float*& left, const float*& right) noexcept;
        // Audio thread, once a block: the streams no voice plays any more are freed.
        void release (const std::array<bool, maxStreams>& inUse) noexcept;

        int getUnderruns() const noexcept { return underruns.load(); }   // samples the disk was late for, live
        int getActiveStreams() const noexcept;
        // Tests: waits (up to `timeoutMs`) until every playing stream is as full as it can be, as an idle disk keeps
        // it; true if it got there.
        bool waitUntilFilled (int timeoutMs) noexcept;

    private:
        struct Stream
        {
            std::atomic<int> state { 0 };       // 0 free, 1 being set up, 2 playing, 3 released
            const StreamSource* source = nullptr;
            std::atomic<juce::int64> written { 0 };   // frames ready (from the file's start; the head counts)
            std::array<std::atomic<juce::int64>, maxOutputs> read {};
            std::atomic<std::uint32_t> attached { 0 };
            std::vector<float> ring;            // maxOutputs x 2 channels x ringFrames
            std::mutex fillLock;
        };
        std::vector<std::unique_ptr<Stream>> streams;
        std::atomic<int> underruns { 0 };

        // The disk thread: fills each playing stream while it has room.
        class Thread;
        std::unique_ptr<Thread> thread;
        bool fill (Stream&, bool untilFull);
        std::mutex readersLock;
        std::map<juce::String, std::unique_ptr<juce::AudioFormatReader>> readers;   // open files (the last 192)
        std::vector<juce::String> readerOrder;
        juce::AudioFormatManager formats;
        juce::AudioBuffer<float> chunk;
        juce::AudioFormatReader* readerFor (const juce::File&);
    };
}
