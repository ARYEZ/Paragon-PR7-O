#include "dsp/sampler/DiskStreamer.h"

#include <algorithm>

namespace sampler
{
    juce::int64 StreamSource::residentEnd (juce::int64 frame) const noexcept
    {
        for (const auto& [a, b] : resident)
            if (frame < b)
                return frame >= a ? b : frame;
        return frame;
    }

    juce::int64 StreamSource::nextResident (juce::int64 frame) const noexcept
    {
        for (const auto& [a, b] : resident)
            if (frame < b)
                return std::max (a, frame);
        return length;
    }

    namespace
    {
        bool loops (const StreamPlan& plan) noexcept
        {
            return plan.looping && plan.loopEnd > plan.loopStart && plan.start < plan.loopEnd;
        }

        // How many timeline frames from `t` map to the file's frames in a row (to the next going-back).
        juce::int64 untilBreak (const StreamPlan& plan, juce::int64 t, juce::int64 exitAt) noexcept
        {
            if (! loops (plan) || t >= exitAt)
                return Streamer::never;
            if (t < plan.loopEnd)
                return plan.loopEnd - t;
            const auto length = plan.loopEnd - plan.loopStart;
            return length - (t - plan.loopEnd) % length;
        }

        // Where the timeline ends (the file's end reached; never while it goes round).
        juce::int64 timelineEnd (const StreamPlan& plan, juce::int64 length, juce::int64 exitAt) noexcept
        {
            if (! loops (plan))
                return length;
            if (exitAt == Streamer::never)
                return Streamer::never;
            return exitAt + (length - plan.loopEnd);
        }

        // The crossfade's zone (file frames whose back reads the voice may ask for), while it still goes round.
        bool inBackZone (const StreamPlan& plan, juce::int64 fileFrame, juce::int64 t, juce::int64 exitAt) noexcept
        {
            return loops (plan) && plan.crossfade > 0 && t < exitAt
                && fileFrame >= plan.loopEnd - plan.crossfade - 4 && fileFrame < plan.loopEnd;
        }
    }

    juce::int64 Streamer::fileFrame (const StreamPlan& plan, juce::int64 t, juce::int64 exitAt) noexcept
    {
        if (! loops (plan) || t < plan.loopEnd)
            return t;
        if (t >= exitAt)
            return plan.loopEnd + (t - exitAt);
        return plan.loopStart + (t - plan.loopEnd) % (plan.loopEnd - plan.loopStart);
    }

    class Streamer::Thread final : public juce::Thread
    {
    public:
        explicit Thread (Streamer& s) : juce::Thread ("PR7-O disk streaming"), owner (s) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                auto busy = false;
                for (auto& stream : owner.streams)
                {
                    const auto state = stream->state.load (std::memory_order_acquire);
                    if (state == 3)
                    {
                        // Let go by the audio thread: ready for the next voice.
                        const std::scoped_lock lock (stream->fillLock);
                        stream->source = nullptr;
                        stream->state.store (0, std::memory_order_release);
                    }
                    else if (state == 2)
                        busy = owner.fill (*stream) || busy;
                }
                // Nothing to read: rest a little (a new note plays from memory, 0.5 s, while its first read comes).
                if (! busy)
                    wait (2);
            }
        }

    private:
        Streamer& owner;
    };

    Streamer::Streamer()
    {
        formats.registerBasicFormats();
        for (int k = 0; k < maxStreams; ++k)
        {
            auto s = std::make_unique<Stream>();
            s->ring.assign ((size_t) 4 * ringFrames, 0.0f);
            streams.push_back (std::move (s));
        }
        chunk.setSize (2, chunkFrames);
        thread = std::make_unique<Thread> (*this);
        thread->startThread (juce::Thread::Priority::high);
    }

    Streamer::~Streamer()
    {
        thread->stopThread (2000);
    }

    juce::int64 Streamer::pastResident (const Stream& s, juce::int64 from, juce::int64 exitAt) const noexcept
    {
        // The first timeline frame from `from` on not held in memory (never through the crossfade's zone: its back
        // reads come from the ring).
        auto w = from;
        const auto end = timelineEnd (s.plan, s.source->length, exitAt);
        for (int guard = 0; guard < 64 && w < end; ++guard)
        {
            const auto a = fileFrame (s.plan, w, exitAt);
            if (inBackZone (s.plan, a, w, exitAt))
                break;
            const auto e = s.source->residentEnd (a);
            if (e <= a)
                break;
            auto step = std::min ({ e - a, untilBreak (s.plan, w, exitAt), end - w });
            if (loops (s.plan) && w < exitAt && s.plan.crossfade > 0 && a < s.plan.loopEnd - s.plan.crossfade - 4)
                step = std::min (step, s.plan.loopEnd - s.plan.crossfade - 4 - a);
            if (step <= 0)
                break;
            w += step;
        }
        return w;
    }

    void Streamer::skipResident (Stream& s, juce::int64 exitAt)
    {
        s.written.store (pastResident (s, s.written.load (std::memory_order_relaxed), exitAt), std::memory_order_release);
    }

    int Streamer::claim (const StreamSource& source, const StreamPlan& plan) noexcept
    {
        for (int k = 0; k < maxStreams; ++k)
        {
            auto& s = *streams[(size_t) k];
            auto expected = 0;
            if (! s.state.compare_exchange_strong (expected, 1, std::memory_order_acq_rel))
                continue;
            s.source = &source;
            s.plan = plan;
            s.plan.start = std::max<juce::int64> (0, plan.start);
            s.written.store (s.plan.start, std::memory_order_relaxed);
            s.need.store (s.plan.start, std::memory_order_relaxed);
            s.exitAt.store (never, std::memory_order_relaxed);
            s.exitApplied.store (never, std::memory_order_relaxed);
            skipResident (s, never);
            s.state.store (2, std::memory_order_release);
            return k;
        }
        return -1;
    }

    void Streamer::consumed (int stream, juce::int64 timeline) noexcept
    {
        if (stream >= 0 && stream < maxStreams)
        {
            auto& need = streams[(size_t) stream]->need;
            if (timeline > need.load (std::memory_order_relaxed))
                need.store (timeline, std::memory_order_release);
        }
    }

    void Streamer::stopLooping (int stream, juce::int64 at) noexcept
    {
        if (stream >= 0 && stream < maxStreams)
            streams[(size_t) stream]->exitAt.store (at, std::memory_order_release);
    }

    int Streamer::span (int stream, juce::int64 t, bool back, const float*& left, const float*& right) noexcept
    {
        if (stream < 0 || stream >= maxStreams)
            return 0;
        auto& s = *streams[(size_t) stream];
        if (s.state.load (std::memory_order_acquire) != 2 || s.source == nullptr)
            return 0;
        const auto exitAt = s.exitAt.load (std::memory_order_acquire);
        if (t >= exitAt && s.exitApplied.load (std::memory_order_acquire) != exitAt)
            return 0;   // the stream hasn't caught up with the loop's stopping yet
        const auto written = s.written.load (std::memory_order_acquire);
        // (a frame from where the voice reads on that isn't in memory is never further back than the ring's room)
        if (t >= written || t < s.need.load (std::memory_order_acquire))
            return 0;
        const auto index = (size_t) (t & (ringFrames - 1));
        auto n = std::min<juce::int64> (written - t, (juce::int64) ringFrames - (juce::int64) index);
        n = std::min (n, untilBreak (s.plan, t, exitAt));
        const auto frame = fileFrame (s.plan, t, exitAt) - (back ? s.plan.loopEnd - s.plan.loopStart : 0);
        n = std::min (n, s.source->nextResident (frame) - frame);
        if (n <= 0)
            return 0;
        const auto* ring = s.ring.data() + (back ? 2 * (size_t) ringFrames : 0);
        left = ring + index;
        right = ring + (size_t) ringFrames + index;
        return (int) n;
    }

    bool Streamer::fillNow (int stream) noexcept
    {
        if (stream < 0 || stream >= maxStreams)
            return false;
        return fill (*streams[(size_t) stream]);
    }

    void Streamer::release (int stream) noexcept
    {
        if (stream >= 0 && stream < maxStreams)
        {
            auto expected = 2;
            streams[(size_t) stream]->state.compare_exchange_strong (expected, 3, std::memory_order_acq_rel);
        }
    }

    int Streamer::getActiveStreams() const noexcept
    {
        int n = 0;
        for (const auto& s : streams)
            n += s->state.load() == 2 ? 1 : 0;
        return n;
    }

    bool Streamer::waitUntilFilled (int timeoutMs) noexcept
    {
        const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (juce::Time::getMillisecondCounter() < until)
        {
            auto filled = true;
            for (auto& s : streams)
            {
                const std::scoped_lock lock (s->fillLock);
                if (s->state.load (std::memory_order_acquire) != 2 || s->source == nullptr)
                    continue;
                const auto exitAt = s->exitAt.load();
                if (exitAt != s->exitApplied.load())
                {
                    filled = false;
                    continue;
                }
                const auto written = s->written.load();
                if (written < timelineEnd (s->plan, s->source->length, exitAt)
                    && pastResident (*s, std::min (s->need.load(), written), exitAt) + ringFrames - written >= chunkFrames)
                    filled = false;
            }
            if (filled)
                return true;
            juce::Thread::sleep (1);
        }
        return false;
    }

    juce::AudioFormatReader* Streamer::readerFor (const juce::File& file)
    {
        const auto key = file.getFullPathName();
        if (const auto it = readers.find (key); it != readers.end())
            return it->second.get();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
            return nullptr;
        // At most 192 files open (macOS allows 256 a program by default): the one opened longest ago closes.
        if (readerOrder.size() >= 192)
        {
            readers.erase (readerOrder.front());
            readerOrder.erase (readerOrder.begin());
        }
        readerOrder.push_back (key);
        return (readers[key] = std::move (reader)).get();
    }

    bool Streamer::readInto (const StreamSource& source, juce::int64 fileStart, int frames, float* left, float* right, size_t ringStart)
    {
        // (with readersLock held) The file's frames into the ring from ringStart (wrapping); silence where the file
        // can't be read or ends.
        auto* reader = readerFor (source.file);
        chunk.clear();
        if (reader != nullptr && fileStart >= 0 && fileStart < reader->lengthInSamples)
        {
            const auto n = (int) std::min<juce::int64> (frames, reader->lengthInSamples - fileStart);
            reader->read (&chunk, 0, n, fileStart, true, reader->numChannels > 1);
        }
        const auto* l = chunk.getReadPointer (0);
        const auto* r = chunk.getReadPointer (source.numChannels > 1 && reader != nullptr && reader->numChannels > 1 ? 1 : 0);
        for (int i = 0; i < frames; ++i)
        {
            const auto at = (ringStart + (size_t) i) & (size_t) (ringFrames - 1);
            left[at] = l[i];
            right[at] = r[i];
        }
        return reader != nullptr;
    }

    bool Streamer::fill (Stream& s)
    {
        const std::scoped_lock lock (s.fillLock);
        if (s.state.load (std::memory_order_acquire) != 2 || s.source == nullptr)
            return false;
        const auto& source = *s.source;
        // The loop stopped going round: the frames read past where it leaves are the wrong ones (going back round);
        // read from there again, past the loop's end.
        const auto exitAt = s.exitAt.load (std::memory_order_acquire);
        auto progressed = false;
        if (exitAt != s.exitApplied.load (std::memory_order_relaxed))
        {
            if (s.written.load (std::memory_order_relaxed) > exitAt)
                s.written.store (exitAt, std::memory_order_release);
            skipResident (s, exitAt);
            s.exitApplied.store (exitAt, std::memory_order_release);
            progressed = true;
        }
        const auto written = s.written.load (std::memory_order_acquire);
        const auto end = timelineEnd (s.plan, source.length, exitAt);
        // Room: the ring holds ringFrames from the first frame the voice will want from it (frames held in memory
        // take no room: their places in the ring are never read).
        const auto room = pastResident (s, std::min (s.need.load (std::memory_order_acquire), written), exitAt) + ringFrames - written;
        const auto frames = (int) std::min ({ (juce::int64) chunkFrames, room, untilBreak (s.plan, written, exitAt), end - written });
        if (frames <= 0)
            return progressed;
        const auto a = fileFrame (s.plan, written, exitAt);
        {
            const std::scoped_lock readerLock (readersLock);
            auto* ring = s.ring.data();
            const auto start = (size_t) (written & (ringFrames - 1));
            if (! readInto (source, a, frames, ring, ring + ringFrames, start))
            {
                // The file's gone: silence from here (the note ends where memory does, in effect).
                s.written.store (end == never ? written + frames : end, std::memory_order_release);
                return false;
            }
            // The crossfade's back reads: the same timeline frames, a loop's length earlier in the file.
            if (loops (s.plan) && s.plan.crossfade > 0 && written < exitAt)
            {
                const auto lo = std::max (a, s.plan.loopEnd - s.plan.crossfade - 4);
                const auto hi = std::min (a + frames, s.plan.loopEnd);
                if (hi > lo)
                    readInto (source, lo - (s.plan.loopEnd - s.plan.loopStart), (int) (hi - lo), ring + 2 * (size_t) ringFrames,
                              ring + 3 * (size_t) ringFrames, (start + (size_t) (lo - a)) & (size_t) (ringFrames - 1));
            }
        }
        s.written.store (written + frames, std::memory_order_release);
        skipResident (s, exitAt);
        return true;
    }
}
