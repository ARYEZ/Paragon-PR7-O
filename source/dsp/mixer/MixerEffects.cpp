#include "dsp/mixer/MixerEffects.h"

#include "dsp/effects/Compressor.h"
#include "dsp/effects/Drive.h"
#include "dsp/effects/LoFi.h"
#include "dsp/effects/Mars.h"
#include "dsp/effects/Modulation.h"
#include "dsp/eq/ParametricEq.h"

namespace mixerfx
{
    using mixer::FxSlot;
    using Values = std::array<float, mixer::maxFxValues>;

    namespace
    {
        Param knob (const char* name, float min, float max, float def, const char* unit, bool log = false)
        {
            Param p;
            p.name = name;
            p.min = min;
            p.max = max;
            p.def = def;
            p.unit = unit;
            p.log = log;
            return p;
        }
        Param choice (const char* name, const juce::StringArray& choices, int def)
        {
            Param p;
            p.name = name;
            p.min = 0.0f;
            p.max = (float) (choices.size() - 1);
            p.def = (float) def;
            p.choices = choices;
            return p;
        }
        Param offAtMin (Param p) { p.offText = "OFF"; return p; }
        Param offAtMax (Param p) { p.offText = "OFF"; p.offAtMax = true; return p; }

        // A second-order section (RBJ's cookbook), for side chains.
        struct Section
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
            float hz = -1.0f;
            void set (bool highPass, float frequency, double sampleRate) noexcept
            {
                if (std::abs (frequency - hz) < 1.0e-3f)
                    return;
                hz = frequency;
                const auto w = juce::MathConstants<double>::twoPi * juce::jlimit (5.0, 0.45 * sampleRate, (double) frequency) / sampleRate;
                const auto cosw = std::cos (w), alpha = std::sin (w) / (2.0 * 0.7071);
                const auto a0 = 1.0 + alpha;
                const auto b = highPass ? (1.0 + cosw) / 2.0 : (1.0 - cosw) / 2.0;
                b0 = (float) (b / a0);
                b1 = (float) ((highPass ? -(1.0 + cosw) : 1.0 - cosw) / a0);
                b2 = b0;
                a1 = (float) (-2.0 * cosw / a0);
                a2 = (float) ((1.0 - alpha) / a0);
            }
            float process (float x) noexcept
            {
                const auto y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
            void reset() noexcept { z1 = z2 = 0.0f; }
        };

        float coefficientFor (double ms, double sampleRate) noexcept
        {
            return (float) (1.0 - std::exp (-1.0 / juce::jmax (1.0, ms * 0.001 * sampleRate)));
        }
        float toDb (float x) noexcept { return 20.0f * std::log10 (juce::jmax (x, 1.0e-6f)); }
    }

    const std::vector<Param>& paramsOf (int type)
    {
        static const std::vector<Param> nothing;
        static const std::vector<Param> eqParams {
            offAtMin (knob ("LOW CUT", 20.0f, 1000.0f, 20.0f, "Hz", true)),
            knob ("LOW", 30.0f, 500.0f, 100.0f, "Hz", true),
            knob ("LOW GAIN", -18.0f, 18.0f, 0.0f, "dB"),
            knob ("MID 1", 100.0f, 8000.0f, 400.0f, "Hz", true),
            knob ("MID 1 GAIN", -18.0f, 18.0f, 0.0f, "dB"),
            knob ("MID 1 Q", 0.3f, 6.0f, 1.0f, "", true),
            knob ("MID 2", 500.0f, 16000.0f, 3000.0f, "Hz", true),
            knob ("MID 2 GAIN", -18.0f, 18.0f, 0.0f, "dB"),
            knob ("MID 2 Q", 0.3f, 6.0f, 1.0f, "", true),
            knob ("HIGH", 2000.0f, 16000.0f, 8000.0f, "Hz", true),
            knob ("HIGH GAIN", -18.0f, 18.0f, 0.0f, "dB"),
            offAtMax (knob ("HIGH CUT", 2000.0f, 20000.0f, 20000.0f, "Hz", true)),
        };
        static const std::vector<Param> compParams {
            knob ("THRESHOLD", -50.0f, 0.0f, -18.0f, "dB"),
            knob ("RATIO", 1.5f, 20.0f, 4.0f, ":1", true),
            knob ("ATTACK", 0.1f, 100.0f, 10.0f, "ms", true),
            knob ("RELEASE", 10.0f, 2000.0f, 200.0f, "ms", true),
            knob ("MAKEUP", 0.0f, 24.0f, 0.0f, "dB"),
            choice ("CHARACTER", { "VCA", "OPTO", "FET" }, 0),
            offAtMin (knob ("SC HPF", 20.0f, 500.0f, 20.0f, "Hz", true)),
            knob ("MIX", 0.0f, 100.0f, 100.0f, "%"),
        };
        static const std::vector<Param> gateParams {
            knob ("THRESHOLD", -80.0f, 0.0f, -40.0f, "dB"),
            knob ("RANGE", 0.0f, 80.0f, 80.0f, "dB"),
            knob ("ATTACK", 0.05f, 50.0f, 0.5f, "ms", true),
            knob ("HOLD", 0.0f, 500.0f, 30.0f, "ms"),
            knob ("RELEASE", 5.0f, 2000.0f, 150.0f, "ms", true),
            offAtMin (knob ("SC HPF", 20.0f, 2000.0f, 20.0f, "Hz", true)),
            offAtMax (knob ("SC LPF", 1000.0f, 20000.0f, 20000.0f, "Hz", true)),
        };
        static const std::vector<Param> transientParams {
            knob ("ATTACK", -100.0f, 100.0f, 0.0f, "%"),
            knob ("SUSTAIN", -100.0f, 100.0f, 0.0f, "%"),
        };
        static const std::vector<Param> driveParams {
            choice ("MODEL", effects::Drive::modelNames, 1),
            knob ("DRIVE", 0.0f, 10.0f, 5.0f, ""),
            knob ("TONE", 0.0f, 10.0f, 5.0f, ""),
        };
        static const std::vector<Param> loFiParams {
            choice ("TYPE", effects::LoFi::typeNames, 3),
            knob ("AMOUNT", 0.0f, 10.0f, 5.0f, ""),
            knob ("NOISE", 0.0f, 10.0f, 0.0f, ""),
            knob ("MIX", 0.0f, 100.0f, 100.0f, "%"),
        };
        static const std::vector<Param> modParams {
            choice ("TYPE", effects::Modulation::typeNames, 0),
            knob ("RATE", 0.05f, 10.0f, 0.8f, "Hz", true),
            knob ("DEPTH", 0.0f, 100.0f, 50.0f, "%"),
            knob ("MIX", 0.0f, 100.0f, 50.0f, "%"),
        };
        static const std::vector<Param> saturationParams = []
        {
            std::vector<Param> p {
                choice ("BANDS", { "1", "2", "3" }, 0),
                knob ("X 1", 40.0f, 2000.0f, 200.0f, "Hz", true),
                knob ("X 2", 500.0f, 12000.0f, 3000.0f, "Hz", true),
                knob ("MIX", 0.0f, 100.0f, 100.0f, "%"),
                choice ("QUALITY", { "1x", "4x", "8x" }, 1),
            };
            for (int b = 1; b <= saturationBands; ++b)
            {
                const auto n = " " + juce::String (b);
                auto style = choice ("STYLE", effects::Mars::styleNames, (int) effects::Mars::Style::warmTape);
                style.name << n;
                auto drive = knob ("DRIVE", 0.0f, 100.0f, 30.0f, "%");
                drive.name << n;
                auto dyn = knob ("DYN", -100.0f, 100.0f, 0.0f, "%");
                dyn.name << n;
                auto mix = knob ("MIX", 0.0f, 100.0f, 100.0f, "%");
                mix.name << n;
                auto level = knob ("LEVEL", -24.0f, 12.0f, 0.0f, "dB");
                level.name << n;
                p.insert (p.end(), { style, drive, dyn, mix, level });
            }
            return p;
        }();
        switch (type)
        {
            case saturation:  return saturationParams;
            case eq:          return eqParams;
            case compressor:  return compParams;
            case gate:        return gateParams;
            case transient:   return transientParams;
            case drive:       return driveParams;
            case loFi:        return loFiParams;
            case mod:         return modParams;
            default:          return nothing;
        }
    }

    FxSlot defaultSlot (int type)
    {
        FxSlot slot;
        slot.type = juce::jlimit (0, typeNames.size() - 1, type);
        const auto& params = paramsOf (slot.type);
        for (size_t i = 0; i < params.size() && i < slot.values.size(); ++i)
            slot.values[i] = params[i].def;
        return slot;
    }

    juce::String valueText (int type, int index, float value)
    {
        const auto& params = paramsOf (type);
        if (index < 0 || index >= (int) params.size())
            return {};
        const auto& p = params[(size_t) index];
        if (! p.choices.isEmpty())
            return p.choices[juce::jlimit (0, p.choices.size() - 1, juce::roundToInt (value))];
        if (p.offText.isNotEmpty() && (p.offAtMax ? value >= p.max - 1.0e-3f : value <= p.min + 1.0e-3f))
            return p.offText;
        if (p.unit == "Hz")
            return value >= 1000.0f ? juce::String (value / 1000.0f, value >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (value)) + " Hz";
        if (p.unit == "ms")
            return juce::String (value, value < 10.0f ? 2 : value < 100.0f ? 1 : 0) + " ms";
        if (p.unit == "%")
            return juce::String (juce::roundToInt (value)) + "%";
        if (p.unit == ":1")
            return juce::String (value, 1) + ":1";
        if (p.unit == "dB")
            return juce::String (value, 1) + " dB";
        return juce::String (value, 1);
    }

    //==========================================================================
    void SlotEffect::prepare (double sampleRate, int maxBlockSize)
    {
        bypass.prepare (sampleRate, maxBlockSize);
        bypass.setImmediately (true);
        output.reset (sampleRate, 0.02);
        prepareEffect (sampleRate, maxBlockSize);
    }

    void SlotEffect::reset()
    {
        resetEffect();
    }

    void SlotEffect::run (float* left, float* right, int numSamples, const FxSlot& slot) noexcept
    {
        float* channels[] = { left, right };
        juce::dsp::AudioBlock<float> block (channels, 2, (size_t) numSamples);
        output.setTargetValue (juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 12.0f, slot.outputDb)));
        bypass.process (block, slot.on, [&] (juce::dsp::AudioBlock<float> wet)
        {
            processEffect (wet.getChannelPointer (0), wet.getChannelPointer (1), numSamples, slot.values);
            if (! output.isSmoothing() && juce::approximatelyEqual (output.getTargetValue(), 1.0f))
                return;
            for (int i = 0; i < numSamples; ++i)
            {
                const auto g = output.getNextValue();
                wet.getChannelPointer (0)[i] *= g;
                wet.getChannelPointer (1)[i] *= g;
            }
        });
        if (! slot.on)
            reductionDb = 0.0f;
    }

    //==========================================================================
    namespace
    {
        // EQ: six of the parametric EQ's bands.
        class EqEffect final : public SlotEffect
        {
        public:
            EqEffect() : SlotEffect (eq) {}

        private:
            void prepareEffect (double rate, int block) override { equaliser.prepare (rate, block); }
            void resetEffect() override { equaliser.reset(); }
            void processEffect (float* l, float* r, int n, const Values& v) noexcept override
            {
                if (v != shown)
                {
                    shown = v;
                    using eq::Shape;
                    settings.on = true;
                    const auto band = [this] (int index, Shape shape, double hz, double gainDb, double q, bool used)
                    {
                        auto& b = settings.bands[(size_t) index];
                        b.used = used;
                        b.on = true;
                        b.shape = shape;
                        b.frequency = hz;
                        b.gainDb = gainDb;
                        b.q = q;
                    };
                    band (0, Shape::lowCut, v[0], 0.0, 0.707, v[0] > 20.5f);
                    band (1, Shape::lowShelf, v[1], v[2], 0.707, std::abs (v[2]) > 0.01f);
                    band (2, Shape::bell, v[3], v[4], v[5], std::abs (v[4]) > 0.01f);
                    band (3, Shape::bell, v[6], v[7], v[8], std::abs (v[7]) > 0.01f);
                    band (4, Shape::highShelf, v[9], v[10], 0.707, std::abs (v[10]) > 0.01f);
                    band (5, Shape::highCut, v[11], 0.0, 0.707, v[11] < 19990.0f);
                }
                float* channels[] = { l, r };
                equaliser.process (juce::dsp::AudioBlock<float> (channels, 2, (size_t) n), settings);
            }

            eq::ParametricEq equaliser;
            eq::ParametricEq::Settings settings;
            Values shown { -1.0f };
        };

        // COMPRESSOR: the plugin's, its side chain high-passed, MIX for parallel compression.
        class CompressorEffect final : public SlotEffect
        {
        public:
            CompressorEffect() : SlotEffect (compressor) {}

        private:
            void prepareEffect (double rate, int block) override
            {
                sampleRate = rate;
                comp.prepare (rate, block);
                sidechain.setSize (2, block);
                dry.setSize (2, block);
            }
            void resetEffect() override
            {
                comp.reset();
                for (auto& s : scFilters)
                    s.reset();
            }
            void processEffect (float* l, float* r, int n, const Values& v) noexcept override
            {
                effects::Compressor::Settings s;
                s.on = true;
                s.thresholdDb = v[0];
                s.ratio = juce::jmax (1.5f, v[1]);
                s.attackMs = v[2];
                s.releaseMs = v[3];
                s.makeUpDb = v[4];
                s.character = (effects::Compressor::Character) juce::jlimit (0, 2, juce::roundToInt (v[5]));
                s.link = true;
                const auto mix = juce::jlimit (0.0f, 1.0f, v[7] * 0.01f);
                float* channels[] = { l, r };
                for (int c = 0; c < 2; ++c)
                {
                    sidechain.copyFrom (c, 0, channels[c], n);
                    dry.copyFrom (c, 0, channels[c], n);
                    if (v[6] > 20.5f)
                    {
                        scFilters[(size_t) c].set (true, v[6], sampleRate);
                        auto* x = sidechain.getWritePointer (c);
                        for (int i = 0; i < n; ++i)
                            x[i] = scFilters[(size_t) c].process (x[i]);
                    }
                }
                juce::AudioBuffer<float> audio (channels, 2, n);
                comp.process (audio, sidechain, n, s);
                reductionDb = juce::jmax (comp.getReductionDb (0), comp.getReductionDb (1));
                if (mix < 0.999f)
                    for (int c = 0; c < 2; ++c)
                    {
                        const auto* d = dry.getReadPointer (c);
                        for (int i = 0; i < n; ++i)
                            channels[c][i] = d[i] + (channels[c][i] - d[i]) * mix;
                    }
            }

            double sampleRate = 48000.0;
            effects::Compressor comp;
            juce::AudioBuffer<float> sidechain, dry;
            std::array<Section, 2> scFilters;
        };

        // GATE (ours): a peak detector on the side chain (both sides together, high- and low-passed); open above
        // THRESHOLD, held for HOLD after the level falls back, then closing. It moves in dB, straight: opening the
        // whole RANGE takes ATTACK, closing it RELEASE. It starts shut (silence before the first hit).
        class GateEffect final : public SlotEffect
        {
        public:
            GateEffect() : SlotEffect (gate) {}

        private:
            void prepareEffect (double rate, int) override { sampleRate = rate; resetEffect(); }
            void resetEffect() override
            {
                envelope = 0.0f;
                gainDb = -200.0f;   // shut (set to -RANGE on the first block)
                holdLeft = 0;
                highPass.reset();
                lowPass.reset();
            }
            void processEffect (float* l, float* r, int n, const Values& v) noexcept override
            {
                const auto threshold = juce::Decibels::decibelsToGain (v[0]);
                const auto range = juce::jlimit (0.0f, 80.0f, v[1]);
                const auto up = range / (float) juce::jmax (1.0, v[2] * 0.001 * sampleRate);
                const auto down = range / (float) juce::jmax (1.0, v[4] * 0.001 * sampleRate);
                const auto hold = (int) (v[3] * 0.001 * sampleRate);
                const auto detectorFall = coefficientFor (5.0, sampleRate);   // the detector's own release: no chatter in a cycle
                const auto useHigh = v[5] > 20.5f, useLow = v[6] < 19990.0f;
                if (useHigh)
                    highPass.set (true, v[5], sampleRate);
                if (useLow)
                    lowPass.set (false, v[6], sampleRate);
                gainDb = juce::jlimit (-range, 0.0f, gainDb);
                auto lowestDb = 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    auto sc = 0.5f * (l[i] + r[i]);
                    if (useHigh)
                        sc = highPass.process (sc);
                    if (useLow)
                        sc = lowPass.process (sc);
                    const auto level = std::abs (sc);
                    envelope = level > envelope ? level : envelope + (level - envelope) * detectorFall;
                    auto open = false;
                    if (envelope >= threshold)
                    {
                        holdLeft = hold;
                        open = true;
                    }
                    else if (holdLeft > 0)
                    {
                        --holdLeft;
                        open = true;
                    }
                    gainDb = open ? juce::jmin (0.0f, gainDb + up) : juce::jmax (-range, gainDb - down);
                    const auto g = gainDb <= -79.9f ? 0.0f : juce::Decibels::decibelsToGain (gainDb);
                    l[i] *= g;
                    r[i] *= g;
                    lowestDb = juce::jmin (lowestDb, gainDb);
                }
                reductionDb = -lowestDb;
            }

            double sampleRate = 48000.0;
            float envelope = 0.0f, gainDb = -200.0f;
            int holdLeft = 0;
            Section highPass, lowPass;
        };

        // TRANSIENT (ours): ATTACK compares a fast-attack envelope with a slow-attack one (they differ only at each
        // hit's front); SUSTAIN a slow-release one with a fast-release one (they differ only in the ring). Each
        // difference in dB, scaled by its knob (100%: all of it), moves the gain; limited to +-24 dB and smoothed.
        class TransientEffect final : public SlotEffect
        {
        public:
            TransientEffect() : SlotEffect (transient) {}

        private:
            void prepareEffect (double rate, int) override
            {
                sampleRate = rate;
                fastAttack = coefficientFor (0.5, rate);
                slowAttack = coefficientFor (25.0, rate);
                fastRelease = coefficientFor (30.0, rate);
                slowRelease = coefficientFor (300.0, rate);
                commonRelease = coefficientFor (60.0, rate);
                smoothing = coefficientFor (1.0, rate);
                resetEffect();
            }
            void resetEffect() override { e1 = e2 = e3 = e4 = 0.0f; gainDb = 0.0f; }
            void processEffect (float* l, float* r, int n, const Values& v) noexcept override
            {
                const auto attackAmount = juce::jlimit (-1.0f, 1.0f, v[0] * 0.01f), sustainAmount = juce::jlimit (-1.0f, 1.0f, v[1] * 0.01f);
                const auto follow = [] (float& e, float x, float up, float down) { e += (x - e) * (x > e ? up : down); };
                for (int i = 0; i < n; ++i)
                {
                    const auto x = juce::jmax (std::abs (l[i]), std::abs (r[i]));
                    follow (e1, x, fastAttack, commonRelease);
                    follow (e2, x, slowAttack, commonRelease);
                    follow (e3, x, fastAttack, fastRelease);
                    follow (e4, x, fastAttack, slowRelease);
                    const auto front = juce::jmax (0.0f, toDb (e1) - toDb (e2));
                    const auto ring = juce::jmax (0.0f, toDb (e4) - toDb (e3));
                    const auto target = juce::jlimit (-24.0f, 24.0f, attackAmount * front + sustainAmount * ring);
                    gainDb += (target - gainDb) * smoothing;
                    const auto g = juce::Decibels::decibelsToGain (gainDb);
                    l[i] *= g;
                    r[i] *= g;
                }
            }

            double sampleRate = 48000.0;
            float fastAttack = 0.0f, slowAttack = 0.0f, fastRelease = 0.0f, slowRelease = 0.0f, commonRelease = 0.0f, smoothing = 0.0f;
            float e1 = 0.0f, e2 = 0.0f, e3 = 0.0f, e4 = 0.0f, gainDb = 0.0f;
        };

        // DRIVE: the plugin's pedals, one a side, a DC blocker after (as on the insert buses).
        class DriveEffect final : public SlotEffect
        {
        public:
            DriveEffect() : SlotEffect (drive) {}

        private:
            void prepareEffect (double rate, int block) override
            {
                for (auto& d : drives)
                    d.prepare (rate, block);
                dcCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 5.0f / (float) rate);
                dc = {};
            }
            void resetEffect() override { dc = {}; }
            void processEffect (float* l, float* r, int n, const Values& v) noexcept override
            {
                effects::Drive::Settings s;
                s.on = true;
                s.model = (effects::Drive::Model) juce::jlimit (0, effects::Drive::modelNames.size() - 1, juce::roundToInt (v[0]));
                s.drive = v[1];
                s.tone = v[2];
                float* channels[] = { l, r };
                for (size_t c = 0; c < 2; ++c)
                {
                    float* one[] = { channels[c] };
                    drives[c].process (juce::dsp::AudioBlock<float> (one, 1, (size_t) n), s);
                    for (int i = 0; i < n; ++i)
                    {
                        dc[c] += (channels[c][i] - dc[c]) * dcCoefficient;
                        channels[c][i] -= dc[c];
                    }
                }
            }

            std::array<effects::Drive, 2> drives;
            std::array<float, 2> dc {};
            float dcCoefficient = 0.0f;
        };

        // SATURATION (0.35.0): MARS, one a side (it's mono, as in PR7-X; each side's DYNAMICS its own).
        class SaturationEffect final : public SlotEffect
        {
        public:
            SaturationEffect() : SlotEffect (saturation) {}

        private:
            void prepareEffect (double rate, int block) override
            {
                for (auto& m : mars)
                    m.prepare (rate, block);
            }
            // (reset only follows prepare, which clears MARS: nothing more.)
            void processEffect (float* l, float* r, int n, const Values& v) noexcept override
            {
                using effects::Mars;
                settings.on = true;
                settings.numBands = juce::jlimit (1, saturationBands, juce::roundToInt (v[0]) + 1);
                const auto x1 = juce::jlimit (20.0f, 18000.0f, v[1]);
                settings.crossoverHz = { x1, juce::jmax (x1 * 1.25f, juce::jlimit (20.0f, 18000.0f, v[2])), 16000.0f };
                settings.slope = Mars::Slope::db24;
                settings.inputDb = settings.outputDb = 0.0f;
                settings.mix = juce::jlimit (0.0f, 1.0f, v[3] / 100.0f);
                settings.quality = (Mars::Quality) juce::jlimit (0, 2, juce::roundToInt (v[4]));
                for (int b = 0; b < Mars::maxBands; ++b)
                {
                    auto& band = settings.bands[(size_t) b];
                    if (b >= saturationBands)
                    {
                        band = settings.bands[0];
                        continue;
                    }
                    const auto* at = v.data() + saturationBandsAt + b * saturationPerBand;
                    band.on = true;
                    band.style = (Mars::Style) juce::jlimit (0, Mars::numStyles - 1, juce::roundToInt (at[0]));
                    band.drive = juce::jlimit (0.0f, 1.0f, at[1] / 100.0f);
                    band.dynamics = juce::jlimit (-1.0f, 1.0f, at[2] / 100.0f);
                    band.mix = juce::jlimit (0.0f, 1.0f, at[3] / 100.0f);
                    band.levelDb = juce::jlimit (-24.0f, 12.0f, at[4]);
                }
                float* channels[] = { l, r };
                for (size_t c = 0; c < 2; ++c)
                {
                    float* one[] = { channels[c] };
                    mars[c].process (juce::dsp::AudioBlock<float> (one, 1, (size_t) n), settings);
                }
            }

            std::array<effects::Mars, 2> mars;
            effects::Mars::Settings settings;
        };

        class LoFiEffect final : public SlotEffect
        {
        public:
            LoFiEffect() : SlotEffect (loFi) {}

        private:
            void prepareEffect (double rate, int block) override { effect.prepare (rate, block); }
            void processEffect (float* l, float* r, int n, const Values& v) noexcept override
            {
                effects::LoFi::Settings s;
                s.on = true;
                s.type = (effects::LoFi::Type) juce::jlimit (0, effects::LoFi::typeNames.size() - 1, juce::roundToInt (v[0]));
                s.amount = v[1];
                s.noise = v[2];
                s.mix = juce::jlimit (0.0f, 1.0f, v[3] * 0.01f);
                float* channels[] = { l, r };
                effect.process (juce::dsp::AudioBlock<float> (channels, 2, (size_t) n), s);
            }

            effects::LoFi effect;
        };

        class ModEffect final : public SlotEffect
        {
        public:
            ModEffect() : SlotEffect (mod) {}

        private:
            void prepareEffect (double rate, int block) override { effect.prepare (rate, block); }
            void processEffect (float* l, float* r, int n, const Values& v) noexcept override
            {
                effects::Modulation::Settings s;
                s.on = true;
                s.type = (effects::Modulation::Type) juce::jlimit (0, effects::Modulation::typeNames.size() - 1, juce::roundToInt (v[0]));
                s.rateHz = v[1];
                s.depth = juce::jlimit (0.0f, 1.0f, v[2] * 0.01f);
                s.mix = juce::jlimit (0.0f, 1.0f, v[3] * 0.01f);
                float* channels[] = { l, r };
                effect.process (juce::dsp::AudioBlock<float> (channels, 2, (size_t) n), s);
            }

            effects::Modulation effect;
        };
    }

    std::unique_ptr<SlotEffect> make (int type)
    {
        switch (type)
        {
            case saturation:  return std::make_unique<SaturationEffect>();
            case eq:          return std::make_unique<EqEffect>();
            case compressor:  return std::make_unique<CompressorEffect>();
            case gate:        return std::make_unique<GateEffect>();
            case transient:   return std::make_unique<TransientEffect>();
            case drive:       return std::make_unique<DriveEffect>();
            case loFi:        return std::make_unique<LoFiEffect>();
            case mod:         return std::make_unique<ModEffect>();
            default:          return nullptr;
        }
    }

    //==========================================================================
    Host::Host()
    {
        for (auto& p : live)
            p = nullptr;
    }

    void Host::prepare (double rate, int block)
    {
        sampleRate = rate;
        blockSize = juce::jmax (1, block);
        for (auto& effect : owned)
            if (effect != nullptr)
            {
                effect->prepare (sampleRate, blockSize);
                effect->reset();
            }
    }

    void Host::sync (const mixer::FxTable& slots)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        for (int strip = 0; strip < mixer::numStrips; ++strip)
            for (int k = 0; k < mixer::numFxSlots; ++k)
            {
                const auto index = (size_t) (strip * mixer::numFxSlots + k);
                const auto type = slots[(size_t) strip][(size_t) k].type;
                auto& current = owned[index];
                if ((current == nullptr ? 0 : current->type) == type)
                    continue;
                std::shared_ptr<SlotEffect> made (make (type));
                if (made != nullptr)
                {
                    made->prepare (sampleRate, blockSize);
                    made->reset();
                }
                live[index] = made.get();
                if (current != nullptr)
                    retired.emplace_back (current, now);
                current = made;
            }
        freeRetired();
    }

    void Host::freeRetired()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        std::erase_if (retired, [now] (const auto& r) { return now - r.second > 2000.0; });
    }

    bool Host::hasEffects (int strip) const noexcept
    {
        if (table == nullptr || strip < 0 || strip >= mixer::numStrips)
            return false;
        for (int k = 0; k < mixer::numFxSlots; ++k)
        {
            const auto& slot = (*table)[(size_t) strip][(size_t) k];
            const auto* effect = live[(size_t) (strip * mixer::numFxSlots + k)].load();
            if (slot.isLoaded() && effect != nullptr && effect->type == slot.type)
                return true;
        }
        return false;
    }

    void Host::process (int strip, float* left, float* right, int numSamples) noexcept
    {
        if (table == nullptr || strip < 0 || strip >= mixer::numStrips)
            return;
        for (int k = 0; k < mixer::numFxSlots; ++k)
        {
            const auto& slot = (*table)[(size_t) strip][(size_t) k];
            auto* effect = live[(size_t) (strip * mixer::numFxSlots + k)].load();
            if (slot.isLoaded() && effect != nullptr && effect->type == slot.type)
                effect->run (left, right, numSamples, slot);
        }
    }

    float Host::getReductionDb (int strip, int slot) const noexcept
    {
        if (strip < 0 || strip >= mixer::numStrips || slot < 0 || slot >= mixer::numFxSlots)
            return 0.0f;
        const auto& effect = owned[(size_t) (strip * mixer::numFxSlots + slot)];
        return effect != nullptr ? effect->getReductionDb() : 0.0f;
    }

    int Host::getLoadedType (int strip, int slot) const noexcept
    {
        if (strip < 0 || strip >= mixer::numStrips || slot < 0 || slot >= mixer::numFxSlots)
            return 0;
        const auto& effect = owned[(size_t) (strip * mixer::numFxSlots + slot)];
        return effect != nullptr ? effect->type : 0;
    }
}
