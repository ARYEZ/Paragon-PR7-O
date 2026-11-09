#include "dsp/sampler/Sample.h"

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
}
