// THE STAGE (PR7-O 0.8.0): what it does to the sound. Each part heard from its seat through a pair of mics, with the
// room's reflections and tail (dsp/stage/Stage.*), the sampler's parts played apart, and the seats kept.
#include "TestUtils.h"
#include "PluginTestUtils.h"
#include "dsp/sampler/Sampler.h"
#include "dsp/sampler/Section.h"
#include "dsp/stage/Stage.h"
#include "rigs/Rig.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;

    struct Out { std::vector<float> left, right; };

    // Plays parts' sound (the same in both their sides unless `right` is given) through a stage with these seats.
    Out run (stage::Stage& s, const std::vector<std::vector<float>>& parts, const std::vector<stage::Seat>& seats, const stage::Settings& settings,
             const std::function<void (int)>& beforeBlock = {})
    {
        const auto length = parts.empty() ? 0 : parts.front().size();
        Out out { std::vector<float> (length), std::vector<float> (length) };
        std::array<stage::Seat, stage::maxParts> list {};
        for (size_t i = 0; i < seats.size(); ++i)
            list[i] = seats[i];
        s.setSeats (list, (int) seats.size());
        for (size_t at = 0; at < length; at += block)
        {
            if (beforeBlock)
                beforeBlock ((int) at);
            const auto n = (int) std::min<size_t> (block, length - at);
            std::array<const float*, stage::maxParts> l {}, r {};
            for (size_t p = 0; p < parts.size(); ++p)
                l[p] = r[p] = parts[p].data() + at;
            s.process (l, r, (int) parts.size(), out.left.data() + at, out.right.data() + at, n, settings);
        }
        return out;
    }

    std::vector<float> click (double seconds, size_t at = 0)
    {
        std::vector<float> x ((size_t) (seconds * rate));
        x[at] = 1.0f;
        return x;
    }

    std::vector<float> tone (double hz, double seconds, float level = 0.5f)
    {
        std::vector<float> x ((size_t) (seconds * rate));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / rate);
        return x;
    }

    std::vector<float> noise (double seconds, unsigned seed = 1)
    {
        std::vector<float> x ((size_t) (seconds * rate));
        juce::Random random ((juce::int64) seed);
        for (auto& v : x)
            v = random.nextFloat() - 0.5f;
        return x;
    }

    // Where the first sound comes (over a tenth of the loudest) and how loud the first 2 ms of it are.
    std::pair<size_t, double> arrival (const std::vector<float>& x)
    {
        auto peak = 0.0f;
        for (const auto v : x)
            peak = std::max (peak, std::abs (v));
        size_t first = 0;
        while (first < x.size() && std::abs (x[first]) < 0.1f * peak)
            ++first;
        auto loudest = 0.0;
        for (size_t i = first; i < std::min (x.size(), first + 96); ++i)
            loudest = std::max (loudest, (double) std::abs (x[i]));
        return { first, loudest };
    }

    std::unique_ptr<stage::Stage> prepared()
    {
        auto s = std::make_unique<stage::Stage>();
        s->prepare (rate, block);
        return s;
    }

    stage::Settings dry()   // the room turned right off (past the knob's -24 dB): the direct sound alone
    {
        stage::Settings settings;
        settings.room = 0;
        settings.roomLevelDb = -120.0f;
        return settings;
    }
}

TEST_CASE ("STAGE 0.8.0: a part on the left reaches the left mic first and louder; on the right, the right", "[stage]")
{
    for (const auto x : { -6.0f, 6.0f })
    {
        auto owned = prepared(); auto& s = *owned;
        const auto out = run (s, { click (0.3) }, { { x, 3.0f, 0.4f } }, dry());
        const auto [atLeft, levelLeft] = arrival (out.left);
        const auto [atRight, levelRight] = arrival (out.right);
        INFO ("x " << x << ": left at " << atLeft << " (" << levelLeft << "), right at " << atRight << " (" << levelRight << ")");
        if (x < 0.0f)
        {
            CHECK (atLeft < atRight);
            CHECK (levelLeft > levelRight);
        }
        else
        {
            CHECK (atRight < atLeft);
            CHECK (levelRight > levelLeft);
        }
        // The two mics 2 m apart: the difference in time is under 2 m's worth (5.8 ms), and a sizeable part of it.
        const auto apart = (double) std::abs ((long) atLeft - (long) atRight);
        CHECK (apart > 0.4 * rate / 343.0);
        CHECK (apart < 2.0 * rate / 343.0 + 2.0);
    }
}

TEST_CASE ("STAGE 0.8.0: a part in the middle 5 m back keeps its level; farther back is later, quieter and duller", "[stage]")
{
    // Its level: a tone in, the same level out (within 1 dB; the room turned right down).
    {
        auto owned = prepared(); auto& s = *owned;
        const auto out = run (s, { tone (500.0, 1.0) }, { { 0.0f, 5.0f, 0.2f } }, dry());
        const auto in = rms (tone (500.0, 1.0), 24000, 48000);
        CHECK_THAT (db (rms (out.left, 24000, 48000) / in), WithinAbs (0.0, 1.0));
        CHECK_THAT (db (rms (out.right, 24000, 48000) / in), WithinAbs (0.0, 1.0));
    }
    // 2 m back against 10 m back: the far one later by the extra distance (343 m/s, to a couple of samples), quieter.
    auto near = prepared(), far = prepared();
    const auto a = run (*near, { click (0.3) }, { { 0.0f, 2.0f, 0.2f } }, dry());
    const auto b = run (*far, { click (0.3) }, { { 0.0f, 10.0f, 0.2f } }, dry());
    const auto [nearAt, nearLevel] = arrival (a.left);
    const auto [farAt, farLevel] = arrival (b.left);
    const auto d = [] (float y)   // to the left mic (1 m left, 2.5 m in front, 3.2 m up) from a seat 1.2 m over its riser
    {
        const auto up = 3.2f - 1.2f - stage::riserHeight (0.0f, y);
        return std::sqrt (1.0f + (y + 2.5f) * (y + 2.5f) + up * up);
    };
    CHECK_THAT ((double) farAt - (double) nearAt, WithinAbs ((d (10.0f) - d (2.0f)) / 343.0 * rate, 3.0));
    CHECK (farLevel < 0.75 * nearLevel);
    // The air: the far one's 10 kHz falls more against its 500 Hz than the near one's (about 1 dB from 7 m to 16 m
    // from the mics, as in the air of a hall).
    auto nearTones = prepared(), farTones = prepared();
    auto both = tone (500.0, 1.0, 0.3f);
    const auto high = tone (10000.0, 1.0, 0.3f);
    for (size_t i = 0; i < both.size(); ++i)
        both[i] += high[i];
    const auto nt = run (*nearTones, { both }, { { 0.0f, 1.0f, 0.2f } }, dry()).left;
    const auto ft = run (*farTones, { both }, { { 0.0f, 10.5f, 0.2f } }, dry()).left;
    const auto tilt = [] (const std::vector<float>& y) { return db (levelAt (y, 10000.0, 24000, 9600) / levelAt (y, 500.0, 24000, 9600)); };
    INFO ("10 kHz against 500 Hz: near " << tilt (nt) << " dB, far " << tilt (ft) << " dB");
    CHECK (tilt (ft) < tilt (nt) - 0.5);
}

TEST_CASE ("STAGE 0.8.0: the room's tail rings for the ROOM's time x DECAY; ROOM LEVEL sets its level", "[stage]")
{
    // The time for the tail to fall 60 dB, from how long it takes to fall from -10 to -30 dB (x 3).
    const auto tailSeconds = [] (int room, float decay)
    {
        auto owned = prepared(); auto& s = *owned;
        stage::Settings settings;
        settings.room = room;
        settings.decay = decay;
        const auto out = run (s, { click (12.0) }, { { 0.0f, 5.0f, 1.0f } }, settings);
        std::vector<double> levels;   // 20 ms windows, dB
        for (size_t at = 0; at + 960 <= out.left.size(); at += 960)
            levels.push_back (db (rms (out.left, at, at + 960)));
        const auto peak = *std::max_element (levels.begin() + 5, levels.end());   // (past the direct click)
        auto from = -1, to = -1;
        for (int i = 5; i < (int) levels.size(); ++i)
        {
            if (from < 0 && levels[(size_t) i] < peak - 10.0) from = i;
            if (to < 0 && levels[(size_t) i] < peak - 30.0) to = i;
        }
        return to > from && from > 0 ? (to - from) * 0.02 * 3.0 : -1.0;
    };
    const auto hall = tailSeconds (2, 1.0f), cathedral = tailSeconds (3, 1.0f), studio = tailSeconds (0, 1.0f), longer = tailSeconds (2, 2.0f);
    INFO ("hall " << hall << " s, cathedral " << cathedral << " s, studio " << studio << " s, hall x2 " << longer << " s");
    CHECK_THAT (hall, WithinAbs (stage::roomFor (2).seconds, 0.3 * stage::roomFor (2).seconds));
    CHECK_THAT (cathedral, WithinAbs (stage::roomFor (3).seconds, 0.3 * stage::roomFor (3).seconds));
    CHECK_THAT (studio, WithinAbs (stage::roomFor (0).seconds, 0.4 * stage::roomFor (0).seconds));
    CHECK_THAT (longer / hall, WithinAbs (2.0, 0.4));

    // ROOM LEVEL +6 dB: the tail (0.5 s on) 6 dB louder.
    const auto late = [] (float roomLevel)
    {
        auto owned = prepared(); auto& s = *owned;
        stage::Settings settings;
        settings.roomLevelDb = roomLevel;
        const auto out = run (s, { click (1.5) }, { { 0.0f, 5.0f, 1.0f } }, settings);
        return rms (out.left, 24000, 72000);
    };
    CHECK_THAT (db (late (6.0f) / late (0.0f)), WithinAbs (6.0, 0.5));
}

TEST_CASE ("STAGE 0.8.0: the mics farther back hear more room against the direct sound", "[stage]")
{
    // Steady noise from the middle: the room's share (what's left once the direct sound is taken away, measured as the
    // sound 0.3 s after the noise stops against the sound while it plays) grows with MIC.
    const auto roomShare = [] (float mic)
    {
        auto owned = prepared(); auto& s = *owned;
        stage::Settings settings;
        settings.micDistance = mic;
        auto x = noise (2.0);
        std::fill (x.begin() + 48000, x.end(), 0.0f);
        const auto out = run (s, { x }, { { 0.0f, 5.0f, 1.0f } }, settings);
        return rms (out.left, 50400, 55200) / rms (out.left, 24000, 47000);
    };
    const auto close = roomShare (2.0f), far = roomShare (18.0f);
    INFO ("room's share at 2 m " << db (close) << " dB, at 18 m " << db (far) << " dB");
    CHECK (db (far) > db (close) + 4.0);
}

TEST_CASE ("STAGE 0.8.0: SPREAD 0 puts every seat in the middle; a seat moved while a note plays doesn't click", "[stage]")
{
    {
        auto owned = prepared(); auto& s = *owned;
        stage::Settings settings = dry();
        settings.spread = 0.0f;
        const auto out = run (s, { tone (500.0, 0.6) }, { { -6.0f, 3.0f, 2.0f } }, settings);
        CHECK_THAT (db (rms (out.left, 14400, 28800) / rms (out.right, 14400, 28800)), WithinAbs (0.0, 0.3));
    }
    // A tone, its seat jumping across the stage every 0.1 s: no sample-to-sample step bigger than the tone's own (its
    // steepest is 2 pi f / rate x its level, here 0.065), with room for the slide.
    auto owned = prepared(); auto& s = *owned;
    const auto x = tone (500.0, 1.0);
    auto flip = false;
    std::array<stage::Seat, stage::maxParts> seats {};
    const auto out = run (s, { x }, { { -6.0f, 3.0f, 1.0f } }, dry(), [&] (int at)
    {
        if (at % 4800 == 0 && at > 0)
        {
            flip = ! flip;
            seats[0] = { flip ? 6.0f : -6.0f, flip ? 9.0f : 3.0f, 1.0f };
            s.setSeats (seats, 1);
        }
    });
    auto steepest = 0.0f;
    for (size_t i = 4801; i < out.left.size(); ++i)
        steepest = std::max ({ steepest, std::abs (out.left[i] - out.left[i - 1]), std::abs (out.right[i] - out.right[i - 1]) });
    CHECK (steepest < 0.15f);
    for (const auto v : out.left)
        REQUIRE (std::isfinite (v));
}

TEST_CASE ("STAGE 0.8.0: the sampler plays each part into its own pair when asked", "[stage][sampler]")
{
    auto program = std::make_shared<sampler::Program>();
    for (int part = 0; part < 2; ++part)
    {
        sampler::Region r;
        r.sample = testkit::sine (part == 0 ? 220.0 : 440.0, 0.5);
        r.loKey = r.hiKey = r.keyCenter = 60 + part;
        r.velTrack = 0.0f;
        r.part = part;
        program->regions.push_back (r);
    }
    program->index();
    sampler::Sampler s;
    s.prepare (rate);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 61, (juce::uint8) 100), 0);
    std::vector<float> mixL (4800), mixR (4800), aL (4800), aR (4800), bL (4800), bR (4800);
    sampler::Sampler::PartOutputs outputs;
    outputs.count = 2;
    outputs.left = { aL.data(), bL.data() };
    outputs.right = { aR.data(), bR.data() };
    s.render (program.get(), mixL.data(), mixR.data(), 4800, midi, 0, {}, &outputs);
    CHECK (levelAt (aL, 220.0, 1000, 2400) > 0.2);
    CHECK (levelAt (aL, 440.0, 1000, 2400) < 1.0e-3);
    CHECK (levelAt (bL, 440.0, 1000, 2400) > 0.2);
    CHECK (levelAt (bL, 220.0, 1000, 2400) < 1.0e-3);
    CHECK (rms (mixL, 0, 4800) == 0.0);   // (left alone)
}

TEST_CASE ("STAGE 0.8.0: in the plugin, a section's parts sit apart; off, it plays as before; seats kept in projects and presets", "[stage][processor]")
{
    juce::TemporaryFile folder;
    const auto dir = folder.getFile();
    dir.createDirectory();
    testkit::writeWav (dir.getChildFile ("low.wav"), *testkit::sine (220.0, 3.0), 24);
    testkit::writeWav (dir.getChildFile ("high.wav"), *testkit::sine (660.0, 3.0), 24);
    dir.getChildFile ("ViolinEns.sfz").replaceWithText ("<region> sample=high.wav lokey=60 hikey=90 pitch_keycenter=72 amp_veltrack=0\n");
    dir.getChildFile ("CelloEns.sfz").replaceWithText ("<region> sample=low.wav lokey=36 hikey=59 pitch_keycenter=48 amp_veltrack=0\n");
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, rate, 512);
    processor.prepareToPlay (rate, 512);
    processor.loadSection (sampler::section::fromFiles ({ dir.getChildFile ("CelloEns.sfz").getFullPathName(),
                                                          dir.getChildFile ("ViolinEns.sfz").getFullPathName() }));
    REQUIRE (processor.waitForInstrument());
    juce::Timer::callPendingTimersSynchronously();   // (the timer pushes the seats)
    testkit::set (processor, ParamIDs::stageOn, 1.0f);
    // (mics close and the room down: a steady test tone's room ring can lean either way, by the note; music averages it)
    testkit::set (processor, ParamIDs::stageMic, 2.0f);
    testkit::set (processor, ParamIDs::stageRoomLevel, -24.0f);
    // Violins (their usual seat: front left) play 660 Hz from key 72, cellos (front right) 220 Hz from key 48.
    const auto play = [&]
    {
        Out out { std::vector<float> (48000), std::vector<float> (48000) };
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        for (int at = 0; at < 48000; at += 512)
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer none;
            processor.processBlock (buffer, at == 0 ? midi : none);
            const auto n = std::min (512, 48000 - at);   // (the last block only partly kept)
            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, out.left.begin() + at);
            std::copy (buffer.getReadPointer (1), buffer.getReadPointer (1) + n, out.right.begin() + at);
        }
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::allSoundOff (1), 0);
        juce::AudioBuffer<float> rest (2, 512);
        processor.processBlock (rest, off);
        return out;
    };
    const auto staged = play();
    const auto violinsLR = db (levelAt (staged.left, 660.0, 24000, 9600) / levelAt (staged.right, 660.0, 24000, 9600));
    const auto cellosLR = db (levelAt (staged.left, 220.0, 24000, 9600) / levelAt (staged.right, 220.0, 24000, 9600));
    INFO ("violins L-R " << violinsLR << " dB, cellos L-R " << cellosLR << " dB");
    CHECK (violinsLR > 1.0);
    CHECK (cellosLR < -1.0);

    // Off: both in the middle, as recorded (a mono file in both sides).
    testkit::set (processor, ParamIDs::stageOn, 0.0f);
    play();   // (its fade out)
    const auto plain = play();
    CHECK_THAT (db (levelAt (plain.left, 660.0, 24000, 9600) / levelAt (plain.right, 660.0, 24000, 9600)), WithinAbs (0.0, 0.1));

    // A seat moved: kept in the project and in a preset, and undone.
    const auto violins = processor.getStageParts()[1];
    REQUIRE (violins.name == "ViolinEns");
    CHECK_FALSE (violins.moved);
    processor.setSeat ("ViolinEns", { 5.0f, 8.0f, 2.0f });
    processor.seatMoveFinished();
    CHECK (processor.getStageParts()[1].moved);
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    processor.resetSeats();
    CHECK_FALSE (processor.getStageParts()[1].moved);
    processor.setStateInformation (saved.getData(), (int) saved.getSize());
    const auto restored = processor.getStageParts()[1];
    CHECK (restored.moved);
    CHECK ((restored.seat == stage::Seat { 5.0f, 8.0f, 2.0f }));
    Rig rig;
    rig.seats["ViolinEns"] = { -2.0f, 6.0f, 1.5f };
    const auto back = Rig::fromXml (*rig.toXml());
    REQUIRE (back.has_value());
    CHECK ((back->seats.at ("ViolinEns") == stage::Seat { -2.0f, 6.0f, 1.5f }));
}

TEST_CASE ("STAGE 0.8.0: CPU, eight parts sounding", "[stage][performance]")
{
    auto owned = prepared(); auto& s = *owned;
    std::vector<std::vector<float>> parts;
    std::vector<stage::Seat> seats;
    for (int p = 0; p < stage::maxParts; ++p)
    {
        parts.push_back (noise (10.0, (unsigned) p + 1));
        seats.push_back (stage::places()[(size_t) p].seat);
    }
    stage::Settings settings;
    settings.room = 3;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto out = run (s, parts, seats, settings);
    const auto seconds = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
    INFO ("10 s of 8 parts in " << seconds << " s");
    CHECK (seconds < 2.5);   // a quarter of real time, on one core of a build machine
    CHECK (std::isfinite (out.left.back()));
}

TEST_CASE ("STAGE 0.8.0: a wide part's two sides don't cancel each other at any pitch (no comb filtering)", "[stage]")
{
    // A mono tone from a wide seat (3.2 m, the cellos'), its direct sound alone: the same level, within 1.5 dB, at
    // every pitch from 100 Hz to 3 kHz (0.8.0's first try sent the two sides along different paths: up to 20 dB holes).
    std::vector<double> levels;
    for (double hz = 100.0; hz < 3000.0; hz *= 1.19)
    {
        auto owned = prepared(); auto& s = *owned;
        const auto out = run (s, { tone (hz, 0.5) }, { { 3.9f, 2.6f, 3.2f } }, dry());
        levels.push_back (db (levelAt (out.right, hz, 12000, 9600)));
    }
    const auto [lo, hi] = std::minmax_element (levels.begin(), levels.end());
    INFO ("from " << *lo << " to " << *hi << " dB");
    CHECK (*hi - *lo < 1.5);
}
