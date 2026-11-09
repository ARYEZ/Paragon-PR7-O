// Shared by the tests that play the whole plugin: a steady test tone from the instrument. A sine made here, loaded as a
// one-region instrument rooted at middle C (60): played on note 60 at the processor's own rate it comes out sample for
// sample (the sampler's read is exact at the root key), so levels and frequencies can be measured as exactly as in the
// family's other plugins. Another note plays it repitched (`keyFor (semitones)`).
#pragma once

#include <catch2/catch_test_macros.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"

namespace testkit
{
    // A sample from a cycle's shape (0 to 1 in, -1 to 1 out), `seconds` long at `rate`, peak about `level`, on 1 or 2
    // channels; its last 3 ms fade out (as a synth note's release would), so its end doesn't click.
    template <typename Shape>
    std::shared_ptr<const sampler::Sample> tone (Shape shape, double hz, double seconds, float level, int channels, double rate)
    {
        auto sample = std::make_shared<sampler::Sample>();
        sample->name = "tone";
        sample->sampleRate = rate;
        const auto length = (size_t) (seconds * rate);
        const auto fade = std::min (length, (size_t) (0.003 * rate));
        sample->channels.assign ((size_t) channels, std::vector<float> (length));
        for (size_t i = 0; i < length; ++i)
        {
            const auto left = length - i;
            const auto gain = left < fade ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (double) left / (double) fade) : 1.0;
            const auto phase = std::fmod (hz * (double) i / rate, 1.0);
            for (auto& channel : sample->channels)
                channel[i] = (float) (level * gain * shape (phase));
        }
        return sample;
    }

    // A sine at `hz`.
    inline std::shared_ptr<const sampler::Sample> sine (double hz, double seconds = 20.0, float level = 0.5f, int channels = 1,
                                                             double rate = 48000.0)
    {
        return tone ([] (double p) { return std::sin (juce::MathConstants<double>::twoPi * p); }, hz, seconds, level, channels, rate);
    }

    // A band-limited saw at `hz` (its harmonics up to 20 kHz, each 1/n), as PR7-K's default oscillator played.
    inline std::shared_ptr<const sampler::Sample> saw (double hz, double seconds = 20.0, float level = 0.5f, int channels = 1,
                                                            double rate = 48000.0)
    {
        const auto harmonics = juce::jmax (1, (int) (20000.0 / hz));
        return tone ([harmonics] (double p)
        {
            auto sum = 0.0;
            for (int n = 1; n <= harmonics; ++n)
                sum += std::sin (juce::MathConstants<double>::twoPi * n * p) / n;
            return sum * 2.0 / juce::MathConstants<double>::pi * 0.85;   // peaks near 1 (the Gibbs overshoot under it)
        }, hz, seconds, level, channels, rate);
    }

    // Writes `audio` to `file` as a WAV: 32-bit float (reads back exactly) or `bits`-bit integer.
    inline void writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate, int bits = 32)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        REQUIRE (stream != nullptr);
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (audio.getNumChannels())
                                 .withBitsPerSample (bits)
                                 .withSampleFormat (bits == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                               : juce::AudioFormatWriterOptions::SampleFormat::integral);
        auto writer = wav.createWriterFor (stream, options);
        REQUIRE (writer != nullptr);
        REQUIRE (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()));
    }
    inline void writeWav (const juce::File& file, const sampler::Sample& sample, int bits = 32)
    {
        juce::AudioBuffer<float> audio ((int) sample.channels.size(), sample.length());
        for (int c = 0; c < audio.getNumChannels(); ++c)
            audio.copyFrom (c, 0, sample.channels[(size_t) c].data(), sample.length());
        writeWav (file, audio, sample.sampleRate, bits);
    }

    inline void set (ParagonX1AudioProcessor& processor, const juce::String& id, float plainValue)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    }

    // The instrument: `sample` over the whole keyboard, rooted at 60, at full level whatever the velocity (the SFZ's
    // amp_veltrack 0: tests measure levels, not velocity), released over 3 ms.
    inline void useSample (ParagonX1AudioProcessor& processor, std::shared_ptr<const sampler::Sample> sample)
    {
        auto program = sampler::programFrom (std::move (sample), 60);
        for (auto& r : program->regions)
        {
            r.velTrack = 0.0f;
            r.release = 0.003f;
        }
        processor.setProgram (std::move (program), "test tone");
    }

    // Several tones, each a region of its own on its own key (rooted there: each plays sample for sample), with its own
    // pan and level, as PR7-D's tests gave each drum slot a sine. Two tones on one key both play on it.
    struct Tone
    {
        int key = 60;
        std::shared_ptr<const sampler::Sample> sample;
        float pan = 0.0f, levelDb = 0.0f;
    };
    inline void useTones (ParagonX1AudioProcessor& processor, const std::vector<Tone>& tones)
    {
        auto program = std::make_shared<sampler::Program>();
        program->name = "test tones";
        for (const auto& t : tones)
        {
            sampler::Region r;
            r.sample = t.sample;
            r.loKey = r.hiKey = r.keyCenter = t.key;
            r.pan = t.pan;
            r.gain = juce::Decibels::decibelsToGain (t.levelDb);
            r.velTrack = 0.0f;
            r.release = 0.003f;
            program->regions.push_back (r);
        }
        program->index();
        processor.setProgram (std::move (program), "test tones");
    }

    // The key that plays the tone `semitones` above its own pitch (0: as recorded).
    inline int keyFor (int semitones = 0) { return 60 + semitones; }
}
