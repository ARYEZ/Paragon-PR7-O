// Ported from PR7-E 0.18.1 (stage 7a), less its tests of converting PR7-E projects and rigs from before its 0.16.0.
// The LIMITER after Pro-L 2 (0.16.0): what each part does to the sound, and
// the loudness meter against EBU's published test signals.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <thread>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/effects/Loudness.h"
#include "dsp/effects/MasterLimiter.h"
#include "rigs/Rig.h"

using Catch::Matchers::WithinAbs;
using effects::MasterLimiter;
using Style = effects::limiter::Style;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    const std::vector<Style> newStyles { Style::clear, Style::punchy, Style::dynamic, Style::allRound,
                                         Style::aggressive, Style::modern, Style::glue, Style::safe };

    MasterLimiter::Settings settings (Style style, float gainDb, float ceilingDb = -1.0f)
    {
        MasterLimiter::Settings s;
        s.on = true;
        s.style = style;
        s.gainDb = gainDb;
        s.ceilingDb = ceilingDb;
        return s;
    }

    void run (MasterLimiter& limiter, const MasterLimiter::Settings& s, juce::AudioBuffer<float>& buffer)
    {
        for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
        {
            juce::dsp::AudioBlock<float> block (buffer);
            limiter.process (block.getSubBlock ((size_t) start, (size_t) juce::jmin (blockSize, buffer.getNumSamples() - start)), s);
        }
    }

    juce::AudioBuffer<float> through (const MasterLimiter::Settings& s, juce::AudioBuffer<float> buffer)
    {
        MasterLimiter limiter;
        limiter.prepare (sampleRate, blockSize);
        run (limiter, s, buffer);
        return buffer;
    }

    juce::AudioBuffer<float> sine (double hz, float levelDb, int numSamples, float rightLevelDb = 1000.0f, double phase = 0.0)
    {
        juce::AudioBuffer<float> buffer (2, numSamples);
        for (int c = 0; c < 2; ++c)
        {
            const auto level = juce::Decibels::decibelsToGain (c == 1 && rightLevelDb < 999.0f ? rightLevelDb : levelDb);
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (c, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate + phase));
        }
        return buffer;
    }

    // Drum-like hits: noise bursts every 250 ms, jumping straight to full level, peaking near 0 dBFS.
    juce::AudioBuffer<float> drumHits (int numSamples, unsigned seed = 42)
    {
        juce::AudioBuffer<float> buffer (2, numSamples);
        juce::Random random ((juce::int64) seed);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto envelope = std::exp (-(float) (i % 12000) / (float) sampleRate * 25.0f);
            for (int c = 0; c < 2; ++c)
                buffer.setSample (c, i, envelope * (random.nextFloat() * 2.0f - 1.0f));
        }
        return buffer;
    }

    float peakDb (const juce::AudioBuffer<float>& b, int from = 0)
    {
        float most = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
            most = juce::jmax (most, b.getMagnitude (c, from, b.getNumSamples() - from));
        return juce::Decibels::gainToDecibels (most, -200.0f);
    }

    // An independent true-peak measurement, close to the ideal: the wave
    // rebuilt at 16 points between each pair of samples from a long (150-tap)
    // Kaiser-windowed sinc reaching 98% of the band. Much slower and finer
    // than the limiter's own detector.
    int lastPeakAt = 0;
    float truePeakDb (const juce::AudioBuffer<float>& b, int from = 0)
    {
        constexpr int half = 75, points = 16;
        constexpr double band = 0.98, beta = 9.0;
        const auto besselI0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        std::vector<std::vector<float>> table ((size_t) points, std::vector<float> ((size_t) half * 2));
        for (int p = 0; p < points; ++p)
        {
            double total = 0.0;
            std::vector<double> row ((size_t) half * 2);
            for (int k = -half + 1; k <= half; ++k)
            {
                const auto d = (double) p / points - k;
                const auto x = juce::MathConstants<double>::pi * d * band;
                const auto edge = d / (half + 0.5);
                const auto window = besselI0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - edge * edge))) / besselI0 (beta);
                row[(size_t) (k + half - 1)] = band * (std::abs (x) < 1.0e-12 ? 1.0 : std::sin (x) / x) * window;
                total += row[(size_t) (k + half - 1)];
            }
            for (size_t i = 0; i < row.size(); ++i)
                table[(size_t) p][i] = (float) (row[i] / total);
        }
        float most = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const auto* x = b.getReadPointer (c);
            for (int n = juce::jmax (from, half); n < b.getNumSamples() - half; ++n)
            {
                if (std::abs (x[n]) < 0.5f && std::abs (x[n + 1]) < 0.5f)
                    continue;   // far from the ceiling: can't peak near it in between
                for (int p = 0; p < points; ++p)
                {
                    float v = 0.0f;
                    const auto* h = table[(size_t) p].data();
                    for (int k = 0; k < half * 2; ++k)
                        v += h[k] * x[n - half + 1 + k];
                    if (std::abs (v) > most)
                    {
                        most = std::abs (v);
                        lastPeakAt = n;
                    }
                }
            }
        }
        return juce::Decibels::gainToDecibels (most, -200.0f);
    }

    // Recordings have little above 20 kHz (microphones and converters filter it): drumHits, low-passed at 18 kHz.
    juce::AudioBuffer<float> recordedHits (int numSamples)
    {
        auto hits = drumHits (numSamples);
        for (int c = 0; c < 2; ++c)
            for (int stage = 0; stage < 4; ++stage)
            {
                juce::dsp::IIR::Filter<float> filter (juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 18000.0f, 0.7071f));
                for (int i = 0; i < numSamples; ++i)
                    hits.setSample (c, i, filter.processSample (hits.getSample (c, i)));
            }
        hits.applyGain (1.0f / juce::jmax (hits.getMagnitude (0, 0, numSamples), hits.getMagnitude (1, 0, numSamples)));
        return hits;
    }

    // The level of `hz` in a stretch (Goertzel), as the RMS of that partial.
    double partialLevel (const float* x, int from, int length, double hz)
    {
        const auto w = juce::MathConstants<double>::twoPi * hz / sampleRate;
        const auto coefficient = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = from; i < from + length; ++i)
        {
            const auto s0 = x[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const auto power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
        return std::sqrt (2.0 * power) / length;
    }

    // Harmonic distortion of a sine at `hz`: the 2nd to 10th harmonics against the fundamental.
    double thd (const juce::AudioBuffer<float>& b, double hz, int from, int length)
    {
        const auto* x = b.getReadPointer (0);
        const auto fundamental = partialLevel (x, from, length, hz);
        double harmonics = 0.0;
        for (int h = 2; h <= 10; ++h)
            harmonics += juce::square (partialLevel (x, from, length, hz * h));
        return std::sqrt (harmonics) / fundamental;
    }
}

//==============================================================================
// The loudness meter, with EBU Tech 3341 and 3342's test signals.
namespace
{
    struct Readings
    {
        float momentary, shortTerm, integrated, range;
        float getMomentary() const { return momentary; }
        float getShortTerm() const { return shortTerm; }
        float getIntegrated() const { return integrated; }
        float getRange() const { return range; }
    };

    Readings meterAfter (const std::vector<std::pair<float, double>>& parts, double hz = 1000.0)
    {
        effects::LoudnessMeter meter;
        meter.prepare (sampleRate);
        for (const auto& [levelDb, seconds] : parts)
        {
            const auto buffer = sine (hz, levelDb, (int) (seconds * sampleRate));
            meter.process (buffer.getReadPointer (0), buffer.getReadPointer (1), buffer.getNumSamples());
        }
        return { meter.getMomentary(), meter.getShortTerm(), meter.getIntegrated(), meter.getRange() };
    }
}

TEST_CASE ("Loudness: a stereo 1 kHz sine at -23 dBFS reads -23 LUFS (EBU Tech 3341, cases 1 and 2)", "[limit][loudness]")
{
    const auto at23 = meterAfter ({ { -23.0f, 20.0 } });
    CHECK_THAT (at23.getMomentary(), WithinAbs (-23.0, 0.1));
    CHECK_THAT (at23.getShortTerm(), WithinAbs (-23.0, 0.1));
    CHECK_THAT (at23.getIntegrated(), WithinAbs (-23.0, 0.1));
    const auto at33 = meterAfter ({ { -33.0f, 20.0 } });
    CHECK_THAT (at33.getIntegrated(), WithinAbs (-33.0, 0.1));
}

TEST_CASE ("Loudness: INTEGRATED gates out the quiet parts (EBU Tech 3341, case 3)", "[limit][loudness]")
{
    // -36, -23, -36 dBFS: the -36 parts are 13 LU under, so the relative gate leaves them out.
    const auto meter = meterAfter ({ { -36.0f, 10.0 }, { -23.0f, 60.0 }, { -36.0f, 10.0 } });
    CHECK_THAT (meter.getIntegrated(), WithinAbs (-23.0, 0.1));
}

TEST_CASE ("Loudness: RANGE is the spread of the short-term loudness (EBU Tech 3342, case 1)", "[limit][loudness]")
{
    const auto meter = meterAfter ({ { -20.0f, 20.0 }, { -30.0f, 20.0 } });
    CHECK_THAT (meter.getRange(), WithinAbs (10.0, 1.0));
}

TEST_CASE ("Loudness: K-weighting lifts the highs and cuts the deep lows as BS.1770 says", "[limit][loudness]")
{
    // Against 1 kHz: 10 kHz reads 3.3 dB louder, 100 Hz 1.8 dB quieter, 20 Hz 14 dB quieter
    // (the curve's own response at 48 kHz).
    const auto at1k = meterAfter ({ { -20.0f, 5.0 } }).getMomentary();
    CHECK_THAT (meterAfter ({ { -20.0f, 5.0 } }, 10000.0).getMomentary() - at1k, WithinAbs (3.34, 0.1));
    CHECK_THAT (meterAfter ({ { -20.0f, 5.0 } }, 100.0).getMomentary() - at1k, WithinAbs (-1.83, 0.1));
    CHECK_THAT (meterAfter ({ { -20.0f, 5.0 } }, 20.0).getMomentary() - at1k, WithinAbs (-13.97, 0.3));
}

//==============================================================================
TEST_CASE ("Limiter: the true-peak detector finds the peaks between the samples", "[limit]")
{
    // A full-scale sine at a quarter of the sample rate, sampled 45 degrees off
    // its peaks: every sample is at -3 dB, but the wave between them reaches 0 dB.
    const auto quarter = sine (sampleRate / 4.0, 0.0f, 4800, 1000.0f, juce::MathConstants<double>::pi / 4.0);
    REQUIRE (peakDb (quarter) < -2.9f);
    effects::limiter::TruePeakDetector detector;
    float most = 0.0f;
    for (int i = 0; i < quarter.getNumSamples(); ++i)
        most = juce::jmax (most, detector.push (quarter.getSample (0, i)));
    CHECK_THAT (juce::Decibels::gainToDecibels (most), WithinAbs (0.0, 0.2));
    CHECK_THAT (truePeakDb (quarter), WithinAbs (0.0, 0.05));   // the tests' own measure agrees
}

TEST_CASE ("Limiter: no style lets a sample past the ceiling, with or without oversampling", "[limit]")
{
    const auto hits = drumHits (96000);
    for (const auto style : newStyles)
        for (const auto oversampling : { 0, 2 })
        {
            INFO ("style " << (int) style << ", oversampling " << oversampling);
            auto s = settings (style, 12.0f, -1.0f);
            s.oversampling = oversampling;
            CHECK (peakDb (through (s, hits)) <= -1.0f + 0.001f);
            s.lookaheadMs = 0.0f;   // none: close to clipping, but still under the ceiling
            CHECK (peakDb (through (s, hits)) <= -1.0f + 0.001f);
        }
}

TEST_CASE ("Limiter: TRUE PEAK keeps the peaks between the samples under the ceiling too", "[limit]")
{
    // Recorded hits driven 12 dB into it with no look-ahead: limiting that
    // fast makes peaks between the samples over the ceiling...
    const auto hits = recordedHits (96000);
    auto s = settings (Style::aggressive, 12.0f, -1.0f);
    s.lookaheadMs = 0.0f;
    CHECK (truePeakDb (through (s, hits), 2000) > -0.7f);
    // ...TRUE PEAK keeps them under (within 0.1 dB: true-peak meters differ by
    // that much). The hardest case, AGGRESSIVE's clipping with neither
    // look-ahead nor oversampling, makes so much aliasing that it's within
    // 0.2 dB: oversampling, as Pro-L 2's manual advises, brings it back.
    for (const auto style : { Style::aggressive, Style::modern, Style::clear })
        for (const auto oversampling : { 0, 1, 2 })
            for (const auto lookahead : { 0.0f, 1.0f })
            {
                INFO ("style " << (int) style << ", oversampling " << oversampling << ", look-ahead " << lookahead);
                auto t = settings (style, 12.0f, -1.0f);
                t.lookaheadMs = lookahead;
                t.truePeak = true;
                t.oversampling = oversampling;
                const auto hardest = style == Style::aggressive && oversampling == 0 && lookahead == 0.0f;
                CHECK (truePeakDb (through (t, hits), 2000) <= -1.0f + (hardest ? 0.2f : 0.1f));
            }
}

TEST_CASE ("Limiter: below the ceiling it changes nothing but the delay it reports", "[limit]")
{
    struct Case { float lookaheadMs; bool truePeak; int oversampling; };
    for (const auto& c : { Case { 1.0f, false, 0 }, Case { 5.0f, false, 0 }, Case { 0.0f, false, 0 }, Case { 1.0f, true, 0 },
                           Case { 1.0f, false, 2 }, Case { 2.0f, true, 1 }, Case { 1.0f, true, 3 } })
    {
        INFO ("look-ahead " << c.lookaheadMs << " ms, true peak " << c.truePeak << ", oversampling " << c.oversampling);
        auto s = settings (Style::modern, 0.0f, -1.0f);
        s.lookaheadMs = c.lookaheadMs;
        s.truePeak = c.truePeak;
        s.oversampling = c.oversampling;
        MasterLimiter limiter;
        limiter.prepare (sampleRate, blockSize);
        const auto latency = limiter.getLatencySamples (s);
        // Look-ahead; with TRUE PEAK, the detector's 32 samples, twice (the limiting and the last stage) and 1 ms.
        auto expected = c.oversampling == 0 ? (int) std::lround (c.lookaheadMs * 48.0) + (c.truePeak ? 32 + 48 + 32 : 0) : -1;
        if (expected >= 0)
            CHECK (latency == expected);

        // A tone at -20 dB: out unchanged, `latency` samples late.
        const auto input = sine (997.0, -20.0f, 48000);
        auto output = input;
        run (limiter, s, output);
        float worst = 0.0f;
        for (int i = 24000; i < 48000; ++i)
            worst = juce::jmax (worst, std::abs (output.getSample (0, i) - input.getSample (0, i - latency)));
        const auto tolerance = c.oversampling == 0 ? 1.0e-6f : 2.0e-3f;   // oversampling's filters: a whisker of ripple
        CHECK (worst < tolerance);
    }
}

TEST_CASE ("Limiter: the processor reports the LIMITER's delay to the DAW", "[limit]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);
    const auto set = [&processor] (const char* id, float value)
    {
        auto* p = processor.getParameters().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    set (ParamIDs::limitOn, 1.0f);
    set (ParamIDs::limitMode, (float) Style::modern);
    set (ParamIDs::limitLookahead, 2.0f);
    processor.updateLatency();
    CHECK (processor.getLatencySamples() == 96);
    set (ParamIDs::limitMode, (float) Style::brick);   // PR7-C's: none
    processor.updateLatency();
    CHECK (processor.getLatencySamples() == 0);
    set (ParamIDs::limitMode, (float) Style::modern);
    set (ParamIDs::limitOn, 0.0f);
    processor.updateLatency();
    CHECK (processor.getLatencySamples() == 0);
}

TEST_CASE ("Limiter: LOOKAHEAD makes heavy limiting cleaner", "[limit]")
{
    // A 100 Hz tone 5 dB over the ceiling.
    const auto tone = sine (100.0, -6.0f, 96000);
    auto s = settings (Style::clear, 10.0f, -1.0f);
    s.lookaheadMs = 0.0f;
    const auto none = thd (through (s, tone), 100.0, 48000, 48000);
    s.lookaheadMs = 5.0f;
    const auto five = thd (through (s, tone), 100.0, 48000, 48000);
    INFO ("THD with none " << none << ", with 5 ms " << five);
    CHECK (five < none * 0.5);
}

TEST_CASE ("Limiter: the styles sound different: AGGRESSIVE distorts most, SAFE least", "[limit]")
{
    const auto tone = sine (100.0, -6.0f, 96000);
    const auto distortion = [&tone] (Style style)
    {
        auto s = settings (style, 10.0f, -1.0f);
        s.lookaheadMs = 1.0f;
        return thd (through (s, tone), 100.0, 48000, 48000);
    };
    const auto aggressive = distortion (Style::aggressive), safe = distortion (Style::safe), clear = distortion (Style::clear);
    INFO ("aggressive " << aggressive << ", clear " << clear << ", safe " << safe);
    CHECK (aggressive > clear * 2.0);
    CHECK (safe < clear);
}

TEST_CASE ("Limiter: a steady bass note driven into it stays clean, and doesn't roughen what's over it (0.18.1)", "[limit]")
{
    // Up to 0.18.0 the transient stage let go a little after each cycle of a
    // bass note and pulled down again at the next: on 50 Hz driven 8 dB in,
    // 0.4% distortion (MODERN), and a 1 kHz tone over it shaken by the bass
    // with sidebands only 37 to 46 dB under it. It now holds at the level the
    // note needs. AGGRESSIVE's clipper distorts on purpose.
    const auto mix = [] (bool withTone)
    {
        auto bass = sine (50.0, -7.0f, 3 * 48000);
        if (withTone)
        {
            const auto tone = sine (1000.0, -20.0f, 3 * 48000);
            for (int c = 0; c < 2; ++c)
                bass.addFrom (c, 0, tone, c, 0, bass.getNumSamples());
        }
        return bass;
    };
    const auto bass = mix (false), both = mix (true);
    for (const auto style : newStyles)
    {
        if (style == Style::aggressive)
            continue;
        INFO ("style " << (int) style);
        CHECK (thd (through (settings (style, 8.0f), bass), 50.0, 2 * 48000, 48000) < 0.0005);   // under 0.05%

        const auto out = through (settings (style, 8.0f), both);
        const auto* x = out.getReadPointer (0);
        const auto tone = partialLevel (x, 2 * 48000, 48000, 1000.0);
        double sidebands = 0.0;
        for (const auto k : { -3, -2, -1, 1, 2, 3 })
            sidebands += juce::square (partialLevel (x, 2 * 48000, 48000, 1000.0 + 50.0 * k));
        const auto sidebandsDb = juce::Decibels::gainToDecibels (std::sqrt (sidebands) / tone);
        INFO ("the bass's sidebands round 1 kHz: " << sidebandsDb << " dB under it");
        CHECK (sidebandsDb < -55.0);   // was -37 to -46 dB; PUNCHY, which moves most on purpose, is now about -60
    }
}

TEST_CASE ("Limiter: DYNAMIC leaves a steady note alone: only real hits are lifted (0.18.1)", "[limit]")
{
    // Under the ceiling, nothing to limit: up to 0.18.0 DYNAMIC lifted each
    // cycle of a bass note a little, which shook a tone over it (-37 dB).
    auto both = sine (50.0, -9.0f, 3 * 48000);
    const auto tone = sine (1000.0, -20.0f, 3 * 48000);
    for (int c = 0; c < 2; ++c)
        both.addFrom (c, 0, tone, c, 0, both.getNumSamples());
    const auto out = through (settings (Style::dynamic, 0.0f), both);
    const auto* x = out.getReadPointer (0);
    double sidebands = 0.0;
    for (const auto k : { -2, -1, 1, 2 })
        sidebands += juce::square (partialLevel (x, 2 * 48000, 48000, 1000.0 + 50.0 * k));
    CHECK (juce::Decibels::gainToDecibels (std::sqrt (sidebands) / partialLevel (x, 2 * 48000, 48000, 1000.0)) < -90.0);
    CHECK (thd (out, 50.0, 2 * 48000, 48000) < 0.0001);
}

TEST_CASE ("Limiter: GLUE starts working under the ceiling, as a bus compressor would; CLEAR doesn't", "[limit]")
{
    // A steady tone peaking 3 dB under the ceiling: CLEAR leaves it be; GLUE,
    // whose release stage starts 6 dB under, takes most of the 3 dB over that
    // (its envelope sits a little under a sine's peaks).
    const auto tone = sine (1000.0, -4.0f, 96000);
    const auto change = [&tone] (Style style)
    {
        const auto out = through (settings (style, 0.0f, -1.0f), tone);
        return juce::Decibels::gainToDecibels (out.getRMSLevel (0, 48000, 48000) / tone.getRMSLevel (0, 48000, 48000));
    };
    CHECK (std::abs (change (Style::clear)) < 0.05f);
    CHECK (change (Style::glue) < -1.0f);
    CHECK (change (Style::glue) > -2.5f);
}

TEST_CASE ("Limiter: DYNAMIC keeps more of the hits' punch than CLEAR", "[limit]")
{
    // The same peaks (at the ceiling), but DYNAMIC's sustain sits lower under them: a higher crest factor.
    const auto hits = drumHits (96000);
    const auto crest = [&hits] (Style style)
    {
        const auto out = through (settings (style, 10.0f, -1.0f), hits);
        return juce::Decibels::gainToDecibels (out.getMagnitude (0, 48000, 48000) / out.getRMSLevel (0, 48000, 48000));
    };
    const auto dynamic = crest (Style::dynamic), clear = crest (Style::clear);
    INFO ("crest: dynamic " << dynamic << " dB, clear " << clear << " dB");
    CHECK (dynamic > clear + 0.5f);
}

TEST_CASE ("Limiter: the two LINK knobs: unlinked, a peak on one side leaves the other alone", "[limit]")
{
    // With GAIN +8 dB: left 3 dB over the ceiling, right well under it.
    const auto tone = sine (1000.0, -6.0f, 48000, -24.0f);
    const auto rightChange = [&tone] (float link)
    {
        auto s = settings (Style::modern, 8.0f, -1.0f);
        s.transientLink = s.releaseLink = link;
        const auto out = through (s, tone);
        const auto latency = 48;
        return juce::Decibels::gainToDecibels (out.getRMSLevel (1, 24000 + latency, 20000) / tone.getRMSLevel (1, 24000, 20000)) - 8.0f;
    };
    CHECK (std::abs (rightChange (0.0f)) < 0.05f);   // just GAIN's +8 dB
    CHECK (rightChange (1.0f) < -2.5f);              // linked: turned down with the left
}

TEST_CASE ("Limiter: UNITY plays it back at the input's level; DELTA plays only what it takes away", "[limit]")
{
    const auto quiet = sine (1000.0, -30.0f, 48000);
    auto s = settings (Style::modern, 10.0f, -1.0f);   // -20 dBFS out: well under the ceiling
    const auto level = [] (const juce::AudioBuffer<float>& b) { return juce::Decibels::gainToDecibels (b.getRMSLevel (0, 24000, 24000)); };
    CHECK_THAT (level (through (s, quiet)) - level (quiet), WithinAbs (10.0, 0.05));
    s.unity = true;
    CHECK_THAT (level (through (s, quiet)) - level (quiet), WithinAbs (0.0, 0.05));

    // DELTA: nothing from a tone it leaves alone; plenty from hits it limits.
    s.unity = false;
    s.delta = true;
    CHECK (level (through (s, quiet)) <= -100.0f);   // silence
    CHECK (level (through (s, drumHits (48000))) > -30.0f);
}

TEST_CASE ("Limiter: DC FILTER takes out an offset, and leaves the music", "[limit]")
{
    auto input = sine (100.0, -20.0f, 96000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < input.getNumSamples(); ++i)
            input.getWritePointer (c)[i] += 0.2f;
    auto s = settings (Style::modern, 0.0f, 0.0f);
    s.dcFilter = true;
    const auto out = through (s, input);
    double mean = 0.0;
    for (int i = 72000; i < 96000; ++i)
        mean += out.getSample (0, i);
    CHECK (std::abs (mean / 24000.0) < 0.001);
    CHECK_THAT (partialLevel (out.getReadPointer (0), 72000, 24000, 100.0) / partialLevel (input.getReadPointer (0), 72000, 24000, 100.0),
                WithinAbs (1.0, 0.01));
}

TEST_CASE ("Limiter: DITHER rounds to the bit depth with a little noise, and NOISE SHAPING moves that noise up", "[limit]")
{
    const auto noiseOf = [] (int shaping)
    {
        auto s = settings (Style::modern, 0.0f, -1.0f);
        s.dither = 5;   // 16 bits
        s.shaping = shaping;
        return through (s, sine (1000.0, -200.0f, 96000));   // silence
    };
    const auto flat = noiseOf (0);
    // On the 16-bit grid, at about -96 dBFS RMS (TPDF dither and the rounding: half a step).
    constexpr float step = 1.0f / 32768.0f;
    for (int i = 0; i < flat.getNumSamples(); i += 97)
        CHECK_THAT (flat.getSample (0, i) / step, WithinAbs (std::round (flat.getSample (0, i) / step), 1.0e-3));
    CHECK_THAT (juce::Decibels::gainToDecibels (flat.getRMSLevel (0, 0, 96000)), WithinAbs (-96.3, 0.5));   // half an LSB RMS

    // The noise's spectrum: below 4 kHz, every shaping is quieter than none; above 15 kHz, louder.
    const auto bandDb = [] (const juce::AudioBuffer<float>& b, double fromHz, double toHz)
    {
        double sum = 0.0;
        int n = 0;
        for (auto hz = fromHz; hz <= toHz; hz += 250.0, ++n)
            for (int from = 0; from + 4800 <= 96000; from += 4800)
                sum += juce::square (partialLevel (b.getReadPointer (0), from, 4800, hz));
        return 10.0 * std::log10 (sum / n);
    };
    for (const auto shaping : { 1, 2, 3 })
    {
        INFO ("shaping " << shaping);
        const auto shaped = noiseOf (shaping);
        CHECK (bandDb (shaped, 500.0, 4000.0) < bandDb (flat, 500.0, 4000.0) - 3.0);
        CHECK (bandDb (shaped, 15000.0, 20000.0) > bandDb (flat, 15000.0, 20000.0));
    }
}

TEST_CASE ("Limiter: the meters read input, output, true peak, reduction and loudness", "[limit]")
{
    MasterLimiter limiter;
    limiter.prepare (sampleRate, blockSize);
    auto s = settings (Style::modern, 6.0f, -1.0f);
    auto tone = sine (1000.0, -3.0f, 48000);
    run (limiter, s, tone);
    CHECK_THAT (limiter.getInputPeakDb(), WithinAbs (-3.0, 0.1));
    CHECK_THAT (limiter.getOutputPeakDb(), WithinAbs (-1.0, 0.1));
    CHECK_THAT (limiter.getReductionDb (0), WithinAbs (4.0, 0.3));
    CHECK (limiter.getLoudness().getMomentary() > -10.0f);
    std::vector<MasterLimiter::Frame> frames (200);
    const auto read = limiter.readFrames (frames.data(), (int) frames.size());
    CHECK (read >= 99);   // a frame every 10 ms
    CHECK_THAT (frames[(size_t) read - 1].reductionDb, WithinAbs (4.0, 0.3));
}

TEST_CASE ("Limiter: switching a new style on doesn't click", "[limit]")
{
    const auto tone = sine (200.0, -14.0f, 48000);
    auto out = tone;
    MasterLimiter limiter;
    limiter.prepare (sampleRate, blockSize);
    auto s = settings (Style::modern, 10.0f, -1.0f);
    s.on = false;
    for (int start = 0; start < out.getNumSamples(); start += blockSize)
    {
        s.on = start >= 24064;
        juce::dsp::AudioBlock<float> block (out);
        limiter.process (block.getSubBlock ((size_t) start, (size_t) juce::jmin (blockSize, out.getNumSamples() - start)), s);
    }
    const auto corner = [&out] (int from, int to)
    {
        float sharpest = 0.0f;
        const auto* x = out.getReadPointer (0);
        for (int i = from; i < to; ++i)
            sharpest = juce::jmax (sharpest, std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]));
        return sharpest;
    };
    const auto steady = juce::jmax (corner (12000, 24000), corner (40000, 47000));
    CHECK (corner (24000, 40000) < steady * 3.0f);
}

//==============================================================================
// Older projects and rigs
TEST_CASE ("Limiter: its heaviest settings still run comfortably in real time", "[limit][performance]")
{
    // TRUE PEAK with 8x oversampling and 5 ms look-ahead, hits driven 12 dB into it.
    auto s = settings (Style::modern, 12.0f, -1.0f);
    s.truePeak = true;
    s.oversampling = 3;
    s.lookaheadMs = 5.0f;
    const auto hits = drumHits (48000 * 5);
    MasterLimiter limiter;
    limiter.prepare (sampleRate, blockSize);
    auto buffer = hits;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    run (limiter, s, buffer);
    const auto fraction = (juce::Time::getMillisecondCounterHiRes() - start) / 5000.0;
    WARN ("The LIMITER at its heaviest uses " << juce::String (fraction * 100.0, 1) << "% of one core in real time");
    CHECK (fraction < 0.25);
}


TEST_CASE ("Limiter: after a kick, a dense mix comes straight back up (it doesn't pump), except in PUNCHY and GLUE", "[limit]")
{
    // 0.16.0 pumped: on a mix limited all the time, each kick's extra dip
    // lingered for a quarter of a second (the owner heard it in every
    // style). Kicks every 500 ms over a loud 2-8 kHz noise bed, GAIN +8 dB:
    // the bed is limited a little all the time, and more under each kick.
    juce::AudioBuffer<float> mix (2, 48000 * 4);
    juce::Random random (3);
    juce::dsp::IIR::Filter<float> hp (juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 2000.0f));
    juce::dsp::IIR::Filter<float> lp (juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 8000.0f));
    for (int i = 0; i < mix.getNumSamples(); ++i)
    {
        const auto t = (double) (i % 24000) / sampleRate;
        const auto kick = (float) (0.7 * std::exp (-t * 30.0) * std::sin (juce::MathConstants<double>::twoPi * 60.0 * t));
        const auto bed = 0.55f * lp.processSample (hp.processSample (random.nextFloat() * 2.0f - 1.0f));
        mix.setSample (0, i, kick + bed);
        mix.setSample (1, i, kick + bed);
    }
    // The bed's level (above 1.5 kHz, where the kick isn't) over a stretch after the last kick.
    const auto bedDb = [] (const juce::AudioBuffer<float>& b, int fromMs, int toMs)
    {
        juce::dsp::IIR::Filter<float> f1 (juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 1500.0f));
        juce::dsp::IIR::Filter<float> f2 (juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 1500.0f));
        const auto from = 72000 + 48 + fromMs * 48, to = 72000 + 48 + toMs * 48;   // after the 1 ms look-ahead
        double sum = 0.0;
        for (int i = from - 4800; i < to; ++i)
            if (const auto y = f2.processSample (f1.processSample (b.getSample (0, i))); i >= from)
                sum += (double) y * y;
        return 10.0 * std::log10 (sum / (to - from));
    };
    for (const auto style : newStyles)
    {
        if (style == Style::punchy || style == Style::glue)
            continue;   // they pump a little on purpose
        INFO ("style " << (int) style);
        const auto out = through (settings (style, 8.0f, -1.0f), mix);
        const auto usual = bedDb (out, 240, 480);   // limited as the bed alone needs
        CHECK (bedDb (out, 80, 120) - usual > -1.2);
        CHECK (bedDb (out, 120, 240) - usual > -0.7);
    }
}

TEST_CASE ("Limiter: its delay can be asked from another thread before it's prepared and while it's prepared again", "[limit]")
{
    // PR7-O 0.4.0: the processor's timer asks for the LIMITER's delay on the message thread while the host prepares the
    // audio on its own; getLatencySamples read the oversamplers prepare() replaces (and that didn't exist before the
    // first prepare): pluginval's parameter fuzzing crashed about one run in eight.
    effects::MasterLimiter limiter;
    auto s = settings (Style::modern, 6.0f, -1.0f);
    s.oversampling = 3;
    const auto before = limiter.getLatencySamples (s);   // never prepared: no oversamplers yet
    limiter.prepare (48000.0, 512);
    CHECK (limiter.getLatencySamples (s) == before);
    CHECK (before > effects::MasterLimiter::oversamplingLatency()[2]);
    std::atomic<bool> done { false };
    std::thread host ([&]
    {
        for (int i = 0; i < 200; ++i)
            limiter.prepare (i % 2 == 0 ? 44100.0 : 48000.0, 64 + 64 * (i % 8));
        done = true;
    });
    auto asked = 0;
    while (! done.load())
    {
        for (const auto oversampling : { 1, 2, 3 })
        {
            s.oversampling = oversampling;
            CHECK (limiter.getLatencySamples (s) > 0);
        }
        ++asked;
    }
    host.join();
    CHECK (asked > 0);
}
