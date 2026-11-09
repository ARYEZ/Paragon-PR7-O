#include "dsp/sampler/Sampler.h"

#include <algorithm>
#include <cmath>

namespace sampler
{
    namespace
    {
        constexpr float minReleaseSeconds = 0.005f;
        constexpr float silent = 1.0e-4f;   // -80 dB: a releasing voice ends here
        constexpr int maxRegionsANote = 64;

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

        // SFZ's velocity curve: full track squared (velocity 64 about -12 dB), none flat.
        inline float velocityCurve (float velTrack, float velocity) noexcept
        {
            const auto x = velocity / 127.0f;
            return 1.0f - velTrack + velTrack * x * x;
        }
    }

    Sampler::Sampler()
    {
        voices.resize ((size_t) numSlots);
        noteScratch.resize ((size_t) numSlots);
        idScratch.resize ((size_t) numSlots);
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
        activeNotes = 0;
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
            keyswitch = -1;
            if (program != nullptr)
                keyswitch = program->swDefault >= 0 ? program->swDefault
                          : ! program->articulations.empty() ? program->articulations.front().lo : -1;
        }
        if (const auto chosen = pendingSwitch.exchange (-1); chosen >= 0)
            keyswitch = chosen;
        updatePitch (settings);
        updateWheel();

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
                // LEVEL, PAN and EXPRESSION, ramped from where the last piece ended (no zipper noise when they move).
                float targetLeft, targetRight;
                balance (settings.pan, targetLeft, targetRight);
                const auto gain = settings.gain * expression * expression;
                targetLeft *= gain;
                targetRight *= gain;
                if (shownGain < 0.0f)
                {
                    shownLeft = targetLeft;
                    shownRight = targetRight;
                    shownGain = gain;
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
                {
                    if (program->isKeyswitch (message.getNoteNumber()))
                        keyswitch = message.getNoteNumber();   // it chooses; it doesn't sound
                    else
                        noteOn (*program, message.getNoteNumber(), message.getVelocity(), channel, settings);
                }
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
            else if (message.isControllerOfType (1))
            {
                wheel = (float) message.getControllerValue() / 127.0f;
                updateWheel();
            }
            else if (message.isControllerOfType (11))
                expression = (float) message.getControllerValue() / 127.0f;
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
        auto& ids = idScratch;
        auto numIds = 0;
        for (const auto& v : voices)
            if (v.active)
            {
                ++active;
                if (! v.fading)
                    ids[(size_t) numIds++] = v.noteId;
                if (! v.released && ! v.fading)
                    keys[(size_t) v.note] = true;
            }
        std::sort (ids.begin(), ids.begin() + numIds);
        activeVoices = active;
        activeNotes = (int) (std::unique (ids.begin(), ids.begin() + numIds) - ids.begin());
        for (size_t k = 0; k < keys.size(); ++k)
            held[k] = keys[k];
        shownSwitch = keyswitch;
        shownWheel = wheel;
        shownExpression = expression;
    }

    void Sampler::noteOn (const Program& program, int note, int velocity, int channel, const Settings& settings) noexcept
    {
        if (note < 0 || note > 127 || velocity <= 0)
            return;
        lastNote = note;
        const auto chance = nextRandom();
        const auto onWheel = settings.dynamics == Dynamics::modWheel;
        // The regions this note plays: on the wheel, a held recording's every velocity layer.
        std::array<const Region*, maxRegionsANote> chosen {};
        auto numChosen = 0;
        for (const auto index : program.byKey[(size_t) note])
        {
            const auto& r = program.regions[(size_t) index];
            const auto anyVelocity = onWheel && r.held;
            if (! r.covers (note, anyVelocity ? r.loVel : velocity, channel) || ! r.playsUnder (keyswitch))
                continue;
            // Its round robin: it counts every note it could have played.
            auto turn = 0;
            if (index < (int) program.sequence.size())
                turn = program.sequence[(size_t) index]++ % r.seqLength;
            if (turn != r.seqPosition - 1)
                continue;
            if (chance < r.loRand || (chance >= r.hiRand && r.hiRand < 1.0f))
                continue;
            if (numChosen < maxRegionsANote)
                chosen[(size_t) numChosen++] = &r;
        }
        if (numChosen == 0)
            return;
        makeRoom (settings);
        const auto noteId = ++notes;
        // On the wheel: each held layer's neighbours, by the middles of the velocity ranges started.
        std::array<int, maxRegionsANote> middles {};   // twice each middle (lovel + hivel), whole numbers
        auto numMiddles = 0;
        if (onWheel)
            for (int i = 0; i < numChosen; ++i)
                if (chosen[(size_t) i]->held)
                {
                    const auto m = chosen[(size_t) i]->loVel + chosen[(size_t) i]->hiVel;
                    if (std::find (middles.begin(), middles.begin() + numMiddles, m) == middles.begin() + numMiddles)
                        middles[(size_t) numMiddles++] = m;
                }
        std::sort (middles.begin(), middles.begin() + numMiddles);
        for (int i = 0; i < numChosen; ++i)
        {
            const auto& r = *chosen[(size_t) i];
            auto lower = -1.0f, upper = -1.0f;
            if (onWheel && r.held)
            {
                const auto m = r.loVel + r.hiVel;
                for (int k = 0; k < numMiddles; ++k)
                {
                    if (middles[(size_t) k] < m)
                        lower = 0.5f * (float) middles[(size_t) k];
                    if (middles[(size_t) k] > m && upper < 0.0f)
                        upper = 0.5f * (float) middles[(size_t) k];
                }
            }
            startVoice (r, note, velocity, channel, settings, noteId, lower, upper);
        }
    }

    void Sampler::fadeNote (std::uint64_t noteId, double seconds) noexcept
    {
        for (auto& v : voices)
            if (v.active && v.noteId == noteId && ! v.fading)
                fadeOut (v, seconds);
    }

    void Sampler::makeRoom (const Settings& settings) noexcept
    {
        // POLYPHONY counts notes: while one more would pass it, a note fades. The oldest let go goes first; else the
        // oldest still held, sparing the chord's lowest and highest keys when three or more are held.
        const auto limit = juce::jlimit (1, maxPolyphony, settings.polyphony);
        for (int guard = 0; guard < maxPolyphony + 1; ++guard)
        {
            using Note = NoteInfo;
            auto& list = noteScratch;
            auto count = 0;
            auto lowest = 128, highest = -1;
            for (const auto& v : voices)
            {
                if (! v.active || v.fading)
                    continue;
                auto found = false;
                for (int k = 0; k < count && ! found; ++k)
                    if (list[(size_t) k].id == v.noteId)
                    {
                        found = true;
                        list[(size_t) k].released = list[(size_t) k].released && v.released;
                        list[(size_t) k].age = std::min (list[(size_t) k].age, v.age);
                    }
                if (! found)
                    list[(size_t) count++] = { v.noteId, v.age, v.note, v.released };
                if (! v.released)
                {
                    lowest = std::min (lowest, v.note);
                    highest = std::max (highest, v.note);
                }
            }
            if (count < limit)
                return;
            auto heldKeys = 0;
            std::array<bool, 128> seen {};
            for (int k = 0; k < count; ++k)
                if (! list[(size_t) k].released && ! seen[(size_t) list[(size_t) k].key])
                {
                    seen[(size_t) list[(size_t) k].key] = true;
                    ++heldKeys;
                }
            const Note* victim = nullptr;
            for (int k = 0; k < count; ++k)   // the oldest let go
                if (list[(size_t) k].released && (victim == nullptr || list[(size_t) k].age < victim->age))
                    victim = &list[(size_t) k];
            if (victim == nullptr)
                for (int k = 0; k < count; ++k)   // the oldest held, not the chord's edges
                {
                    const auto& n = list[(size_t) k];
                    const auto edge = heldKeys >= 3 && (n.key == lowest || n.key == highest);
                    if (! edge && (victim == nullptr || n.age < victim->age))
                        victim = &n;
                }
            if (victim == nullptr)
                for (int k = 0; k < count; ++k)   // (only edges left: the oldest)
                    if (victim == nullptr || list[(size_t) k].age < victim->age)
                        victim = &list[(size_t) k];
            if (victim == nullptr)
                return;
            fadeNote (victim->id, 0.01);
        }
    }

    void Sampler::startVoice (const Region& r, int note, int velocity, int channel, const Settings& settings, std::uint64_t noteId,
                              float lowerMiddle, float upperMiddle) noexcept
    {
        if (r.sample == nullptr || r.sample->length() < 2)
            return;
        // A free slot (none: the oldest fading voice gives up its slot at once, else the oldest of all).
        Voice* slot = nullptr;
        for (auto& v : voices)
            if (! v.active)
            {
                slot = &v;
                break;
            }
        if (slot == nullptr)
            for (auto& v : voices)
                if (slot == nullptr || (v.fading && ! slot->fading)
                    || (v.fading == slot->fading && v.age < slot->age))
                    slot = &v;

        auto& v = *slot;
        v = Voice {};
        v.active = true;
        v.region = &r;
        v.sample = r.sample.get();
        v.note = note;
        v.channel = channel;
        v.noteId = noteId;
        v.age = ++counter;
        v.position = (double) r.offset;
        v.lastFrame = r.lastFrame();
        const auto cents = ((float) note - (float) r.keyCenter) * r.keytrack + r.tuneCents;
        v.baseIncrement = std::pow (2.0, (double) cents / 1200.0) * v.sample->sampleRate / rate;
        // Level: the region's, its pan; its velocity through amp_veltrack (on the wheel: the wheel's, below).
        balance (r.pan, v.gainLeft, v.gainRight);
        v.onWheel = settings.dynamics == Dynamics::modWheel && r.held;
        v.velTrack = r.velTrack;
        const auto velocityGain = v.onWheel ? 1.0f : velocityCurve (r.velTrack, (float) velocity);
        v.gainLeft *= r.gain * velocityGain;
        v.gainRight *= r.gain * velocityGain;
        if (v.onWheel)
        {
            v.layerMiddle = 0.5f * (float) (r.loVel + r.hiVel);
            v.lowerMiddle = lowerMiddle;
            v.upperMiddle = upperMiddle;
            v.wheelGain = v.wheelTarget = wheelGainOf (v);
        }
        // The loop: the file's, or AUTO SUSTAIN's while it's on.
        if (r.loop != Region::Loop::none || (r.autoLoop && settings.autoSustain))
        {
            v.looping = true;
            v.loopStart = r.loopStart;
            v.loopEnd = r.loopEnd;
            v.crossfade = (int) juce::jlimit<juce::int64> (0, juce::jmin (r.loopStart, (r.loopEnd - r.loopStart) / 2), (juce::int64) r.loopCrossfade);
            v.likeness = juce::jlimit (-0.9f, 1.0f, r.loopLikeness);
        }
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

    float Sampler::wheelGainOf (const Voice& v) const noexcept
    {
        // The wheel as a velocity (1 to 127): this layer's share (equal power between neighbouring layers' middles, all
        // of it beyond the outermost), and the level that velocity gives.
        const auto at = 1.0f + wheel * 126.0f;
        auto share = 1.0f;
        if (at < v.layerMiddle && v.lowerMiddle >= 0.0f)
            share = std::sqrt (juce::jlimit (0.0f, 1.0f, (at - v.lowerMiddle) / (v.layerMiddle - v.lowerMiddle)));
        else if (at > v.layerMiddle && v.upperMiddle >= 0.0f)
            share = std::sqrt (juce::jlimit (0.0f, 1.0f, (v.upperMiddle - at) / (v.upperMiddle - v.layerMiddle)));
        return share * velocityCurve (v.velTrack, at);
    }

    void Sampler::updateWheel() noexcept
    {
        for (auto& v : voices)
            if (v.active && v.onWheel)
                v.wheelTarget = wheelGainOf (v);
    }

    void Sampler::updatePitch (const Settings& settings) noexcept
    {
        for (auto& v : voices)
            if (v.active && v.region != nullptr)
            {
                const auto bendCents = bend >= 0.0f ? bend * v.region->bendUp : -bend * v.region->bendDown;
                const auto cents = 100.0f * settings.transpose + settings.tuneCents + bendCents;
                v.increment = v.baseIncrement * std::pow (2.0, (double) cents / 1200.0);
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
        // A sustain loop (AUTO SUSTAIN's too) stops going round; one let go inside its crossfade finishes it first, so
        // the sound doesn't jump. loop_continuous goes on.
        if (v.looping && v.region->loop != Region::Loop::continuous)
        {
            if (v.crossfade > 0 && v.position >= (double) (v.loopEnd - v.crossfade))
                v.stopAtWrap = true;
            else
                v.looping = false;
        }
    }

    void Sampler::fadeOut (Voice& v, double seconds) noexcept
    {
        v.fadeStep = (float) (1.0 / juce::jmax (1.0, seconds * rate));
        v.fading = true;
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
            const auto read = [&] (const float* c, double position) noexcept
            {
                const auto index = (juce::int64) position;
                const auto t = (float) (position - (double) index);
                return hermite (at (c, index - 1), at (c, index), at (c, index + 1), at (c, index + 2), t);
            };
            const auto wheelStep = (v.wheelTarget - v.wheelGain) / (float) numSamples;
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
                if (v.fading)
                    v.fade -= v.fadeStep;
                v.wheelGain += wheelStep;
                const auto done = (! v.looping && v.position > (double) v.lastFrame) || v.fade <= 0.0f
                               || (v.stage == Voice::Stage::release && v.level < silent)
                               || (v.stage == Voice::Stage::sustain && v.sustain <= 0.0f);
                if (done)
                {
                    v.active = false;
                    break;
                }
                auto x = read (l, v.position);
                auto y = stereo ? read (r, v.position) : x;
                // Going round a loop: its last `crossfade` frames fade into the frames before its start.
                if (v.looping && v.crossfade > 0 && v.position >= (double) (v.loopEnd - v.crossfade))
                {
                    const auto t = juce::jlimit (0.0f, 1.0f, (float) ((v.position - (double) (v.loopEnd - v.crossfade)) / (double) v.crossfade));
                    const auto w1 = 1.0f - t, w2 = t;
                    const auto norm = 1.0f / std::sqrt (juce::jmax (1.0e-6f, w1 * w1 + w2 * w2 + 2.0f * v.likeness * w1 * w2));
                    const auto from = w1 * norm, to = w2 * norm;
                    const auto back = v.position - (double) (v.loopEnd - v.loopStart);
                    x = x * from + read (l, back) * to;
                    y = stereo ? y * from + read (r, back) * to : x;
                }
                const auto gain = v.level * v.fade * (v.onWheel ? v.wheelGain : 1.0f);
                left[i] += x * gain * v.gainLeft;
                right[i] += y * gain * v.gainRight;
                v.position += v.increment;
                if (v.looping && v.position >= (double) v.loopEnd)
                {
                    v.position -= (double) (v.loopEnd - v.loopStart);
                    if (v.stopAtWrap)
                        v.looping = v.stopAtWrap = false;
                }
            }
            if (v.active)
                v.wheelGain = v.wheelTarget;
        }
    }
}
