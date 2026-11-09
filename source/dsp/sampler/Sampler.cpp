#include "dsp/sampler/Sampler.h"

#include <cmath>

namespace sampler
{
    namespace
    {
        constexpr float minReleaseSeconds = 0.005f;
        constexpr float silent = 1.0e-4f;   // -80 dB: a releasing voice ends here

        // Catmull-Rom (Hermite) through x1 at t = 0 and x2 at t = 1: exactly x1 when t is 0.
        inline float hermite (float x0, float x1, float x2, float x3, float t) noexcept
        {
            const auto c1 = 0.5f * (x2 - x0);
            const auto c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
            const auto c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
            return ((c3 * t + c2) * t + c1) * t + x1;
        }

        // A coefficient that takes a level down by `db` (negative) over `samples` samples.
        inline float fallCoef (float db, double samples) noexcept
        {
            return samples <= 1.0 ? 0.0f : (float) std::exp (std::log (juce::Decibels::decibelsToGain ((double) db)) / samples);
        }

        // A balance: the centre leaves both sides at full level.
        inline void balance (float pan, float& left, float& right) noexcept
        {
            pan = juce::jlimit (-1.0f, 1.0f, pan);
            left = pan > 0.0f ? 1.0f - pan : 1.0f;
            right = pan < 0.0f ? 1.0f + pan : 1.0f;
        }
    }

    void Sampler::prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void Sampler::reset() noexcept
    {
        for (auto& v : voices)
            v = Voice {};
        for (auto& h : held)
            h = false;
        pedal = false;
        bend = 0.0f;
        activeVoices = 0;
    }

    float Sampler::nextRandom() noexcept
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return (float) (random >> 8) / 16777216.0f;   // [0, 1)
    }

    void Sampler::render (const Program* program, float* left, float* right, int numSamples, const juce::MidiBuffer& midi, int midiStart,
                          const Settings& settings) noexcept
    {
        if (program != current)
        {
            for (auto& v : voices)
                if (v.active)
                    fadeOut (v, 0.02);
            current = program;
        }
        updatePitch (settings);

        auto done = 0;
        const auto playTo = [&] (int to)
        {
            to = juce::jlimit (done, numSamples, to);
            while (done < to)
            {
                const auto n = juce::jmin (to - done, (int) scratchLeft.size());
                std::fill (scratchLeft.begin(), scratchLeft.begin() + n, 0.0f);
                std::fill (scratchRight.begin(), scratchRight.begin() + n, 0.0f);
                renderVoices (scratchLeft.data(), scratchRight.data(), n);
                // LEVEL and PAN, ramped from where the last piece ended (no zipper noise when they move).
                float targetLeft, targetRight;
                balance (settings.pan, targetLeft, targetRight);
                targetLeft *= settings.gain;
                targetRight *= settings.gain;
                if (shownGain < 0.0f)
                {
                    shownLeft = targetLeft;
                    shownRight = targetRight;
                    shownGain = settings.gain;
                }
                const auto stepLeft = (targetLeft - shownLeft) / (float) n, stepRight = (targetRight - shownRight) / (float) n;
                for (int i = 0; i < n; ++i)
                {
                    shownLeft += stepLeft;
                    shownRight += stepRight;
                    left[done + i] += scratchLeft[(size_t) i] * shownLeft;
                    right[done + i] += scratchRight[(size_t) i] * shownRight;
                }
                shownLeft = targetLeft;
                shownRight = targetRight;
                done += n;
            }
        };

        for (const auto metadata : midi)
        {
            const auto at = metadata.samplePosition - midiStart;
            if (at < 0)
                continue;
            if (at >= numSamples)
                break;
            const auto message = metadata.getMessage();
            const auto channel = message.getChannel();
            if (settings.channel > 0 && channel != settings.channel)
                continue;
            playTo (at);
            if (message.isNoteOn())
            {
                if (program != nullptr)
                    noteOn (*program, message.getNoteNumber(), message.getVelocity(), channel, settings);
            }
            else if (message.isNoteOff())
                noteOff (message.getNoteNumber(), channel);
            else if (message.isSustainPedalOn())
                pedal = true;
            else if (message.isSustainPedalOff())
            {
                pedal = false;
                for (auto& v : voices)
                    if (v.active && v.pedalHeld)
                        release (v);
            }
            else if (message.isPitchWheel())
            {
                bend = juce::jlimit (-1.0f, 1.0f, (float) (message.getPitchWheelValue() - 8192) / 8192.0f);
                updatePitch (settings);
            }
            else if (message.isAllNotesOff())
            {
                for (auto& v : voices)
                    if (v.active)
                        release (v);
            }
            else if (message.isAllSoundOff())
            {
                for (auto& v : voices)
                    if (v.active)
                        fadeOut (v, 0.005);
            }
        }
        playTo (numSamples);

        auto active = 0;
        std::array<bool, 128> keys {};
        for (const auto& v : voices)
            if (v.active)
            {
                ++active;
                if (! v.released && v.fadeStep == 0.0f)
                    keys[(size_t) v.note] = true;
            }
        activeVoices = active;
        for (size_t k = 0; k < keys.size(); ++k)
            held[k] = keys[k];
    }

    void Sampler::noteOn (const Program& program, int note, int velocity, int channel, const Settings& settings) noexcept
    {
        if (note < 0 || note > 127 || velocity <= 0)
            return;
        lastNote = note;
        const auto chance = nextRandom();
        for (const auto index : program.byKey[(size_t) note])
        {
            const auto& r = program.regions[(size_t) index];
            if (! r.covers (note, velocity, channel))
                continue;
            // Its round robin: it counts every note it could have played.
            auto turn = 0;
            if (index < (int) program.sequence.size())
                turn = program.sequence[(size_t) index]++ % r.seqLength;
            if (turn != r.seqPosition - 1)
                continue;
            if (chance < r.loRand || (chance >= r.hiRand && r.hiRand < 1.0f))
                continue;
            startVoice (r, note, velocity, channel, settings);
        }
    }

    void Sampler::startVoice (const Region& r, int note, int velocity, int channel, const Settings& settings) noexcept
    {
        if (r.sample == nullptr || r.sample->length() < 2)
            return;
        // POLYPHONY: the oldest voice sounding past it fades (a released one first).
        const auto limit = juce::jlimit (1, maxPolyphony, settings.polyphony);
        auto sounding = 0;
        for (const auto& v : voices)
            if (v.active && v.fadeStep == 0.0f)
                ++sounding;
        while (sounding >= limit)
        {
            Voice* oldest = nullptr;
            for (auto& v : voices)
                if (v.active && v.fadeStep == 0.0f
                    && (oldest == nullptr || (v.released && ! oldest->released) || (v.released == oldest->released && v.age < oldest->age)))
                    oldest = &v;
            if (oldest == nullptr)
                break;
            fadeOut (*oldest, 0.01);
            --sounding;
        }
        // A free slot (none: the oldest fading voice gives up its slot at once).
        Voice* slot = nullptr;
        for (auto& v : voices)
            if (! v.active)
            {
                slot = &v;
                break;
            }
        if (slot == nullptr)
            for (auto& v : voices)
                if (slot == nullptr || v.age < slot->age)
                    slot = &v;

        auto& v = *slot;
        v = Voice {};
        v.active = true;
        v.region = &r;
        v.sample = r.sample.get();
        v.note = note;
        v.channel = channel;
        v.age = ++counter;
        v.position = (double) r.offset;
        v.lastFrame = r.lastFrame();
        const auto cents = ((float) note - (float) r.keyCenter) * r.keytrack + r.tuneCents;
        v.baseIncrement = std::pow (2.0, (double) cents / 1200.0) * v.sample->sampleRate / rate;
        // Level: the region's, its velocity through amp_veltrack (SFZ's squared curve), its pan.
        const auto x = (float) velocity / 127.0f;
        const auto velocityGain = 1.0f - r.velTrack + r.velTrack * x * x;
        balance (r.pan, v.gainLeft, v.gainRight);
        v.gainLeft *= r.gain * velocityGain;
        v.gainRight *= r.gain * velocityGain;
        // The envelope.
        v.countdown = (int) std::lround (r.delay * rate);
        v.stage = v.countdown > 0 ? Voice::Stage::delay : Voice::Stage::attack;
        const auto attackSamples = r.attack * rate;
        v.attackStep = attackSamples >= 1.0 ? (float) (1.0 / attackSamples) : 1.0f;
        v.holdSamples = (int) std::lround (r.hold * rate);
        v.sustain = r.sustain;
        v.decayCoef = fallCoef (-60.0f, r.decay * rate);
        v.releaseCoef = fallCoef (-80.0f, juce::jmax (minReleaseSeconds, r.release * settings.releaseScale) * rate);
        updatePitch (settings);
    }

    void Sampler::updatePitch (const Settings& settings) noexcept
    {
        for (auto& v : voices)
            if (v.active && v.region != nullptr)
            {
                const auto bendCents = bend >= 0.0f ? bend * v.region->bendUp : -bend * v.region->bendDown;
                const auto cents = 100.0f * settings.transpose + settings.tuneCents + bendCents;
                v.increment = v.baseIncrement * (cents == 0.0f ? 1.0 : std::pow (2.0, (double) cents / 1200.0));
            }
    }

    void Sampler::noteOff (int note, int channel) noexcept
    {
        for (auto& v : voices)
            if (v.active && ! v.released && v.note == note && v.channel == channel)
            {
                if (v.region->oneShot)
                    continue;
                if (pedal)
                    v.pedalHeld = true;
                else
                    release (v);
            }
    }

    void Sampler::release (Voice& v) noexcept
    {
        if (v.released || v.region == nullptr || v.region->oneShot)
            return;
        v.released = true;
        v.pedalHeld = false;
        v.stage = Voice::Stage::release;
    }

    void Sampler::fadeOut (Voice& v, double seconds) noexcept
    {
        v.fadeStep = (float) (1.0 / juce::jmax (1.0, seconds * rate));
        v.released = true;
    }

    void Sampler::renderVoices (float* left, float* right, int numSamples) noexcept
    {
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            const auto& s = *v.sample;
            const auto* l = s.channels[0].data();
            const auto* r = s.channels[s.channels.size() > 1 ? 1 : 0].data();
            const auto length = (juce::int64) s.length();
            const auto stereo = s.channels.size() > 1;
            const auto at = [length] (const float* c, juce::int64 i) noexcept { return i >= 0 && i < length ? c[i] : 0.0f; };
            for (int i = 0; i < numSamples; ++i)
            {
                // The envelope.
                switch (v.stage)
                {
                    case Voice::Stage::delay:
                        if (--v.countdown <= 0)
                            v.stage = Voice::Stage::attack;
                        break;
                    case Voice::Stage::attack:
                        v.level += v.attackStep;
                        if (v.level >= 1.0f)
                        {
                            v.level = 1.0f;
                            v.countdown = v.holdSamples;
                            v.stage = v.countdown > 0 ? Voice::Stage::hold : Voice::Stage::decay;
                        }
                        break;
                    case Voice::Stage::hold:
                        if (--v.countdown <= 0)
                            v.stage = Voice::Stage::decay;
                        break;
                    case Voice::Stage::decay:
                        v.level = v.sustain + (v.level - v.sustain) * v.decayCoef;
                        if (v.level - v.sustain < 1.0e-4f)
                        {
                            v.level = v.sustain;
                            v.stage = Voice::Stage::sustain;
                        }
                        break;
                    case Voice::Stage::sustain:
                        break;
                    case Voice::Stage::release:
                        v.level *= v.releaseCoef;
                        break;
                }
                if (v.fadeStep > 0.0f)
                    v.fade -= v.fadeStep;
                const auto done = v.position > (double) v.lastFrame || v.fade <= 0.0f
                               || (v.stage == Voice::Stage::release && v.level < silent)
                               || (v.stage == Voice::Stage::sustain && v.sustain <= 0.0f);
                if (done)
                {
                    v.active = false;
                    break;
                }
                const auto index = (juce::int64) v.position;
                const auto t = (float) (v.position - (double) index);
                const auto gain = v.level * v.fade;
                const auto x = hermite (at (l, index - 1), at (l, index), at (l, index + 1), at (l, index + 2), t);
                const auto y = stereo ? hermite (at (r, index - 1), at (r, index), at (r, index + 1), at (r, index + 2), t) : x;
                left[i] += x * gain * v.gainLeft;
                right[i] += y * gain * v.gainRight;
                v.position += v.increment;
            }
        }
    }
}
