#include "dsp/effects/MasterLimiter.h"

namespace effects
{
    namespace
    {
        float coefficientFor (double sampleRate, float milliseconds) noexcept
        {
            return 1.0f - std::exp (-1.0f / (juce::jmax (1.0e-3f, milliseconds) * 0.001f * (float) sampleRate));
        }

        float toDb (float gain) noexcept { return gain > 1.0e-6f ? 20.0f * std::log10 (gain) : -120.0f; }
        float toGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

        // The 8x interpolation's seven in-between points (the eighth is the
        // sample itself): a Kaiser-windowed sinc over 64 samples, each point's
        // taps scaled to sum to 1.
        using Table = std::array<std::array<float, limiter::TruePeakDetector::taps>, limiter::TruePeakDetector::points - 1>;
        const Table& interpolation()
        {
            static const Table table = []
            {
                const auto besselI0 = [] (double x)
                {
                    double sum = 1.0, term = 1.0;
                    for (int k = 1; k < 30; ++k)
                    {
                        term *= (x / (2.0 * k)) * (x / (2.0 * k));
                        sum += term;
                    }
                    return sum;
                };
                constexpr int centre = limiter::TruePeakDetector::delay - 1;   // tap j is sample m + (j - centre)
                constexpr double cutoff = 0.96, beta = 6.0, halfSpan = limiter::TruePeakDetector::taps / 2 + 0.5;
                constexpr double pi = juce::MathConstants<double>::pi;
                Table t {};
                constexpr int points = limiter::TruePeakDetector::points;
                for (int point = 1; point < points; ++point)
                {
                    std::array<double, limiter::TruePeakDetector::taps> c {};
                    double total = 0.0;
                    for (int j = 0; j < limiter::TruePeakDetector::taps; ++j)
                    {
                        const auto distance = (double) point / points - (double) (j - centre);   // from sample m + (j - centre) to the point
                        const auto x = pi * cutoff * distance;
                        const auto sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (x) / x;
                        const auto edge = distance / halfSpan;
                        const auto window = besselI0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - edge * edge))) / besselI0 (beta);
                        c[(size_t) j] = cutoff * sinc * window;
                        total += c[(size_t) j];
                    }
                    for (int j = 0; j < limiter::TruePeakDetector::taps; ++j)
                        t[(size_t) point - 1][(size_t) j] = (float) (c[(size_t) j] / total);
                }
                return t;
            }();
            return table;
        }

        // The last stage (true peak, or catching what oversampling adds): only a quick transient stage.
        limiter::Character postCharacter() noexcept
        {
            limiter::Character c;
            c.slowAmount = 0.0f;
            c.fastReleaseMs = 20.0f;
            c.adapt = 0.0f;
            return c;
        }
    }

    namespace limiter
    {
        Character characterOf (Style style) noexcept
        {
            Character c;
            switch (style)
            {
                // (0.16.1: less pumping all round. The release stage follows the
                // average level, not each peak, and takes less of the work; the
                // transient stage lets go sooner, slowing down only under
                // limiting that goes on and on.)
                // Clear: stays out of the way.
                case Style::clear:      c = { 0.3f,  0.0f,  40.0f, 4.0f,  false, false }; break;
                // Punchy: the release stage does more of the work: a little
                // pumping, on purpose, for punch.
                case Style::punchy:     c = { 0.6f,  0.0f,  20.0f, 2.0f,  false, false }; break;
                // Dynamic: lifts transients (up to 4 dB, by how far a hit
                // stands above the level just before it) before limiting, so
                // the hits keep their attack.
                case Style::dynamic:    c = { 0.3f,  0.0f,  35.0f, 4.0f,  true,  false }; break;
                // All-round: between Clear and Punchy, easing in 1.5 dB under the ceiling.
                case Style::allRound:   c = { 0.4f,  1.5f,  30.0f, 4.0f,  false, false }; break;
                // Aggressive: a soft clipper rounds off the top 2 dB first; the
                // limiter quickly catches the rest. Loud and edgy.
                case Style::aggressive: c = { 0.2f,  0.0f,  12.0f, 2.0f,  false, true  }; break;
                // Modern: lets go fast after a lone peak (louder, cleaner with
                // little look-ahead) and slower under sustained limiting.
                case Style::modern:     c = { 0.3f,  0.0f,  20.0f, 6.0f,  false, false }; break;
                // Glue: the release stage starts 6 dB under the ceiling, as a
                // bus compressor would: glue (and some pumping), not transparency.
                case Style::glue:       c = { 0.6f,  6.0f,  30.0f, 2.0f,  false, false }; break;
                // Safe: slower, smooth recovery so it doesn't distort; not as loud.
                case Style::safe:       c = { 0.4f,  0.0f,  60.0f, 4.0f,  false, false }; break;
                case Style::brick: case Style::soft: case Style::autoRelease: break;
            }
            return c;
        }

        //======================================================================
        void TruePeakDetector::reset() noexcept
        {
            history.fill (0.0f);
            position = 0;
            previousBetween = 0.0f;
        }

        float TruePeakDetector::push (float sample, float atLeast) noexcept
        {
            // PR7-D 0.49.0: each sample written twice, `taps` apart, so the last `taps` samples always sit side by side
            // (history[position + 1 .. position + taps], oldest first) and the filter reads them with no wrapping index:
            // the same numbers added in the same order (bit for bit as before), in about half the time.
            history[(size_t) position] = sample;
            history[(size_t) (position + taps)] = sample;
            const auto* window = history.data() + position + 1;   // window[taps - 1] is the newest
            const auto& table = interpolation();
            // The points between m and m + 1, m = `delay` samples back: from the
            // samples m - (delay - 1) to m + delay (the newest).
            const auto centre = std::abs (window[taps - 1 - delay]);
            const auto next = std::abs (window[taps - delay]);
            float between = 0.0f;
            if (juce::jmax (centre, next) >= atLeast * 0.5f)
                for (const auto& point : table)
                {
                    float value = 0.0f;
                    for (int j = 0; j < taps; ++j)
                        value += point[(size_t) j] * window[j];
                    between = juce::jmax (between, std::abs (value));
                }
            const auto result = juce::jmax (centre, previousBetween, between);
            previousBetween = between;
            position = (position + 1) % taps;
            return result;
        }

        //======================================================================
        void Stage::prepare (double newSampleRate, int maxLookahead)
        {
            sampleRate = newSampleRate;
            capacity = maxLookahead + TruePeakDetector::delay + 2;
            for (auto& c : channels)
            {
                c.delayLine.assign ((size_t) capacity, 0.0f);
                c.holdValues.assign ((size_t) capacity, 1.0f);
                c.holdIndex.assign ((size_t) capacity, 0);
                c.boxValues.assign ((size_t) capacity, 1.0f);
            }
            reset();
        }

        void Stage::configure (int lookaheadSamples, bool detectTruePeaks, double rate) noexcept
        {
            lookaheadSamples = juce::jlimit (0, capacity - TruePeakDetector::delay - 2, lookaheadSamples);
            if (lookaheadSamples == lookahead && detectTruePeaks == truePeak && juce::approximatelyEqual (rate, sampleRate))
                return;
            lookahead = lookaheadSamples;
            truePeak = detectTruePeaks;
            sampleRate = rate;
            reset();
        }

        void Stage::reset() noexcept
        {
            for (auto& c : channels)
            {
                std::fill (c.delayLine.begin(), c.delayLine.end(), 0.0f);
                std::fill (c.boxValues.begin(), c.boxValues.end(), 1.0f);
                c.boxSum = (double) (lookahead + 1);
                c.holdFront = c.holdBack = 0;
                c.fast = 1.0f;
                c.slowDb = 0.0f;
                c.memory = 0.0f;
                c.meanSquare = 0.0f;
                c.sustainedDb = 0.0f;
                c.usualDb = 0.0f;
                c.windowLeast = 1.0f;
                c.windowCount = 0;
                c.detector.reset();
            }
            written = 0;
            boostFast = boostSlow = 0.0f;
        }

        void Stage::process (float* left, float* right, int numSamples, const Params& p, std::array<float, 2>& reduction) noexcept
        {
            const auto numChannels = right != nullptr ? 2 : 1;
            std::array<float*, 2> data { left, right };
            const auto window = lookahead + 1;
            const auto delay = (std::int64_t) getDelay();
            const auto& character = p.character;
            // How long limiting has gone on: rising over half a second of it, falling in 150 ms without.
            const auto memoryRise = coefficientFor (sampleRate, 500.0f), memoryFall = coefficientFor (sampleRate, 150.0f);
            const auto averageCoefficient = coefficientFor (sampleRate, 100.0f);
            const auto windowLength = juce::jmax (1, (int) (0.02 * sampleRate));
            const auto deeperCoefficient = 1.0f - std::exp (-20.0f / 1000.0f);     // per 20 ms window: 1 s
            const auto shallowerCoefficient = 1.0f - std::exp (-20.0f / 300.0f);   // 300 ms
            const auto usualCoefficient = coefficientFor (sampleRate, 20.0f);
            const auto boostAttack = coefficientFor (sampleRate, 0.5f), boostRelease = coefficientFor (sampleRate, 40.0f);
            const auto boostSlowAttack = coefficientFor (sampleRate, 15.0f), boostSlowRelease = coefficientFor (sampleRate, 250.0f);
            const auto clipKnee = p.ceiling * 0.7943282f, clipTop = p.ceiling * 1.2589254f;   // 2 dB either side

            for (int i = 0; i < numSamples; ++i)
            {
                std::array<float, 2> z { left[i], numChannels == 2 ? right[i] : 0.0f };

                // DYNAMIC: transients lifted before the limiting.
                if (character.boostTransients)
                {
                    const auto level = juce::jmax (std::abs (z[0]), std::abs (z[1]));
                    boostFast += (level - boostFast) * (level > boostFast ? boostAttack : boostRelease);
                    boostSlow += (level - boostSlow) * (level > boostSlow ? boostSlowAttack : boostSlowRelease);
                    // Only a real jump in level (over 1.5 dB above the level just
                    // before, eased in over 1.5 dB) is a hit: a steady note's own
                    // up and down, a bass note's cycles, isn't (0.18.1: before,
                    // it lifted each cycle a little, which distorted).
                    if (boostFast > 1.0e-5f && boostSlow > 1.0e-5f && boostFast > boostSlow * 1.1885f)
                    {
                        const auto jumpDb = toDb (boostFast / boostSlow) - 1.5f;
                        const auto boost = toGain (juce::jmin (4.0f, 0.5f * (jumpDb < 1.5f ? jumpDb * jumpDb / 3.0f : jumpDb - 0.75f)));
                        z[0] *= boost;
                        z[1] *= boost;
                    }
                }

                // The release stage: an envelope of how far over the ceiling (or its knee) the level is.
                std::array<float, 2> slowApplied {};
                if (character.slowAmount > 0.0f)
                {
                    // Its threshold is `knee` under the ceiling, eased in over a
                    // knee as wide (a hard threshold at the ceiling for most styles).
                    const auto knee = character.slowKneeDb;
                    const auto thresholdDb = p.ceilingDb - knee;
                    const auto from = p.ceiling * toGain (-1.5f * knee);
                    for (int c = 0; c < numChannels; ++c)
                    {
                        auto& channel = channels[(size_t) c];
                        float over = 0.0f;
                        // The average level (100 ms, as the peak a steady wave of
                        // that power would have), so a lone hit barely moves it.
                        const auto x2 = z[(size_t) c] * z[(size_t) c];
                        channel.meanSquare += (x2 - channel.meanSquare) * averageCoefficient;
                        if (const auto level = std::sqrt (2.0f * channel.meanSquare); level > from)
                        {
                            const auto x = toDb (level) - thresholdDb;
                            over = knee <= 0.0f      ? juce::jmax (0.0f, x)
                                 : x > 0.5f * knee   ? x
                                 : x < -0.5f * knee  ? 0.0f
                                                     : juce::square (x + 0.5f * knee) / (2.0f * knee);
                        }
                        const auto target = character.slowAmount * over;
                        channel.slowDb += (target - channel.slowDb) * (target > channel.slowDb ? p.attackCoefficient : p.releaseCoefficient);
                    }
                    const auto most = numChannels == 2 ? juce::jmax (channels[0].slowDb, channels[1].slowDb) : channels[0].slowDb;
                    for (int c = 0; c < numChannels; ++c)
                    {
                        const auto db = (1.0f - p.releaseLink) * channels[(size_t) c].slowDb + p.releaseLink * most;
                        if (db > 1.0e-4f)
                        {
                            z[(size_t) c] *= toGain (-db);
                            slowApplied[(size_t) c] = db;
                        }
                    }
                }

                // AGGRESSIVE: a soft clipper takes the very top first.
                if (character.clip)
                    for (int c = 0; c < numChannels; ++c)
                        if (const auto a = std::abs (z[(size_t) c]); a > clipKnee)
                            z[(size_t) c] = std::copysign (clipKnee + (clipTop - clipKnee) * std::tanh ((a - clipKnee) / (clipTop - clipKnee)), z[(size_t) c]);

                // The transient stage: the gain each sample needs, linked, then
                // the moving minimum and the moving average over the look-ahead.
                std::array<float, 2> needed { 1.0f, 1.0f };
                const auto slot = (size_t) (written % capacity);
                for (int c = 0; c < numChannels; ++c)
                {
                    auto& channel = channels[(size_t) c];
                    channel.delayLine[slot] = z[(size_t) c];
                    const auto level = truePeak ? channel.detector.push (z[(size_t) c], p.ceiling) : std::abs (z[(size_t) c]);
                    needed[(size_t) c] = level > p.ceiling ? p.ceiling / level : 1.0f;
                }
                if (numChannels == 2 && p.transientLink > 0.0f)
                {
                    const auto least = juce::jmin (needed[0], needed[1]);
                    if (least < 1.0f)
                        for (auto& g : needed)
                            if (g > least)
                                g *= std::pow (least / g, p.transientLink);
                }

                for (int c = 0; c < numChannels; ++c)
                {
                    auto& channel = channels[(size_t) c];
                    const auto g = needed[(size_t) c];
                    // The moving minimum over the last `window` samples.
                    while (channel.holdBack > channel.holdFront && channel.holdValues[(size_t) ((channel.holdBack - 1) % capacity)] >= g)
                        --channel.holdBack;
                    channel.holdValues[(size_t) (channel.holdBack % capacity)] = g;
                    channel.holdIndex[(size_t) (channel.holdBack % capacity)] = written;
                    ++channel.holdBack;
                    while (written - channel.holdIndex[(size_t) (channel.holdFront % capacity)] >= window)
                        ++channel.holdFront;
                    const auto held = channel.holdValues[(size_t) (channel.holdFront % capacity)];
                    // ...averaged over the same window: the gain glides down over the look-ahead.
                    auto& oldest = channel.boxValues[(size_t) (written % window)];
                    channel.boxSum += (double) held - (double) oldest;
                    oldest = held;
                    const auto boxed = juce::jmin (1.0f, (float) (channel.boxSum / (double) window));

                    // How much the music needs turning down as a rule (0.16.1): the
                    // deepest reduction of each 20 ms, smoothed: slowly deeper (1 s,
                    // so a hit hardly moves it), back quicker (300 ms). A dense
                    // mix sits at that level; a kick goes deeper for a moment.
                    channel.windowLeast = juce::jmin (channel.windowLeast, boxed);
                    if (++channel.windowCount >= windowLength)
                    {
                        const auto windowDb = -toDb (channel.windowLeast);
                        channel.sustainedDb += (windowDb - channel.sustainedDb) * (windowDb > channel.sustainedDb ? deeperCoefficient : shallowerCoefficient);
                        channel.windowLeast = 1.0f;
                        channel.windowCount = 0;
                    }

                    // Recovery: never above what's needed (it only rises towards it).
                    // Back up to the usual level quickly, whatever the music: so
                    // a hit's extra dip doesn't linger (that's pumping). Under
                    // limiting that goes on, no further than the usual level
                    // (0.18.1): before, it let go a little between the peaks, after
                    // each cycle of a bass note, and pulled down again at the next,
                    // a wobble that distorted the bass and roughened everything over
                    // it. The usual level eases back as the music gets quieter
                    // (300 ms); a lone peak (no limiting going on) still lets go all
                    // the way, quickly.
                    channel.usualDb += (channel.sustainedDb - channel.usualDb) * usualCoefficient;
                    const auto holdDb = channel.memory * channel.usualDb;
                    const auto target = juce::jmin (boxed, toGain (-holdDb));
                    if (boxed < channel.fast)
                        channel.fast = boxed;
                    else if (target > channel.fast)
                    {
                        const auto deeper = -toDb (channel.fast) > channel.sustainedDb + 0.5f;
                        const auto releaseMs = character.fastReleaseMs * p.fastReleaseScale * (deeper ? 1.0f : 1.0f + character.adapt * channel.memory);
                        channel.fast += (target - channel.fast) * coefficientFor (sampleRate, releaseMs);
                    }
                    const auto fastDb = -toDb (channel.fast);
                    // Only limiting that goes on and on slows the recovery (less distortion
                    // there); separate hits, however hard, keep it quick (no pumping).
                    channel.memory += fastDb > 0.5f ? (1.0f - channel.memory) * memoryRise : -channel.memory * memoryFall;

                    const auto out = channel.delayLine[(size_t) ((written - delay + capacity) % capacity)];
                    data[(size_t) c][i] = out * channel.fast;
                    reduction[(size_t) c] = juce::jmax (reduction[(size_t) c], slowApplied[(size_t) c] + fastDb);
                }
                ++written;
            }
        }
    }
}

namespace effects
{
    //==========================================================================
    void MasterLimiter::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);
        legacy.prepare (sampleRate, maxBlock);
        for (size_t i = 0; i < oversamplers.size(); ++i)
        {
            oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>> (2, i + 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                                               true, true);
            oversamplers[i]->initProcessing ((size_t) maxBlock);
        }
        const auto lookaheadBase = (int) std::ceil (limiter::maxLookaheadMs * 0.001 * sampleRate);
        main.prepare (sampleRate * 8.0, lookaheadBase * 8 + 8);
        post.prepare (sampleRate, (int) std::ceil (0.001 * sampleRate) + 2);
        configuration = {};

        work.setSize (2, maxBlock);
        dry.setSize (2, maxBlock);
        delayed.setSize (2, maxBlock);
        gains.assign ((size_t) maxBlock, 1.0f);
        const auto mostLatency = (int) oversamplers.back()->getLatencyInSamples() + lookaheadBase + limiter::TruePeakDetector::delay
                                 + (int) std::ceil (0.001 * sampleRate) + limiter::TruePeakDetector::delay + 4;
        dryCapacity = mostLatency + 1;
        dryLine.assign ((size_t) dryCapacity * 2, 0.0f);
        for (auto* smoothed : { &gain, &unity, &trim, &fade })
            smoothed->reset (sampleRate, 0.02);
        loudness.prepare (sampleRate);
        frameLength = juce::jmax (1, (int) std::lround (0.01 * sampleRate));
        reset();
    }

    void MasterLimiter::reset()
    {
        legacy.reset();
        main.reset();
        post.reset();
        for (auto& o : oversamplers)
            if (o != nullptr)
                o->reset();
        std::fill (dryLine.begin(), dryLine.end(), 0.0f);
        dryWrite = 0;
        dcIn = dcOut = {};
        for (auto& e : ditherError)
            e.fill (0.0f);
        for (auto& d : outputDetectors)
            d.reset();
        for (auto& r : reductionDb)
            r.store (0.0f);
        fresh = true;
    }

    MasterLimiter::Configuration MasterLimiter::configurationFor (const Settings& s) const noexcept
    {
        Configuration c;
        c.legacy = limiter::isLegacy (s.style);
        if (c.legacy)
        {
            c.lookahead = c.oversampling = 0;
            return c;
        }
        c.oversampling = juce::jlimit (0, 3, s.oversampling);
        const auto factor = 1 << c.oversampling;
        c.lookahead = (int) std::lround (juce::jlimit (0.0f, limiter::maxLookaheadMs, s.lookaheadMs) * 0.001 * sampleRate) * factor;
        c.truePeak = s.truePeak;
        return c;
    }

    int MasterLimiter::postLookahead (const Settings& s) const noexcept
    {
        // TRUE PEAK: 1 ms to ease out the peaks the limiting itself adds between
        // samples. Oversampling alone: 0.25 ms to catch what its filters add.
        if (limiter::isLegacy (s.style) || (! s.truePeak && s.oversampling == 0))
            return -1;   // no last stage
        return (int) std::lround ((s.truePeak ? 1.0 : 0.25) * 0.001 * sampleRate);
    }

    const std::array<int, 3>& MasterLimiter::oversamplingLatency()
    {
        // The same filters prepare() makes, measured once.
        static const auto measured = []
        {
            std::array<int, 3> latency {};
            for (size_t i = 0; i < latency.size(); ++i)
            {
                // (initialised as prepare() does: its whole-sample latency is set up there)
                juce::dsp::Oversampling<float> filters (2, i + 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
                filters.initProcessing (512);
                latency[i] = (int) filters.getLatencyInSamples();
            }
            return latency;
        }();
        return measured;
    }

    int MasterLimiter::getLatencySamples (const Settings& s) const noexcept
    {
        if (! s.on || limiter::isLegacy (s.style))
            return 0;
        const auto c = configurationFor (s);
        const auto factor = 1 << c.oversampling;
        // (PR7-O 0.4.0: from the delays measured once, not the oversamplers themselves: the processor's timer asks this
        // on the message thread, and the oversamplers are replaced by prepare() on the host's; before the first prepare
        // there were none. pluginval's parameter fuzzing crashed here about one run in eight.)
        auto latency = c.oversampling > 0 ? oversamplingLatency()[(size_t) c.oversampling - 1] : 0;
        const auto coreTruePeak = c.truePeak && factor <= 2;   // at 4x and up the samples are close enough
        latency += (c.lookahead + (coreTruePeak ? limiter::TruePeakDetector::delay : 0)) / factor;
        if (const auto last = postLookahead (s); last >= 0)
            latency += last + (s.truePeak ? limiter::TruePeakDetector::delay : 0);
        return latency;
    }

    void MasterLimiter::process (juce::dsp::AudioBlock<float> audio, const Settings& s) noexcept
    {
        // Longer than prepared for: in pieces.
        if ((int) audio.getNumSamples() > maxBlock)
        {
            for (size_t start = 0; start < audio.getNumSamples(); start += (size_t) maxBlock)
                process (audio.getSubBlock (start, juce::jmin ((size_t) maxBlock, audio.getNumSamples() - start)), s);
            return;
        }

        const auto numChannels = juce::jmin (2, (int) audio.getNumChannels());
        const auto numSamples = (int) audio.getNumSamples();
        const auto gainTarget = toGain (s.gainDb);
        const auto unityTarget = s.unity ? 1.0f / gainTarget : 1.0f;
        const auto trimTarget = toGain (s.trimDb);
        if (fresh)
        {
            // Playback is starting: be at the settings from the first sample.
            fresh = false;
            gain.setCurrentAndTargetValue (gainTarget);
            unity.setCurrentAndTargetValue (unityTarget);
            trim.setCurrentAndTargetValue (trimTarget);
            fade.setCurrentAndTargetValue (s.on ? 1.0f : 0.0f);
            warmUp = s.on ? 0 : -1;   // on from the start: its look-ahead is simply the delay
        }
        gain.setTargetValue (gainTarget);
        unity.setTargetValue (unityTarget);
        trim.setTargetValue (trimTarget);
        fade.setTargetValue (s.on ? 1.0f : 0.0f);

        if (! s.on && ! fade.isSmoothing())
        {
            warmUp = -1;   // starting again later: wait for the look-ahead to fill first
            for (auto& r : reductionDb)
                r.store (0.0f);
            for (auto* m : { &inputPeakDb, &outputPeakDb, &outputTruePeakDb })
                m->store (-100.0f);
            return;
        }

        // A change of look-ahead, oversampling or true peak starts the stages afresh.
        if (const auto c = configurationFor (s); ! (c == configuration))
        {
            configuration = c;
            if (! c.legacy)
            {
                const auto factor = 1 << c.oversampling;
                main.configure (c.lookahead, c.truePeak && factor <= 2, sampleRate * factor);
                if (const auto last = postLookahead (s); last >= 0)
                    post.configure (last, s.truePeak, sampleRate);
                if (c.oversampling > 0)
                    oversamplers[(size_t) c.oversampling - 1]->reset();
            }
            else
                legacy.reset();
        }
        auto whileOn = s;
        whileOn.on = true;   // fading out: still as late as while on
        const auto latency = getLatencySamples (whileOn);
        if (warmUp < 0)
            warmUp = latency;   // switched on from silent: the fade in starts once the stages have something to play

        // The input as it came (for the on/off fade and DELTA, delayed to line up), and the wet path's copy.
        for (int c = 0; c < numChannels; ++c)
        {
            dry.copyFrom (c, 0, audio.getChannelPointer ((size_t) c), numSamples);
            work.copyFrom (c, 0, audio.getChannelPointer ((size_t) c), numSamples);
        }
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                dryLine[(size_t) (dryWrite * 2 + c)] = dry.getSample (c, i);
                delayed.setSample (c, i, dryLine[(size_t) (((dryWrite - latency + dryCapacity) % dryCapacity) * 2 + c)]);
            }
            dryWrite = (dryWrite + 1) % dryCapacity;
        }

        // DC FILTER: a gentle 5 Hz low cut.
        if (s.dcFilter)
        {
            const auto r = std::exp (-juce::MathConstants<float>::twoPi * 5.0f / (float) sampleRate);
            for (int c = 0; c < numChannels; ++c)
                for (int i = 0; i < numSamples; ++i)
                {
                    auto& x = work.getWritePointer (c)[i];
                    const auto y = x - dcIn[(size_t) c] + r * dcOut[(size_t) c];
                    dcIn[(size_t) c] = x;
                    dcOut[(size_t) c] = y;
                    x = y;
                }
        }

        std::array<float, 2> reduction {};
        auto workBlock = juce::dsp::AudioBlock<float> (work).getSubsetChannelBlock (0, (size_t) numChannels).getSubBlock (0, (size_t) numSamples);
        for (int i = 0; i < numSamples; ++i)
            gains[(size_t) i] = gain.getNextValue();
        const auto gainAt = [this] (int i) { return gains[(size_t) i]; };

        if (configuration.legacy)
        {
            // BRICK, SOFT, AUTO: PR7-C's limiter: GAIN and OUTPUT as its threshold and make up.
            legacy.process (workBlock, { true, s.ceilingDb - s.gainDb, s.gainDb, (Limiter::Mode) s.style, s.mix, s.link });
            reduction = { legacy.getReductionDb (0), legacy.getReductionDb (1) };
        }
        else
        {
            for (int c = 0; c < numChannels; ++c)
                for (int i = 0; i < numSamples; ++i)
                    work.getWritePointer (c)[i] *= gainAt (i);

            limiter::Stage::Params p;
            p.ceilingDb = juce::jmin (0.0f, s.ceilingDb);
            p.ceiling = toGain (p.ceilingDb);
            p.character = limiter::characterOf (s.style);
            const auto factor = 1 << configuration.oversampling;
            p.attackCoefficient = coefficientFor (sampleRate * factor, s.attackMs);
            p.releaseCoefficient = coefficientFor (sampleRate * factor, s.releaseMs);
            p.fastReleaseScale = std::sqrt (juce::jmax (10.0f, s.releaseMs) / 250.0f);
            p.transientLink = juce::jlimit (0.0f, 1.0f, s.transientLink);
            p.releaseLink = juce::jlimit (0.0f, 1.0f, s.releaseLink);

            if (configuration.oversampling > 0)
            {
                auto& oversampler = *oversamplers[(size_t) configuration.oversampling - 1];
                auto up = oversampler.processSamplesUp (workBlock);
                main.process (up.getChannelPointer (0), numChannels == 2 ? up.getChannelPointer (1) : nullptr, (int) up.getNumSamples(), p, reduction);
                oversampler.processSamplesDown (workBlock);
            }
            else
                main.process (work.getWritePointer (0), numChannels == 2 ? work.getWritePointer (1) : nullptr, numSamples, p, reduction);

            if (const auto last = postLookahead (s); last >= 0)
            {
                auto q = p;
                q.character = postCharacter();
                // A hair under the ceiling (0.12 dB): for what this stage's own gain
                // changes add, and a peak the detector's 8 points miss (under 0.2 dB).
                q.ceilingDb = p.ceilingDb - (s.truePeak ? 0.12f : 0.0f);
                q.ceiling = toGain (q.ceilingDb);
                std::array<float, 2> more {};
                post.process (work.getWritePointer (0), numChannels == 2 ? work.getWritePointer (1) : nullptr, numSamples, q, more);
                reduction[0] += more[0];
                reduction[1] += more[1];
            }
        }
        if (numChannels == 1)
            reduction[1] = reduction[0];

        meter (juce::dsp::AudioBlock<float> (dry).getSubsetChannelBlock (0, (size_t) numChannels).getSubBlock (0, (size_t) numSamples),
               workBlock, reduction);

        // DELTA (against the input delayed to line up), UNITY, OUTPUT TRIM, and
        // the fade on or off. The fade is from the input as it comes, not
        // delayed: switching on glides from it to the (later) limited sound
        // rather than jumping back in time, which would click.
        for (int i = 0; i < numSamples; ++i)
        {
            const auto u = unity.getNextValue(), t = trim.getNextValue();
            const auto f = warmUp > 0 ? 0.0f : fade.getNextValue();
            if (warmUp > 0)
                --warmUp;
            const auto g = gainAt (i);
            for (int c = 0; c < numChannels; ++c)
            {
                auto wet = work.getSample (c, i);
                if (s.delta)
                    wet = delayed.getSample (c, i) * g - wet;
                wet *= u * t;
                const auto input = dry.getSample (c, i);
                audio.getChannelPointer ((size_t) c)[i] = input + (wet - input) * f;
            }
        }

        // DITHER: last of all.
        if (s.on && s.dither > 0)
            dither (audio.getSubsetChannelBlock (0, (size_t) numChannels), limiter::ditherBits[juce::jlimit (1, 5, s.dither)], s.shaping);
    }

    void MasterLimiter::meter (juce::dsp::AudioBlock<float> input, juce::dsp::AudioBlock<float> output, const std::array<float, 2>& reduction) noexcept
    {
        const auto numChannels = (int) output.getNumChannels();
        const auto numSamples = (int) output.getNumSamples();
        float in = 0.0f, out = 0.0f, truePeak = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            for (int i = 0; i < numSamples; ++i)
            {
                in = juce::jmax (in, std::abs (input.getSample (c, i)));
                const auto y = output.getSample (c, i);
                out = juce::jmax (out, std::abs (y));
                truePeak = juce::jmax (truePeak, outputDetectors[(size_t) c].push (y, truePeak));
            }
        inputPeakDb.store (toDb (in));
        outputPeakDb.store (toDb (out));
        outputTruePeakDb.store (toDb (truePeak));
        for (int c = 0; c < 2; ++c)
            reductionDb[(size_t) c].store (reduction[(size_t) c]);
        loudness.process (output.getChannelPointer (0), numChannels == 2 ? output.getChannelPointer (1) : nullptr, numSamples);

        // The level display: a frame every 10 ms.
        frameIn = juce::jmax (frameIn, in);
        frameOut = juce::jmax (frameOut, out);
        frameReduction = juce::jmax (frameReduction, reduction[0], reduction[1]);
        for (inFrame += numSamples; inFrame >= frameLength; inFrame -= frameLength)
        {
            const auto scope = frameFifo.write (1);
            if (scope.blockSize1 > 0)
                frames[(size_t) scope.startIndex1] = { toDb (frameIn), toDb (frameOut), frameReduction, loudness.getMomentary() };
            frameIn = frameOut = frameReduction = 0.0f;
        }
    }

    int MasterLimiter::readFrames (Frame* destination, int maxFrames) noexcept
    {
        const auto scope = frameFifo.read (juce::jmin (maxFrames, frameFifo.getNumReady()));
        for (int i = 0; i < scope.blockSize1; ++i)
            destination[i] = frames[(size_t) (scope.startIndex1 + i)];
        for (int i = 0; i < scope.blockSize2; ++i)
            destination[scope.blockSize1 + i] = frames[(size_t) (scope.startIndex2 + i)];
        return scope.blockSize1 + scope.blockSize2;
    }

    void MasterLimiter::dither (juce::dsp::AudioBlock<float> audio, int bits, int shaping) noexcept
    {
        // TPDF dither (two random values, 2 LSB peak to peak), then rounding to
        // the bit depth; NOISE SHAPING feeds the rounding error back so the
        // noise moves away from where the ear is most sensitive:
        //   BASIC      first order: less noise below about 7 kHz (at 44.1 kHz), more above
        //   OPTIMIZED  second order: lower still in the middle, more in the top octave
        //   WEIGHTED   9th order, shaped to the ear's sensitivity at low levels
        //              (designed for 44.1 kHz)
        static constexpr std::array<float, 9> basic { 1.0f }, optimized { 2.0f, -1.0f },
            weighted { 2.412f, -3.370f, 3.937f, -4.174f, 3.353f, -2.205f, 1.281f, -0.569f, 0.0847f };
        const auto& h = shaping == 1 ? basic : shaping == 2 ? optimized : weighted;
        const auto order = shaping == 1 ? 1 : shaping == 2 ? 2 : shaping == 3 ? 9 : 0;
        const auto step = 1.0f / (float) (1 << (bits - 1));
        for (size_t c = 0; c < audio.getNumChannels(); ++c)
        {
            auto& error = ditherError[c];
            auto* data = audio.getChannelPointer (c);
            for (size_t i = 0; i < audio.getNumSamples(); ++i)
            {
                float shaped = data[i];
                for (int k = 0; k < order; ++k)
                    shaped -= h[(size_t) k] * error[(size_t) k];
                const auto noise = (random.nextFloat() - random.nextFloat()) * step;
                const auto q = juce::jlimit (-1.0f, 1.0f - step, std::round ((shaped + noise) / step) * step);
                for (int k = order - 1; k > 0; --k)
                    error[(size_t) k] = error[(size_t) k - 1];
                if (order > 0)
                    error[0] = juce::jlimit (-4.0f * step, 4.0f * step, q - shaped);
                data[i] = q;
            }
        }
    }
}
