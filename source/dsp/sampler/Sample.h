#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <vector>

// A recording held in memory: its frames as floats, one or two channels (all the same length), at the file's own rate.
// (PR7-D's drum Sample, without its drum-only parts.) 0.1.0 loads whole files; disk streaming (DiskStreamer.h, kept
// from PR7-D) comes in with the libraries (plan stage 3).
namespace sampler
{
    struct Sample
    {
        juce::String name;
        juce::String path;                          // the file it came from
        double sampleRate = 48000.0;
        std::vector<std::vector<float>> channels;   // 1 or 2

        int length() const noexcept { return channels.empty() ? 0 : (int) channels[0].size(); }
        size_t bytes() const noexcept { return channels.size() * (size_t) length() * sizeof (float); }
    };

    namespace sampleFiles
    {
        // Longer files are cut here: held notes of orchestral samples run to 30 s or so; ten minutes is a safety limit.
        inline constexpr double maxSeconds = 600.0;
        // WAV, AIFF, FLAC, Ogg Vorbis (JUCE's basic formats); null if it can't be read. The first two channels.
        std::shared_ptr<const Sample> load (const juce::File&);
    }
}
