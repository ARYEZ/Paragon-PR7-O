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
        cameFrom.fill (-1);
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
        keyDown.fill (false);
        cameFrom.fill (-1);
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
                          const Settings& settings, const PartOutputs* parts) noexcept
    {
        const auto numParts = parts != nullptr ? juce::jlimit (1, maxParts, parts->count) : 1;
        if (program != current)
        {
            for (auto& v : voices)
                if (v.active)
                    fadeOut (v, 0.02);
            current = program;
            cameFrom.fill (-1);
            releaseDue.fill (false);
            // 0.9.0: the controllers start where the file sets them (set_ccN); the mod wheel and EXPRESSION follow.
            if (program != nullptr)
                for (int c = 0; c < 128; ++c)
                    if (program->initialCC[(size_t) c] >= 0)
                    {
                        cc[(size_t) c] = (float) program->initialCC[(size_t) c];
                        if (c == 1) wheel = cc[1] / 127.0f;
                        if (c == 11) expression = cc[11] / 127.0f;
                    }
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
                const auto n = juce::jmin (to - done, scratchSize);
                for (int p = 0; p < numParts; ++p)
                {
                    std::fill (scratchLeft[(size_t) p].begin(), scratchLeft[(size_t) p].begin() + n, 0.0f);
                    std::fill (scratchRight[(size_t) p].begin(), scratchRight[(size_t) p].begin() + n, 0.0f);
                }
                renderVoices (n, numParts);
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
                    rampLeft[(size_t) i] = shownLeft;
                    rampRight[(size_t) i] = shownRight;
                }
                for (int p = 0; p < numParts; ++p)
                {
                    auto* intoLeft = (parts != nullptr ? parts->left[(size_t) p] : left) + done;
                    auto* intoRight = (parts != nullptr ? parts->right[(size_t) p] : right) + done;
                    const auto& fromLeft = scratchLeft[(size_t) p];
                    const auto& fromRight = scratchRight[(size_t) p];
                    for (int i = 0; i < n; ++i)
                    {
                        intoLeft[i] += fromLeft[(size_t) i] * rampLeft[(size_t) i];
                        intoRight[i] += fromRight[(size_t) i] * rampRight[(size_t) i];
                    }
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
            now = clock + at;
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
                noteOff (program, message.getNoteNumber(), channel, settings);
            else if (message.isPitchWheel())
            {
                bend = juce::jlimit (-1.0f, 1.0f, (float) (message.getPitchWheelValue() - 8192) / 8192.0f);
                updatePitch (settings);
            }
            else if (message.isController() && ! message.isAllNotesOff() && ! message.isAllSoundOff())
            {
                const auto number = message.getControllerNumber(), value = message.getControllerValue();
                cc[(size_t) number] = (float) value;
                // The sustain pedal: CC 64, or the file's sustain_cc (0.9.0).
                if (number == (program != nullptr ? program->sustainCC : 64))
                {
                    const auto down = value >= 64;
                    if (down != pedal)
                    {
                        pedal = down;
                        if (! pedal)
                        {
                            for (auto& v : voices)
                                if (v.active && v.pedalHeld)
                                    release (v);
                            // 0.9.0: the release samples of keys let go under the pedal, now.
                            if (program != nullptr)
                                for (int k = 0; k < 128; ++k)
                                    if (std::exchange (releaseDue[(size_t) k], false))
                                        playReleases (*program, k, keyChannel[(size_t) k], settings, true);
                        }
                    }
                }
                if (number == 1)
                {
                    wheel = (float) value / 127.0f;
                    updateWheel();
                }
                else if (number == 11)
                    expression = (float) value / 127.0f;
                controllersChanged();
            }
            else if (message.isAllNotesOff())
            {
                keyDown.fill (false);
                cameFrom.fill (-1);
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
        clock += numSamples;

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
        const auto from = settings.legato ? joinableFrom (note) : -1;
        auto othersDown = false;   // 0.9.0: for first / legato samples
        for (int k = 0; k < 128 && ! othersDown; ++k)
            othersDown = k != note && keyDown[(size_t) k];
        phraseGoingOn = othersDown;
        keyDown[(size_t) note] = true;
        keyVelocity[(size_t) note] = velocity;
        keyChannel[(size_t) note] = channel;
        keyStart[(size_t) note] = now;
        lastNoteId = 0;
        const auto joined = playNote (program, note, velocity, channel, settings, from, NoteExtras {});
        cameFrom[(size_t) note] = joined ? from : -1;
        // 0.10.0, DOUBLE: the octaves too, as the same note (ended by this key), joined along with it.
        const auto mainId = lastNoteId;
        for (const auto offset : { 12, -12 })
        {
            const auto wanted = (offset > 0 && (settings.doubleMode == 1 || settings.doubleMode == 3))
                             || (offset < 0 && (settings.doubleMode == 2 || settings.doubleMode == 3));
            const auto key = note + offset;
            if (! wanted || key < 0 || key > 127 || ! program.plays (key) || program.isKeyswitch (key))
                continue;
            NoteExtras extras;
            extras.owner = note;
            extras.gain = settings.doubleGain;
            extras.isDouble = true;
            extras.noteId = mainId;
            extras.joinTimeKey = from;
            playNote (program, key, velocity, channel, settings, joined && from + offset >= 0 && from + offset < 128 ? from + offset : -1, extras);
        }
    }

    int Sampler::joinableFrom (int note) const noexcept
    {
        // The nearest sounding held note within an octave, begun at least chordWindow ago (the most recent when two are
        // as near).
        const auto earliest = now - (juce::int64) (chordWindow * rate);
        auto best = -1, bestDistance = 13;
        juce::int64 bestStart = 0;
        for (const auto& v : voices)
        {
            if (! v.active || v.released || v.fading || v.region == nullptr || ! v.region->held || v.note == note || v.isDouble)
                continue;
            const auto start = keyStart[(size_t) v.note];
            const auto distance = std::abs (v.note - note);
            if (start > earliest || distance > 12)
                continue;
            if (distance < bestDistance || (distance == bestDistance && start > bestStart))
            {
                best = v.note;
                bestDistance = distance;
                bestStart = start;
            }
        }
        return best;
    }

    bool Sampler::playNote (const Program& program, int note, int velocity, int channel, const Settings& settings, int joinFrom,
                            const NoteExtras& extras) noexcept
    {
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
            // 0.9.0: release samples wait for the key to come up; first only as a phrase starts, legato only within one;
            // only while its controllers are in range.
            if (r.trigger == Region::Trigger::release || r.trigger == Region::Trigger::releaseKey
                || (r.trigger == Region::Trigger::first && phraseGoingOn) || (r.trigger == Region::Trigger::legato && ! phraseGoingOn)
                || ! conditionsMet (r))
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
            return false;
        // LEGATO: joined only when it plays a held recording.
        Join join;
        if (joinFrom >= 0)
            for (int i = 0; i < numChosen && join.from < 0; ++i)
                if (chosen[(size_t) i]->held && chosen[(size_t) i]->legatoStart >= 0)
                    join.from = joinFrom;
        if (join.from >= 0)
        {
            const auto timeKey = extras.joinTimeKey >= 0 ? extras.joinTimeKey : join.from;   // (a doubled octave: the played key's)
            join.seconds = joinSeconds ((double) (now - keyStart[(size_t) timeKey]) / rate);
            join.glide = juce::jmax (0.0, (double) settings.glideSeconds);
            // The note joined fades out (equal power) as this one fades in, its pitch gliding with it.
            const auto count = (int) std::lround (join.glide * rate);
            const auto cents = 100.0 * (double) (note - join.from);
            for (auto& v : voices)
                if (v.active && v.note == join.from && ! v.released && ! v.fading)
                {
                    fadeOut (v, join.seconds);
                    v.legatoOut = true;
                    if (count > 0)
                    {
                        v.glideTarget = std::pow (2.0, cents / 1200.0);
                        v.glideStep = std::pow (2.0, cents / 1200.0 / (double) count);
                        v.glideCount = count;
                    }
                }
        }
        // (a doubled octave is the same note as the key played: no room made for it, its number shared)
        if (extras.noteId == 0)
            makeRoom (settings);
        const auto noteId = extras.noteId != 0 ? extras.noteId : ++notes;
        lastNoteId = noteId;
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
            // 0.10.0, PLAYERS: the first as ever; the others a little out of tune, later, further in and elsewhere across.
            static constexpr std::array<float, 4> detunes { 0.0f, 0.7f, -0.9f, 0.35f };
            static constexpr std::array<std::array<float, 4>, 4> pansFor { { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
                                                                             { 0.0f, -0.6f, 0.6f, 0.0f }, { 0.0f, -0.6f, 0.6f, 0.0f } } };
            const auto players = juce::jlimit (1, 4, settings.players);
            const auto& pans = pansFor[(size_t) (players - 1)];
            auto sumLeft = 0.0f, sumRight = 0.0f;   // each side's players' gains, squared (their sounds unalike: powers add)
            for (int c = 0; c < players; ++c)
            {
                float pl, pr;
                balance (pans[(size_t) c], pl, pr);
                sumLeft += pl * pl;
                sumRight += pr * pr;
            }
            for (int c = 0; c < players; ++c)
            {
                Copy copy;
                copy.owner = extras.owner >= 0 ? extras.owner : note;
                copy.isDouble = extras.isDouble;
                copy.gainLeft = extras.gain / std::sqrt (sumLeft);
                copy.gainRight = extras.gain / std::sqrt (sumRight);
                if (c > 0)
                {
                    copy.cents = settings.detuneCents * (detunes[(size_t) c] + 0.5f * (nextRandom() - 0.5f));
                    copy.delay = (int) std::lround ((0.25 + 0.75 * nextRandom()) * settings.timingMs * 0.001 * rate);
                    copy.offsetSeconds = 0.008 * nextRandom();
                    copy.pan = pans[(size_t) c];
                }
                startVoice (r, note, velocity, channel, settings, noteId, lower, upper, join.from >= 0 ? &join : nullptr, 1.0f, &copy);
            }
        }
        return join.from >= 0;
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
                              float lowerMiddle, float upperMiddle, const Join* join, float extraGain, const Copy* copy) noexcept
    {
        if (r.sample == nullptr || r.sample->length() < 2)
            return;
        // 0.9.0, GROUPS: a note in a group stops the voices that group turns off (off_by), as they say (off_mode).
        if (r.group != 0)
            for (auto& w : voices)
                if (w.active && ! w.fading && w.noteId != noteId && w.region != nullptr && w.region->offBy == r.group)
                {
                    if (w.region->offMode == Region::OffMode::normal)
                        release (w);
                    else
                        fadeOut (w, w.region->offMode == Region::OffMode::time ? (double) w.region->offTime : 0.006);
                }
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
        v.velocity = velocity;
        if (r.offsetRandom > 0)   // 0.9.0: offset_random
            v.position = juce::jmin ((double) (v.lastFrame - 2), v.position + (double) nextRandom() * (double) r.offsetRandom);
        const auto cents = ((float) note - (float) r.keyCenter) * r.keytrack + r.tuneCents + r.pitchVeltrack * (float) velocity / 127.0f;
        v.baseIncrement = std::pow (2.0, (double) cents / 1200.0) * v.sample->sampleRate / rate;
        // Level: the region's, its pan; its velocity through amp_veltrack (on the wheel: the wheel's, below).
        balance (r.pan, v.gainLeft, v.gainRight);
        // (0.9.0: a file that follows the mod wheel itself, its crossfades or gain_cc1, keeps it: MOD WHEEL leaves it be)
        v.onWheel = settings.dynamics == Dynamics::modWheel && r.held && ! r.usesController (1);
        v.velTrack = r.velTrack;
        auto velocityGain = v.onWheel ? 1.0f : velocityCurve (r.velTrack, (float) velocity);
        if (! v.onWheel && ! r.velCurve.empty())   // 0.9.0: amp_velcurve_N, straight lines between its points
            for (size_t i = 1; i < r.velCurve.size(); ++i)
                if (velocity <= r.velCurve[i].first)
                {
                    const auto& [v0, g0] = r.velCurve[i - 1];
                    const auto& [v1, g1] = r.velCurve[i];
                    velocityGain = v1 > v0 ? g0 + (g1 - g0) * (float) (velocity - v0) / (float) (v1 - v0) : g1;
                    break;
                }
        // 0.9.0: crossfades on velocity and keys (equal power or straight), level by key, and what the caller adds (a
        // release sample's rt_decay).
        const auto fadeOf = [] (int value, int lo, int hi, bool in, bool power)
        {
            if (lo < 0 && hi < 0)
                return 1.0f;
            auto t = hi > lo ? juce::jlimit (0.0f, 1.0f, (float) (value - lo) / (float) (hi - lo)) : (value >= hi ? 1.0f : 0.0f);
            if (! in)
                t = 1.0f - t;
            return power ? std::sqrt (t) : t;
        };
        if (r.xfinLoVel >= 0 || r.xfinHiVel >= 0)
            velocityGain *= fadeOf (velocity, juce::jmax (0, r.xfinLoVel), r.xfinHiVel < 0 ? 0 : r.xfinHiVel, true, r.velFadePower);
        if (r.xfoutLoVel >= 0 || r.xfoutHiVel >= 0)
            velocityGain *= fadeOf (velocity, r.xfoutLoVel < 0 ? 127 : r.xfoutLoVel, r.xfoutHiVel < 0 ? 127 : r.xfoutHiVel, false, r.velFadePower);
        if (r.xfinLoKey >= 0 || r.xfinHiKey >= 0)
            velocityGain *= fadeOf (note, juce::jmax (0, r.xfinLoKey), r.xfinHiKey < 0 ? 0 : r.xfinHiKey, true, r.keyFadePower);
        if (r.xfoutLoKey >= 0 || r.xfoutHiKey >= 0)
            velocityGain *= fadeOf (note, r.xfoutLoKey < 0 ? 127 : r.xfoutLoKey, r.xfoutHiKey < 0 ? 127 : r.xfoutHiKey, false, r.keyFadePower);
        velocityGain *= juce::Decibels::decibelsToGain (r.ampKeytrack * (float) (note - r.ampKeycenter), -144.0f) * extraGain;
        v.gainLeft *= r.gain * velocityGain;
        v.gainRight *= r.gain * velocityGain;
        v.controlGain = v.controlTarget = controlGainOf (v);
        // 0.9.0: a start held back (delay, delay_random), its filters, its level LFO.
        v.waiting = (int) std::lround ((r.startDelay + nextRandom() * r.startDelayRandom) * rate);
        for (size_t f = 0; f < 2; ++f)
        {
            const auto& filter = r.filters[f];
            auto& state = v.filters[f];
            state = {};
            if (filter.type == Region::Filter::Type::none)
                continue;
            state.on = true;
            state.type = filter.type;
            state.onePole = filter.type == Region::Filter::Type::lowpass1 || filter.type == Region::Filter::Type::highpass1;
            state.baseHz = filter.cutoff * (float) std::pow (2.0, (filter.keytrack * (float) (note - filter.keycenter) + filter.veltrack * (float) velocity / 127.0f) / 1200.0);
            state.k = 1.0f / (0.70710678f * juce::Decibels::decibelsToGain (filter.resonance));
        }
        updateFilters (v);
        v.lfoPhase = 0.0;
        v.lfoStep = juce::MathConstants<double>::twoPi * (double) r.lfoFreq / rate;
        v.lfoAge = 0;
        v.lfoGain = 1.0f;
        if (v.onWheel)
        {
            v.layerMiddle = 0.5f * (float) (r.loVel + r.hiVel);
            v.lowerMiddle = lowerMiddle;
            v.upperMiddle = upperMiddle;
            v.wheelGain = v.wheelTarget = wheelGainOf (v);
        }
        // The loop: the file's, or AUTO SUSTAIN's while it's on (0.9.0: never a release sample's: its key is already up,
        // nothing would end it).
        const auto isRelease = r.trigger == Region::Trigger::release || r.trigger == Region::Trigger::releaseKey;
        if (! isRelease && (r.loop != Region::Loop::none || (r.autoLoop && settings.autoSustain)))
        {
            v.looping = true;
            v.loopStart = r.loopStart;
            v.loopEnd = r.loopEnd;
            v.crossfade = (int) juce::jlimit<juce::int64> (0, juce::jmin (r.loopStart, (r.loopEnd - r.loopStart) / 2), (juce::int64) r.loopCrossfade);
            v.likeness = juce::jlimit (-0.9f, 1.0f, r.loopLikeness);
        }
        v.tilted = v.looping && r.autoLoop && std::abs (r.loopTilt) > 0.01f && v.loopEnd > v.loopStart;
        v.tiltK = 0.0;
        v.tiltGain = v.tiltStep = v.tiltEnd = 1.0f;
        if (v.tilted)
        {
            v.tiltEnd = juce::Decibels::decibelsToGain (r.loopTilt);
            v.tiltK = std::log ((double) v.tiltEnd) / (double) (v.loopEnd - v.loopStart);
        }
        // The envelope (0.9.0: each stage moved by velocity and by controllers as the note starts).
        std::array<float, 6> stage { r.delay, r.attack, r.hold, r.decay, r.sustain, r.release };
        for (size_t i = 0; i < 6; ++i)
        {
            stage[i] += r.velToEnvelope[i] * (float) velocity / 127.0f;
            for (const auto& [number, amount] : r.ccToEnvelope[i])
                stage[i] += amount * cc[(size_t) juce::jlimit (0, 127, number)] / 127.0f;
            stage[i] = i == 4 ? juce::jlimit (0.0f, 1.0f, stage[i]) : juce::jmax (0.0f, stage[i]);
        }
        v.countdown = (int) std::lround (stage[0] * rate);
        v.stage = v.countdown > 0 ? Voice::Stage::delay : Voice::Stage::attack;
        const auto attackSamples = stage[1] * rate;
        v.attackStep = attackSamples >= 1.0 ? (float) (1.0 / attackSamples) : 1.0f;
        v.holdSamples = (int) std::lround (stage[2] * rate);
        v.sustain = stage[4];
        v.decayCoef = fallCoef (-60.0f, stage[3] * rate);
        v.releaseCoef = fallCoef (-80.0f, juce::jmax (minReleaseSeconds, stage[5] * settings.releaseScale) * rate);
        // LEGATO: a joined held recording starts past its attack, fading in over the join (equal power), its pitch
        // gliding from the note it joins.
        if (join != nullptr && r.held && r.legatoStart >= 0)
        {
            v.position = (double) r.legatoStart;
            v.countdown = 0;
            v.stage = Voice::Stage::attack;
            v.attackStep = (float) (1.0 / juce::jmax (1.0, join->seconds * rate));
            v.legatoIn = true;
            if (const auto count = (int) std::lround (join->glide * rate); count > 0 && join->from != note)
            {
                const auto cents = 100.0 * (double) (join->from - note);
                v.glideRatio = std::pow (2.0, cents / 1200.0);
                v.glideStep = std::pow (2.0, -cents / 1200.0 / (double) count);
                v.glideTarget = 1.0;
                v.glideCount = count;
            }
        }
        // 0.10.0: this player's own pitch, place across, level, lateness and start; the key that ends it.
        v.owner = copy != nullptr && copy->owner >= 0 ? copy->owner : note;
        v.isDouble = copy != nullptr && copy->isDouble;
        if (copy != nullptr)
        {
            v.baseIncrement *= std::pow (2.0, (double) copy->cents / 1200.0);
            float panLeft, panRight;
            balance (copy->pan, panLeft, panRight);
            v.gainLeft *= panLeft * copy->gainLeft;
            v.gainRight *= panRight * copy->gainRight;
            v.waiting += copy->delay;
            if (copy->offsetSeconds > 0.0)
                v.position = juce::jmin ((double) (v.lastFrame - 2), v.position + copy->offsetSeconds * v.sample->sampleRate);
        }
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

    void Sampler::noteOff (const Program* program, int note, int channel, const Settings& settings) noexcept
    {
        if (note < 0 || note > 127)
            return;
        keyDown[(size_t) note] = false;
        // LEGATO: let go of a joined note while a key it came from is still down: it joins back to that one.
        auto back = cameFrom[(size_t) note];
        cameFrom[(size_t) note] = -1;
        for (int guard = 0; guard < 128 && back >= 0 && ! keyDown[(size_t) back]; ++guard)
            back = cameFrom[(size_t) back];
        if (settings.legato && ! pedal && program != nullptr && back >= 0 && keyDown[(size_t) back])
        {
            auto sounding = false;
            for (const auto& v : voices)
                sounding = sounding || (v.active && v.note == note && ! v.released && ! v.fading && v.region != nullptr && v.region->held);
            if (sounding)   // (the join's length follows how long this note played)
                playNote (*program, back, keyVelocity[(size_t) back], keyChannel[(size_t) back], settings, note, NoteExtras {});
        }
        for (auto& v : voices)
            if (v.active && ! v.released && v.owner == note && v.channel == channel)   // (0.10.0: its doubled octaves too)
            {
                if (v.region->oneShot)
                    continue;
                if (pedal)
                    v.pedalHeld = true;
                else
                    release (v);
            }
        // 0.9.0: its release samples (release_key at once; release after the pedal), started after the note's own
        // voices are let go (else they'd be let go with them).
        if (program != nullptr)
        {
            releaseDue[(size_t) note] = pedal;
            playReleases (*program, note, channel, settings);
        }
    }

    void Sampler::release (Voice& v) noexcept
    {
        if (v.released || v.region == nullptr || v.region->oneShot)
            return;
        if (v.legatoIn && v.stage == Voice::Stage::attack)   // (from the equal-power rise's level: no step)
            v.level = std::sin (juce::MathConstants<float>::halfPi * v.level);
        v.legatoIn = false;
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

    bool Sampler::conditionsMet (const Region& r) const noexcept
    {
        for (const auto& c : r.ccConditions)
            if (cc[(size_t) juce::jlimit (0, 127, c.cc)] < (float) c.lo || cc[(size_t) juce::jlimit (0, 127, c.cc)] > (float) c.hi)
                return false;
        return true;
    }

    void Sampler::playReleases (const Program& program, int note, int channel, const Settings& settings, bool pedalLifted) noexcept
    {
        if (note < 0 || note > 127)
            return;
        const auto velocity = juce::jmax (1, keyVelocity[(size_t) note]);
        const auto heldSeconds = (float) ((double) juce::jmax<juce::int64> (0, now - keyStart[(size_t) note]) / rate);
        auto started = false;
        std::uint64_t noteId = 0;
        for (const auto index : program.byKey[(size_t) note])
        {
            const auto& r = program.regions[(size_t) index];
            const auto wanted = pedalLifted ? r.trigger == Region::Trigger::release
                                            : r.trigger == Region::Trigger::releaseKey || (r.trigger == Region::Trigger::release && ! pedal);
            if (! wanted || ! r.covers (note, velocity, channel) || ! r.playsUnder (keyswitch) || ! conditionsMet (r))
                continue;
            auto turn = 0;
            if (index < (int) program.sequence.size())
                turn = program.sequence[(size_t) index]++ % r.seqLength;
            if (turn != r.seqPosition - 1)
                continue;
            const auto chance = nextRandom();
            if (chance < r.loRand || (chance >= r.hiRand && r.hiRand < 1.0f))
                continue;
            if (! started)
            {
                makeRoom (settings);
                noteId = ++notes;
                started = true;
            }
            // Quieter the longer the key was held (rt_decay dB a second).
            startVoice (r, note, velocity, channel, settings, noteId, -1.0f, -1.0f, nullptr,
                        juce::Decibels::decibelsToGain (-r.rtDecay * heldSeconds, -144.0f));
        }
    }

    float Sampler::controlGainOf (const Voice& v) const noexcept
    {
        // The file's controllers: gain_ccN (dB), amplitude_onccN (%), and its crossfades on controllers.
        if (v.region == nullptr)
            return 1.0f;
        const auto& r = *v.region;
        auto db = 0.0f, amplitude = 1.0f, fade = 1.0f;
        for (const auto& [number, amount] : r.gainCC)
            db += amount * cc[(size_t) juce::jlimit (0, 127, number)] / 127.0f;
        if (! r.amplitudeCC.empty())
        {
            amplitude = 0.0f;
            for (const auto& [number, amount] : r.amplitudeCC)
                amplitude += amount / 100.0f * cc[(size_t) juce::jlimit (0, 127, number)] / 127.0f;
            amplitude = juce::jlimit (0.0f, 1.0f, amplitude);
        }
        for (const auto& f : r.ccFades)
        {
            const auto value = cc[(size_t) juce::jlimit (0, 127, f.cc)];
            auto t = f.hi > f.lo ? juce::jlimit (0.0f, 1.0f, (value - (float) f.lo) / (float) (f.hi - f.lo)) : (value >= (float) f.hi ? 1.0f : 0.0f);
            if (! f.in)
                t = 1.0f - t;
            fade *= r.ccFadePower ? std::sqrt (t) : t;
        }
        return juce::Decibels::decibelsToGain (db, -144.0f) * amplitude * fade;
    }

    void Sampler::updateFilters (Voice& v) noexcept
    {
        // Each filter's cutoff: its own, moved by the controllers now (key and velocity were taken at the note's start).
        for (size_t f = 0; f < 2; ++f)
        {
            auto& state = v.filters[f];
            if (! state.on || v.region == nullptr)
                continue;
            auto cents = 0.0f;
            for (const auto& [number, amount] : v.region->filters[f].cc)
                cents += amount * cc[(size_t) juce::jlimit (0, 127, number)] / 127.0f;
            const auto hz = juce::jlimit (10.0, 0.45 * rate, (double) state.baseHz * std::pow (2.0, cents / 1200.0));
            state.g = (float) std::tan (juce::MathConstants<double>::pi * hz / rate);
            state.G = state.g / (1.0f + state.g);
            state.a1 = 1.0f / (1.0f + state.g * (state.g + state.k));
            state.a2 = state.g * state.a1;
            state.a3 = state.g * state.a2;
        }
    }

    void Sampler::controllersChanged() noexcept
    {
        for (auto& v : voices)
            if (v.active)
            {
                v.controlTarget = controlGainOf (v);
                updateFilters (v);
            }
    }

    void Sampler::renderVoices (int numSamples, int numParts) noexcept
    {
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            const auto part = v.region != nullptr ? juce::jlimit (0, numParts - 1, v.region->part) : 0;
            auto* left = scratchLeft[(size_t) part].data();
            auto* right = scratchRight[(size_t) part].data();
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
            const auto tilted = v.tilted;
            const auto tiltAt = [&v] (double position) noexcept
            {
                if (position <= (double) v.loopStart)
                    return 1.0f;
                if (position >= (double) v.loopEnd)
                    return v.tiltEnd;
                return (float) std::exp (v.tiltK * (position - (double) v.loopStart));
            };
            if (tilted)
            {
                v.tiltGain = tiltAt (v.position);
                v.tiltStep = (float) std::exp (v.tiltK * v.increment * v.glideRatio);
            }
            // 0.9.0: the file's controllers' level (ramped across the piece) and its level LFO (from where it was to where
            // it'll be at the piece's end: depth dB x sin, faded in after its delay).
            const auto controlStep = (v.controlTarget - v.controlGain) / (float) numSamples;
            const auto& region = *v.region;
            auto lfoStep = 0.0f;
            if (region.lfoDepth != 0.0f && region.lfoFreq > 0.0f)
            {
                const auto ageEnd = v.lfoAge + numSamples;
                const auto delaySamples = region.lfoDelay * (float) rate, fadeSamples = juce::jmax (1.0f, region.lfoFade * (float) rate);
                const auto amount = juce::jlimit (0.0f, 1.0f, ((float) ageEnd - delaySamples) / fadeSamples);
                const auto phaseEnd = v.lfoPhase + v.lfoStep * (double) numSamples;
                const auto target = juce::Decibels::decibelsToGain (region.lfoDepth * amount * (float) std::sin (phaseEnd));
                lfoStep = (target - v.lfoGain) / (float) numSamples;
                v.lfoPhase = std::fmod (phaseEnd, juce::MathConstants<double>::twoPi);
                v.lfoAge = ageEnd;
            }
            const auto width = region.width;
            const auto filtering = v.filters[0].on || v.filters[1].on;
            for (int i = 0; i < numSamples; ++i)
            {
                v.controlGain += controlStep;
                v.lfoGain += lfoStep;
                if (v.waiting > 0)   // 0.9.0: its start held back (delay): nothing yet, the recording not moving
                {
                    --v.waiting;
                    continue;
                }
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
                auto x = read (l, v.position) * v.tiltGain;
                auto y = stereo ? read (r, v.position) * v.tiltGain : x;
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
                // 0.9.0: its stereo width, then its filters.
                if (stereo && width != 1.0f)
                {
                    const auto middle = 0.5f * (x + y), side = 0.5f * (x - y) * width;
                    x = middle + side;
                    y = middle - side;
                }
                if (filtering)
                    for (auto& f : v.filters)
                    {
                        if (! f.on)
                            continue;
                        const auto run = [&f] (float in, int c) noexcept
                        {
                            using T = Region::Filter::Type;
                            auto& s1 = f.ic1[(size_t) c];
                            if (f.onePole)
                            {
                                const auto vv = (in - s1) * f.G;
                                const auto low = vv + s1;
                                s1 = low + vv;
                                return f.type == T::lowpass1 ? low : in - low;
                            }
                            auto& s2 = f.ic2[(size_t) c];
                            const auto v3 = in - s2;
                            const auto v1 = f.a1 * s1 + f.a2 * v3;
                            const auto v2 = s2 + f.a2 * s1 + f.a3 * v3;
                            s1 = 2.0f * v1 - s1;
                            s2 = 2.0f * v2 - s2;
                            switch (f.type)
                            {
                                case T::lowpass2:  return v2;
                                case T::highpass2: return in - f.k * v1 - v2;
                                case T::bandpass2: return v1;
                                case T::notch2:    return in - f.k * v1;
                                case T::none: case T::lowpass1: case T::highpass1: break;
                            }
                            return in;
                        };
                        x = run (x, 0);
                        y = stereo ? run (y, 1) : x;
                    }
                auto level = v.level, fade = v.fade;
                if (v.legatoIn && v.stage == Voice::Stage::attack)   // LEGATO's equal-power crossfade
                    level = std::sin (juce::MathConstants<float>::halfPi * level);
                if (v.legatoOut)
                    fade = std::sin (juce::MathConstants<float>::halfPi * juce::jmax (0.0f, fade));
                const auto gain = level * fade * (v.onWheel ? v.wheelGain : 1.0f) * v.controlGain * v.lfoGain;
                left[i] += x * gain * v.gainLeft;
                right[i] += y * gain * v.gainRight;
                v.position += v.increment * v.glideRatio;
                if (v.glideCount > 0 && --v.glideCount == 0)
                    v.glideRatio = v.glideTarget;
                else if (v.glideCount > 0)
                    v.glideRatio *= v.glideStep;
                if (v.looping && v.position >= (double) v.loopEnd)
                {
                    v.position -= (double) (v.loopEnd - v.loopStart);
                    if (v.stopAtWrap)
                        v.looping = v.stopAtWrap = false;
                    if (tilted)
                        v.tiltGain = tiltAt (v.position);
                }
                else if (tilted)
                {
                    if (v.position <= (double) v.loopStart)
                        v.tiltGain = 1.0f;
                    else if (v.position >= (double) v.loopEnd)
                        v.tiltGain = v.tiltEnd;
                    else
                        v.tiltGain *= v.tiltStep;
                }
            }
            if (v.active)
            {
                v.wheelGain = v.wheelTarget;
                v.controlGain = v.controlTarget;
            }
        }
    }
}
