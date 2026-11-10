#include "dsp/sampler/Sample.h"

#include <algorithm>

namespace sampler
{
    size_t Sample::bytes() const noexcept
    {
        if (! streamed())
            return channels.size() * (size_t) length() * sizeof (float);
        size_t n = 0;
        for (const auto& p : pieces)
            n += p.channels.size() * (size_t) (p.end() - p.start) * sizeof (float);
        return n;
    }

    const Sample::Piece* Sample::pieceAt (juce::int64 frame) const noexcept
    {
        // (a few pieces: the first that ends past the frame)
        for (const auto& p : pieces)
            if (frame < p.end())
                return frame >= p.start ? &p : nullptr;
        return nullptr;
    }
}

namespace sampler::sampleFiles
{
    std::shared_ptr<const Sample> load (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples < 2 || reader->sampleRate <= 0.0)
            return nullptr;
        const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));
        const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
        juce::AudioBuffer<float> buffer (channels, length);
        reader->read (&buffer, 0, length, 0, true, channels > 1);
        auto sample = std::make_shared<Sample>();
        sample->name = file.getFileNameWithoutExtension();
        sample->path = file.getFullPathName();
        sample->sampleRate = reader->sampleRate;
        for (int c = 0; c < channels; ++c)
            sample->channels.emplace_back (buffer.getReadPointer (c), buffer.getReadPointer (c) + length);
        return sample;
    }

    std::shared_ptr<const Sample> streamedFrom (const Sample& whole, std::vector<std::pair<juce::int64, juce::int64>> keep)
    {
        const auto length = (juce::int64) whole.length();
        if (whole.streamed() || length < 2 || whole.channels.empty())
            return nullptr;
        // In order, cut to the recording, overlapping or touching stretches merged.
        for (auto& [a, b] : keep)
        {
            a = juce::jlimit<juce::int64> (0, length, a);
            b = juce::jlimit<juce::int64> (0, length, b);
        }
        std::sort (keep.begin(), keep.end());
        std::vector<std::pair<juce::int64, juce::int64>> merged;
        for (const auto& [a, b] : keep)
        {
            if (b <= a)
                continue;
            if (! merged.empty() && a <= merged.back().second)
                merged.back().second = std::max (merged.back().second, b);
            else
                merged.emplace_back (a, b);
        }
        auto sample = std::make_shared<Sample>();
        sample->name = whole.name;
        sample->path = whole.path;
        sample->sampleRate = whole.sampleRate;
        sample->frames = length;
        sample->streamedChannels = (int) whole.channels.size();
        for (const auto& [a, b] : merged)
        {
            Sample::Piece piece;
            piece.start = a;
            for (const auto& c : whole.channels)
                piece.channels.emplace_back (c.begin() + a, c.begin() + b);
            sample->pieces.push_back (std::move (piece));
        }
        sample->stream.file = juce::File (whole.path);
        sample->stream.length = length;
        sample->stream.numChannels = sample->streamedChannels;
        sample->stream.resident = merged;
        return sample;
    }
}
