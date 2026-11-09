#include "dsp/sampler/DiskStreamer.h"

#include <limits>

namespace sampler
{
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
                        // Freed by the audio thread: ready for the next hit.
                        const std::scoped_lock lock (stream->fillLock);
                        stream->source = nullptr;
                        stream->attached.store (0);
                        stream->state.store (0, std::memory_order_release);
                    }
                    else if (state == 2)
                        busy = owner.fill (*stream, false) || busy;
                }
                // Nothing to read: rest a little (a new hit plays its head, 0.2 s, while the first read comes).
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
            s->ring.assign ((size_t) maxOutputs * 2 * ringFrames, 0.0f);
            streams.push_back (std::move (s));
        }
        thread = std::make_unique<Thread> (*this);
        thread->startThread (juce::Thread::Priority::high);
    }

    Streamer::~Streamer()
    {
        thread->stopThread (2000);
    }

    int Streamer::claim (const StreamSource& source) noexcept
    {
        for (int k = 0; k < maxStreams; ++k)
        {
            auto& s = *streams[(size_t) k];
            auto expected = 0;
            if (! s.state.compare_exchange_strong (expected, 1, std::memory_order_acq_rel))
                continue;
            s.source = &source;
            s.written.store (source.headFrames, std::memory_order_relaxed);
            for (auto& r : s.read)
                r.store (source.headFrames, std::memory_order_relaxed);
            s.attached.store (0, std::memory_order_relaxed);
            s.state.store (2, std::memory_order_release);
            return k;
        }
        return -1;
    }

    void Streamer::attach (int stream, int output) noexcept
    {
        if (stream >= 0 && stream < maxStreams && output >= 0 && output < maxOutputs)
            streams[(size_t) stream]->attached.fetch_or (1u << output);
    }

    void Streamer::consumed (int stream, int output, juce::int64 frame) noexcept
    {
        if (stream >= 0 && stream < maxStreams && output >= 0 && output < maxOutputs)
            streams[(size_t) stream]->read[(size_t) output].store (frame, std::memory_order_release);
    }

    float Streamer::sample (int stream, int output, int channel, juce::int64 frame, bool waitForDisk) noexcept
    {
        if (stream < 0 || stream >= maxStreams || output < 0 || output >= maxOutputs)
            return 0.0f;
        auto& s = *streams[(size_t) stream];
        const auto* source = s.source;
        if (source == nullptr || frame < source->headFrames || frame >= source->length)
            return 0.0f;
        auto written = s.written.load (std::memory_order_acquire);
        if (frame >= written)
        {
            if (! waitForDisk)
            {
                underruns.fetch_add (1, std::memory_order_relaxed);
                return 0.0f;
            }
            // Bouncing: read it now (the disk thread may be at it too: the stream's lock keeps them apart). PR7-O 0.1.0
            // fix: `fill` also says false when the disk thread has just filled the ring itself (nothing left to do), so
            // where the ring is filled to is read again after the loop, not taken from before it (that stale value made
            // about one run in three of the streaming test return silence for audio that was there).
            while (frame >= (written = s.written.load (std::memory_order_acquire)) && written < source->length)
                if (! fill (s, false))
                    break;
            written = s.written.load (std::memory_order_acquire);
            if (frame >= written)
                return 0.0f;
        }
        const auto index = (size_t) ((frame - source->headFrames) & (ringFrames - 1));
        return s.ring[((size_t) output * 2 + (size_t) juce::jlimit (0, 1, channel)) * ringFrames + index];
    }

    int Streamer::span (int stream, int output, juce::int64 frame, const float*& left, const float*& right) noexcept
    {
        if (stream < 0 || stream >= maxStreams || output < 0 || output >= maxOutputs)
            return 0;
        auto& s = *streams[(size_t) stream];
        const auto* source = s.source;
        if (source == nullptr || frame < source->headFrames || frame >= source->length)
            return 0;
        const auto written = juce::jmin (s.written.load (std::memory_order_acquire), source->length);
        if (frame >= written)
            return 0;
        const auto index = (juce::int64) ((frame - source->headFrames) & (ringFrames - 1));
        const auto n = juce::jmin (written - frame, (juce::int64) ringFrames - index);
        left = s.ring.data() + ((size_t) output * 2) * ringFrames + (size_t) index;
        right = s.ring.data() + ((size_t) output * 2 + 1) * ringFrames + (size_t) index;
        return (int) juce::jmin (n, (juce::int64) std::numeric_limits<int>::max());
    }

    void Streamer::release (const std::array<bool, maxStreams>& inUse) noexcept
    {
        for (int k = 0; k < maxStreams; ++k)
            if (! inUse[(size_t) k])
            {
                auto expected = 2;
                streams[(size_t) k]->state.compare_exchange_strong (expected, 3, std::memory_order_acq_rel);
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
            for (const auto& s : streams)
            {
                if (s->state.load (std::memory_order_acquire) != 2 || s->source == nullptr)
                    continue;
                // Full: all of it read, or no room for another chunk past its slowest voice.
                const auto written = s->written.load (std::memory_order_acquire);
                auto slowest = (juce::int64) s->source->headFrames;
                const auto attached = s->attached.load (std::memory_order_acquire);
                auto any = false;
                for (int o = 0; o < maxOutputs; ++o)
                    if ((attached & (1u << o)) != 0)
                    {
                        const auto r = s->read[(size_t) o].load (std::memory_order_acquire);
                        slowest = any ? juce::jmin (slowest, r) : r;
                        any = true;
                    }
                if (written < s->source->length && slowest + ringFrames - written >= chunkFrames)
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
        // At most 192 files open (0.33.0: a pack's hit can be 7 files, one a mic; macOS allows 256 a program by
        // default): the one used longest ago closes.
        if (readerOrder.size() >= 192)
        {
            readers.erase (readerOrder.front());
            readerOrder.erase (readerOrder.begin());
        }
        readerOrder.push_back (key);
        return (readers[key] = std::move (reader)).get();
    }

    bool Streamer::fill (Stream& s, bool)
    {
        const std::scoped_lock lock (s.fillLock);
        if (s.state.load (std::memory_order_acquire) != 2 || s.source == nullptr)
            return false;
        const auto& source = *s.source;
        const auto written = s.written.load (std::memory_order_acquire);
        if (written >= source.length)
            return false;
        // Room: the ring holds ringFrames past the slowest of the voices playing it (none yet: from the head).
        auto slowest = written;
        const auto attached = s.attached.load (std::memory_order_acquire);
        auto any = false;
        for (int o = 0; o < maxOutputs; ++o)
            if ((attached & (1u << o)) != 0)
            {
                const auto r = s.read[(size_t) o].load (std::memory_order_acquire);
                slowest = any ? juce::jmin (slowest, r) : r;
                any = true;
            }
        if (! any)
            slowest = source.headFrames;
        const auto room = slowest + ringFrames - written;
        const auto frames = (int) juce::jmin<juce::int64> (chunkFrames, source.length - written, room);
        if (frames <= 0)
            return false;

        {
            const std::scoped_lock readerLock (readersLock);
            // Into each mic's ring, as the recipe says: cleared, then each file read once (one, or one a mic) and its
            // parts added.
            const auto start = (size_t) ((written - source.headFrames) & (ringFrames - 1));
            for (int o = 0; o < source.numOutputs && o < maxOutputs; ++o)
                for (int c = 0; c < 2; ++c)
                {
                    auto* ring = s.ring.data() + ((size_t) o * 2 + (size_t) c) * ringFrames;
                    for (int i = 0; i < frames; ++i)
                        ring[(start + (size_t) i) & (ringFrames - 1)] = 0.0f;
                }
            const auto numFiles = juce::jmax (1, (int) source.files.size());
            auto anyRead = false;
            for (int f = 0; f < numFiles; ++f)
            {
                if (std::none_of (source.parts.begin(), source.parts.end(), [f] (const StreamSource::Part& p) { return p.fileIndex == f; }))
                    continue;
                auto* reader = readerFor (source.fileAt (f));
                if (reader == nullptr)
                    continue;   // gone: that mic silent from here
                anyRead = true;
                const auto channels = (int) reader->numChannels;
                if (chunk.getNumChannels() < channels || chunk.getNumSamples() < chunkFrames)
                    chunk.setSize (juce::jmax (channels, chunk.getNumChannels()), chunkFrames, false, false, true);
                chunk.clear();
                if (written < reader->lengthInSamples)
                    reader->read (&chunk, 0, (int) juce::jmin<juce::int64> (frames, reader->lengthInSamples - written), written, true, true);
                for (const auto& part : source.parts)
                {
                    if (part.fileIndex != f || part.fileChannel < 0 || part.fileChannel >= channels || part.output < 0 || part.output >= maxOutputs)
                        continue;
                    const auto* from = chunk.getReadPointer (part.fileChannel);
                    auto* ring = s.ring.data() + ((size_t) part.output * 2 + (size_t) juce::jlimit (0, 1, part.channel)) * ringFrames;
                    for (int i = 0; i < frames; ++i)
                        ring[(start + (size_t) i) & (ringFrames - 1)] += from[i] * part.gain;
                }
            }
            if (! anyRead)
            {
                // Every file's gone: silence from here (the hit ends at its head's end, in effect).
                s.written.store (source.length, std::memory_order_release);
                return false;
            }
        }
        s.written.store (written + frames, std::memory_order_release);
        return true;
    }
}
