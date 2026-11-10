#pragma once

#include "dsp/sampler/DiskStreamer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <utility>
#include <vector>

// A recording: its frames as floats, one or two channels (all the same length), at the file's own rate. (PR7-D's drum
// Sample, without its drum-only parts.) Held whole in memory (`channels`), or, 0.11.0's DISK STREAMING, STREAMED:
// only some stretches of it in memory (`pieces`: its start, a legato join's start, the tail after its loop, a few
// frames round each loop point), the rest read from its file while it plays (DiskStreamer.h).
namespace sampler
{
    struct Sample
    {
        juce::String name;
        juce::String path;                          // the file it came from
        double sampleRate = 48000.0;
        std::vector<std::vector<float>> channels;   // the whole recording, 1 or 2 channels (empty when streamed)

        // 0.11.0, streamed: its length, its channels, the stretches in memory (in order, apart), its file.
        struct Piece
        {
            juce::int64 start = 0;
            std::vector<std::vector<float>> channels;
            juce::int64 end() const noexcept { return start + (channels.empty() ? 0 : (juce::int64) channels[0].size()); }
        };
        std::vector<Piece> pieces;
        juce::int64 frames = 0;
        int streamedChannels = 0;
        StreamSource stream;

        bool streamed() const noexcept { return channels.empty() && frames > 0; }
        int length() const noexcept { return streamed() ? (int) frames : channels.empty() ? 0 : (int) channels[0].size(); }
        int numChannels() const noexcept { return streamed() ? streamedChannels : (int) channels.size(); }
        // What it holds in memory, and what the whole recording would.
        size_t bytes() const noexcept;
        size_t wholeBytes() const noexcept { return (size_t) numChannels() * (size_t) length() * sizeof (float); }
        // The piece in memory holding `frame` (null: none, or it isn't streamed).
        const Piece* pieceAt (juce::int64 frame) const noexcept;
    };

    namespace sampleFiles
    {
        // Longer files are cut here: held notes of orchestral samples run to 30 s or so; ten minutes is a safety limit.
        inline constexpr double maxSeconds = 600.0;
        // WAV, AIFF, FLAC, Ogg Vorbis (JUCE's basic formats); null if it can't be read. The first two channels.
        std::shared_ptr<const Sample> load (const juce::File&);
        // 0.11.0: a whole recording made streamed, keeping only `keep` (file frames [start, end); merged, cut to it).
        std::shared_ptr<const Sample> streamedFrom (const Sample& whole, std::vector<std::pair<juce::int64, juce::int64>> keep);
    }
}
