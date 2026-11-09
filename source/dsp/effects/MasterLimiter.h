#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "dsp/effects/EffectUtils.h"
#include "dsp/effects/Limiter.h"
#include "dsp/effects/Loudness.h"

namespace effects
{
    // PR7-E's LIMITER from 0.16.0 (stomp 7), inspired by FabFilter Pro-L 2.
    // Our own design: Pro-L 2's algorithms aren't published, so this follows
    // what its manual says each control does, not how it does it.
    //
    // GAIN drives the sound into a ceiling (OUTPUT) it never passes. Two stages:
    //   - a fast TRANSIENT stage that looks ahead (LOOKAHEAD, 0 to 5 ms): it
    //     sees each peak coming and eases the gain down over the look-ahead
    //     time, so it's at the right level exactly when the peak arrives. It
    //     can't let a peak through: the gain it applies to each sample is at
    //     most what that sample needs (a moving minimum, then a moving average
    //     over the same window). With no look-ahead it acts at once, close to
    //     clipping.
    //   - a slower RELEASE stage that follows the average level: ATTACK is
    //     how soon it sets in, RELEASE how long it holds. It takes some of the
    //     work off the fast stage, which keeps it cleaner.
    // Each has its own stereo LINK (Pro-L 2's two knobs).
    //
    // STYLE changes how the stages work together (see limiter::characterOf).
    // BRICK, SOFT and AUTO are PR7-C's limiter (effects::Limiter), unchanged,
    // for projects and rigs from before: no look-ahead, oversampling or true
    // peak for those.
    //
    // TRUE PEAK keeps the peaks a converter would rebuild between the samples
    // under the ceiling too: it detects them (4x interpolation), and a last
    // short look-ahead stage catches what the limiting itself adds.
    // OVERSAMPLING (2x to 8x) runs the limiting at a higher rate, for less
    // aliasing. Both add delay (reported to the DAW, see getLatencySamples).
    // Then UNITY (hear it at the input's level), DELTA (hear only what's
    // taken away), OUTPUT TRIM and DITHER, last. DC FILTER comes first.
    namespace limiter
    {
        enum class Style { brick, soft, autoRelease, clear, punchy, dynamic, allRound, aggressive, modern, glue, safe };
        inline constexpr int firstNewStyle = 3;
        inline bool isLegacy (Style style) noexcept { return (int) style < firstNewStyle; }

        // How a style uses the stages: our own numbers.
        struct Character
        {
            float slowAmount = 0.5f;     // how much of the over-level the release stage takes (0 to 1)
            float slowKneeDb = 0.0f;     // it starts this far under the ceiling (softly)
            float fastReleaseMs = 50.0f; // the transient stage's recovery...
            float adapt = 5.0f;          // ...stretched up to (1 + adapt) times under sustained limiting
            bool boostTransients = false;
            bool clip = false;           // a soft clipper takes the very top first
        };
        Character characterOf (Style) noexcept;

        inline const juce::StringArray oversamplingNames { "Off", "2x", "4x", "8x" };   // saved by index: only append
        inline const juce::StringArray ditherNames { "Off", "24 bits", "22 bits", "20 bits", "18 bits", "16 bits" };
        inline constexpr int ditherBits[] { 0, 24, 22, 20, 18, 16 };
        inline const juce::StringArray shapingNames { "None", "Basic", "Optimized", "Weighted" };
        inline constexpr float maxLookaheadMs = 5.0f;

        // Finds the peaks between the samples, as a converter would rebuild
        // them: an 8x windowed-sinc interpolation over 64 samples, reaching
        // 96% of the band. More than BS.1770's example (4x, 12 taps), which
        // misses peaks high in the band, where heavy limiting puts a lot.
        // push() returns the highest level at or either side of the sample
        // `delay` samples back.
        class TruePeakDetector
        {
        public:
            static constexpr int taps = 64, delay = 32, points = 8;
            void reset() noexcept;
            // Only where the samples either side are within 6 dB of `atLeast`
            // does it look between them (a peak between two samples doesn't
            // rise further over both in music): what matters is near the
            // ceiling, and this saves most of the work.
            float push (float sample, float atLeast = 0.0f) noexcept;

        private:
            std::array<float, 128> history {};
            int position = 0;
            float previousBetween = 0.0f;
        };

        // One look-ahead limiting pass over one or two channels.
        class Stage
        {
        public:
            struct Params
            {
                float ceiling = 1.0f, ceilingDb = 0.0f;
                Character character;
                float attackCoefficient = 1.0f, releaseCoefficient = 1.0f;   // the release stage
                float fastReleaseScale = 1.0f;
                float transientLink = 1.0f, releaseLink = 1.0f;             // 0 to 1
            };

            void prepare (double sampleRate, int maxLookahead);
            void reset() noexcept;
            void configure (int lookaheadSamples, bool truePeak, double sampleRate) noexcept;
            int getDelay() const noexcept { return lookahead + (truePeak ? TruePeakDetector::delay : 0); }

            // In place; right may be null. `reduction` gets each channel's most gain reduction (dB).
            void process (float* left, float* right, int numSamples, const Params&, std::array<float, 2>& reduction) noexcept;

        private:
            struct Channel
            {
                std::vector<float> delayLine, holdValues, boxValues;
                std::vector<std::int64_t> holdIndex;
                std::int64_t holdFront = 0, holdBack = 0;   // a monotonic queue: the moving minimum
                double boxSum = 0.0;
                float fast = 1.0f, slowDb = 0.0f, memory = 0.0f, meanSquare = 0.0f;
                float sustainedDb = 0.0f, windowLeast = 1.0f;   // the usual reduction (see process)
                float usualDb = 0.0f;                           // ...smoothed sample by sample (no steps each 20 ms)
                int windowCount = 0;
                TruePeakDetector detector;
            };

            double sampleRate = 48000.0;
            int capacity = 1, lookahead = 0;
            std::int64_t written = 0;
            bool truePeak = false;
            std::array<Channel, 2> channels;
            float boostFast = 0.0f, boostSlow = 0.0f;
        };
    }

    class MasterLimiter
    {
    public:
        struct Settings
        {
            bool on = false;
            limiter::Style style = limiter::Style::modern;
            float gainDb = 0.0f, ceilingDb = -1.0f;
            float lookaheadMs = 1.0f, attackMs = 20.0f, releaseMs = 250.0f;
            float transientLink = 0.75f, releaseLink = 1.0f;   // 0 to 1
            bool truePeak = false;
            int oversampling = 0;                              // index into oversamplingNames
            int dither = 0, shaping = 0;                       // indices into ditherNames, shapingNames
            bool unity = false, delta = false, dcFilter = false;
            float trimDb = 0.0f;
            float mix = 1.0f;                                  // BRICK, SOFT and AUTO only
            bool link = true;                                  // BRICK, SOFT and AUTO only
        };

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void process (juce::dsp::AudioBlock<float>, const Settings&) noexcept;

        // How late process() makes the sound with these settings (0 when off or for BRICK, SOFT, AUTO). Safe to ask from
        // any thread at any time (PR7-O 0.4.0: it no longer touches the oversamplers, which prepare() replaces).
        int getLatencySamples (const Settings&) const noexcept;
        // The oversamplers' own delay at 2x, 4x and 8x (samples at the host's rate: the same at any rate or block size).
        static const std::array<int, 3>& oversamplingLatency();
        double getSampleRate() const noexcept { return sampleRate; }

        // Meters (the last block; dB).
        float getReductionDb (int channel) const noexcept { return reductionDb[(size_t) juce::jlimit (0, 1, channel)].load(); }
        float getInputPeakDb() const noexcept { return inputPeakDb.load(); }
        float getOutputPeakDb() const noexcept { return outputPeakDb.load(); }
        float getOutputTruePeakDb() const noexcept { return outputTruePeakDb.load(); }
        LoudnessMeter& getLoudness() noexcept { return loudness; }

        // The level display's history: a frame each 10 ms, read by the UI.
        struct Frame { float inputDb, outputDb, reductionDb, momentaryLufs; };
        int readFrames (Frame* destination, int maxFrames) noexcept;

    private:
        struct Configuration
        {
            int lookahead = -1, oversampling = -1;
            bool truePeak = false, legacy = true;
            bool operator== (const Configuration&) const = default;
        };
        Configuration configurationFor (const Settings&) const noexcept;
        int postLookahead (const Settings&) const noexcept;
        void dither (juce::dsp::AudioBlock<float>, int bits, int shaping) noexcept;
        void meter (juce::dsp::AudioBlock<float> input, juce::dsp::AudioBlock<float> output, const std::array<float, 2>& reduction) noexcept;

        double sampleRate = 48000.0;
        int maxBlock = 512;
        bool fresh = true;
        Configuration configuration;

        Limiter legacy;
        limiter::Stage main, post;
        std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;

        juce::AudioBuffer<float> work, dry, delayed;
        std::vector<float> gains;   // GAIN, sample by sample, this block
        std::vector<float> dryLine;   // the input (with GAIN) delayed by the latency, both channels interleaved
        int dryCapacity = 1, dryWrite = 0;
        juce::SmoothedValue<float> gain { 1.0f }, unity { 1.0f }, trim { 1.0f };
        juce::SmoothedValue<float> fade { 0.0f };   // on/off
        int warmUp = -1;                            // samples to wait before fading in (-1: not yet known)
        std::array<float, 2> dcIn {}, dcOut {};
        std::array<std::array<float, 9>, 2> ditherError {};
        juce::Random random;

        // Meters
        std::array<std::atomic<float>, 2> reductionDb {};
        std::atomic<float> inputPeakDb { -100.0f }, outputPeakDb { -100.0f }, outputTruePeakDb { -100.0f };
        std::array<limiter::TruePeakDetector, 2> outputDetectors;
        LoudnessMeter loudness;
        int frameLength = 480, inFrame = 0;
        float frameIn = 0.0f, frameOut = 0.0f, frameReduction = 0.0f;
        std::vector<Frame> frames = std::vector<Frame> (4096);   // on the heap: the processor is sometimes on the stack (tests), and Windows' is small
        juce::AbstractFifo frameFifo { 4096 };
    };
}
