#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

// DISK STREAMING (PR7-O 0.11.0; PR7-D 0.28.0's streamer reworked for held, looping orchestral notes). Our own design
// and code.
//
//   - A long recording keeps only some short stretches in memory (Sample::pieces): its start, where a legato join
//     starts it, the start of its tail after its loop, a few frames round each loop point. Everything else is read
//     from its file while it plays.
//   - A voice playing a streamed recording claims a STREAM: a background thread reads the frames the voice will
//     play next, in the order it'll play them (its TIMELINE: the file's frames from where it starts, going back
//     round its loop at each loop end until it's let go), into a ring (0.37 s at 44.1 kHz) ahead of it. Stretches
//     held in memory are skipped (a note's first frames come from memory while the first read is under way).
//   - A crossfaded loop also needs the frames before its start while it crossfades: the stream reads those into a
//     second ring alongside (`back`).
//   - When the note is let go and its loop stops going round, the voice says where its timeline leaves the loop
//     (`stopLooping`); the stream reads from the file's frames past the loop end from there. The tail's first
//     frames are in memory, so this costs no gap.
//   - The audio thread never waits for the disk while playing live: frames the disk hasn't read yet are silent (and
//     counted). Bouncing (non-realtime) reads them there and then (`fillNow`), so a bounce is always whole.
namespace sampler
{
    // A streamed recording: its file, length, channels, and the stretches held in memory (file frames [start, end),
    // in order, apart).
    struct StreamSource
    {
        juce::File file;
        juce::int64 length = 0;
        int numChannels = 1;
        std::vector<std::pair<juce::int64, juce::int64>> resident;
        // The end of the resident stretch holding `frame` (frame itself if none does); the start of the next one from
        // `frame` on (the length if none).
        juce::int64 residentEnd (juce::int64 frame) const noexcept;
        juce::int64 nextResident (juce::int64 frame) const noexcept;
    };

    // How a voice will play it: from `start` (a file frame), going round [loopStart, loopEnd) when `looping`,
    // crossfading over `crossfade` frames before the loop end (the back reads: those frames less the loop's length).
    struct StreamPlan
    {
        juce::int64 start = 0;
        bool looping = false;
        juce::int64 loopStart = 0, loopEnd = 0;
        int crossfade = 0;
    };

    class Streamer
    {
    public:
        static constexpr int maxStreams = 256;
        static constexpr int ringFrames = 1 << 14;
        static constexpr int chunkFrames = 2048;
        static constexpr juce::int64 never = std::numeric_limits<juce::int64>::max();

        Streamer();
        ~Streamer();

        // Audio thread. A stream for a voice (-1: none free: the voice plays what's in memory only). The timeline
        // starts at plan.start.
        int claim (const StreamSource&, const StreamPlan&) noexcept;
        // The voice won't read timeline frames under `timeline` again (the ring may reuse their room).
        void consumed (int stream, juce::int64 timeline) noexcept;
        // From timeline frame `at` (where the timeline was going to go back to the loop start) the file goes on
        // past the loop end instead.
        void stopLooping (int stream, juce::int64 at) noexcept;
        // The frames from timeline frame `t` on that are ready and lie in one stretch of the ring, as each channel's
        // pointer to t's (`back`: the crossfade's back reads). 0: none ready. They stop before any frame held in
        // memory (its place in the ring isn't filled) and at the timeline's next going-back.
        int span (int stream, juce::int64 t, bool back, const float*& left, const float*& right) noexcept;
        // Bouncing: reads the stream's next chunk here and now; false if there's nothing more it can read.
        bool fillNow (int stream) noexcept;
        void release (int stream) noexcept;
        void countUnderrun() noexcept { underruns.fetch_add (1, std::memory_order_relaxed); }

        int getUnderruns() const noexcept { return underruns.load(); }   // frames the disk was late for (or no stream)
        int getActiveStreams() const noexcept;
        // Tests: waits (up to `timeoutMs`) until every playing stream is as full as it can be, as an idle disk keeps
        // it; true if it got there.
        bool waitUntilFilled (int timeoutMs) noexcept;

        // Timeline frame -> file frame for a plan (exitAt: where it stopped looping; never: still looping).
        static juce::int64 fileFrame (const StreamPlan&, juce::int64 t, juce::int64 exitAt) noexcept;

    private:
        struct Stream
        {
            std::atomic<int> state { 0 };       // 0 free, 1 being set up, 2 playing, 3 released
            const StreamSource* source = nullptr;
            StreamPlan plan;
            std::atomic<juce::int64> written { 0 };   // timeline frames ready up to here
            std::atomic<juce::int64> need { 0 };      // the voice reads from here on
            std::atomic<juce::int64> exitAt { never }, exitApplied { never };
            std::vector<float> ring;            // main left, main right, back left, back right: ringFrames each
            std::mutex fillLock;
        };
        std::vector<std::unique_ptr<Stream>> streams;
        std::atomic<int> underruns { 0 };

        class Thread;
        std::unique_ptr<Thread> thread;
        bool fill (Stream&);
        void skipResident (Stream&, juce::int64 exitAt);
        juce::int64 pastResident (const Stream&, juce::int64 from, juce::int64 exitAt) const noexcept;
        std::mutex readersLock;
        std::map<juce::String, std::unique_ptr<juce::AudioFormatReader>> readers;   // open files (the last 192)
        std::vector<juce::String> readerOrder;
        juce::AudioFormatManager formats;
        juce::AudioBuffer<float> chunk;
        juce::AudioFormatReader* readerFor (const juce::File&);
        bool readInto (const StreamSource&, juce::int64 fileStart, int frames, float* left, float* right, size_t ringStart);
    };
}
