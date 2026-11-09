// THE INSTRUMENT (PR7-O 0.1.0): SFZ files read (dsp/sampler/Sfz.h) and played (Sampler.h), measured on what comes out:
// which region a note plays, at what pitch and level, through its envelope, its round robins and random choices, and
// the voices. Every SFZ here is invented and written by the test, with generated tones (no library is ever committed).
// Also the PR7-D pieces kept for later stages (the mixer's effects, disk streaming), still working.
#include "TestUtils.h"
#include "PluginTestUtils.h"

#include "dsp/mixer/MixerEffects.h"
#include "dsp/sampler/DiskStreamer.h"
#include "dsp/sampler/Sampler.h"
#include "dsp/sampler/Sfz.h"

#include <map>

namespace
{
    // A folder of its own for each test's files.
    struct Folder
    {
        Folder() { dir.createDirectory(); }
        ~Folder() { dir.deleteRecursively(); }
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonPR7OSampler").getChildFile (juce::Uuid().toString());

        // A sine `seconds` long at `hz`, peak `level`, written as 32-bit float (so it reads back exactly).
        juce::File wav (const juce::String& path, double hz, double seconds = 0.5, float level = 0.5f, int channels = 1, double rate = 48000.0)
        {
            const auto file = dir.getChildFile (path);
            juce::AudioBuffer<float> buffer (channels, (int) (seconds * rate));
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (c, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate) * (c == 1 ? -1.0f : 1.0f));
            testkit::writeWav (file, buffer, rate);
            return file;
        }
        juce::File sfz (const juce::String& name, const juce::String& text)
        {
            const auto file = dir.getChildFile (name);
            file.getParentDirectory().createDirectory();
            file.replaceWithText (text);
            return file;
        }
    };

    // Plays `midi` (positions from the start) through a sampler for `seconds`; both sides.
    std::array<std::vector<float>, 2> render (const sampler::Program& program, const juce::MidiBuffer& midi, double seconds,
                                              sampler::Sampler::Settings settings = {}, sampler::Sampler* use = nullptr)
    {
        sampler::Sampler own;
        auto& s = use != nullptr ? *use : own;
        if (use == nullptr)
            s.prepare (sampleRate);
        std::array<std::vector<float>, 2> out;
        const auto total = (int) (seconds * sampleRate);
        out[0].assign ((size_t) total, 0.0f);
        out[1].assign ((size_t) total, 0.0f);
        for (int start = 0; start < total; start += blockSize)
        {
            const auto n = juce::jmin (blockSize, total - start);
            s.render (&program, out[0].data() + start, out[1].data() + start, n, midi, start, settings);
        }
        return out;
    }

    juce::MidiBuffer noteOn (int key, int velocity = 100, int at = 0, int channel = 1)
    {
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (channel, key, (juce::uint8) velocity), at);
        return m;
    }

    double peakOf (const std::vector<float>& x, size_t from = 0, size_t to = 0)
    {
        auto peak = 0.0f;
        for (auto i = from; i < (to == 0 ? x.size() : to); ++i)
            peak = std::max (peak, std::abs (x[i]));
        return peak;
    }

    // Which of `candidates` (Hz) is loudest over the first 0.1 s from `at` samples.
    double loudestOf (const std::vector<float>& x, std::initializer_list<double> candidates, size_t at = 0)
    {
        auto best = 0.0, bestLevel = -1.0;
        for (const auto hz : candidates)
            if (const auto level = levelAt (x, hz, at, 4800); level > bestLevel)
            {
                best = hz;
                bestLevel = level;
            }
        return best;
    }
}

TEST_CASE ("SFZ: headers pass their settings on; #define, #include, default_path, note names, note_offset; comments out", "[sfz]")
{
    Folder f;
    f.wav ("samples/low.wav", 110.0);
    f.wav ("samples/high.wav", 440.0);
    f.sfz ("common.sfzh", "<global> amp_veltrack=0 ampeg_release=0.3 // every region\n");
    const auto file = f.sfz ("strings.sfz",
        "/* An invented instrument */\n"
        "#define $ROOT 45\n"
        "#include \"common.sfzh\"\n"
        "<control> default_path=samples\\ note_offset=0\n"
        "<group> volume=-6\n"
        "<region> sample=low.wav lokey=c1 hikey=$ROOT pitch_keycenter=a1 // a comment\n"
        "<region> sample=high.wav key=a3 volume=0 pan=-50\n");
    const auto program = sampler::sfz::load (file);
    REQUIRE (program->problems.isEmpty());
    REQUIRE (program->regions.size() == 2);
    const auto& low = program->regions[0];
    const auto& high = program->regions[1];
    CHECK (low.loKey == 24);           // c1: SFZ's names, c4 = 60
    CHECK (low.hiKey == 45);           // $ROOT
    CHECK (low.keyCenter == 33);       // a1
    CHECK_THAT (low.gain, WithinAbs (0.5012, 1.0e-3));   // the group's -6 dB
    CHECK_THAT (low.release, WithinAbs (0.3, 1.0e-6));   // the global's, through #include
    CHECK_THAT (low.velTrack, WithinAbs (0.0, 1.0e-6));
    CHECK (high.loKey == 57);
    CHECK (high.hiKey == 57);
    CHECK (high.keyCenter == 57);      // key= is its root too
    CHECK_THAT (high.gain, WithinAbs (1.0, 1.0e-6));     // its own volume over the group's
    CHECK_THAT (high.pan, WithinAbs (-0.5, 1.0e-6));
    CHECK (program->numFiles == 2);
    CHECK (program->lowestKey() == 24);
    CHECK (program->highestKey() == 57);
    CHECK (program->plays (40));
    CHECK_FALSE (program->plays (50));

    // note_offset / octave_offset move every key.
    const auto shifted = sampler::sfz::load (f.sfz ("shifted.sfz", "<control> octave_offset=1 note_offset=-2\n<region> sample=samples/low.wav key=60\n"));
    REQUIRE (shifted->regions.size() == 1);
    CHECK (shifted->regions[0].loKey == 70);
    CHECK (shifted->regions[0].keyCenter == 70);
}

TEST_CASE ("SFZ: what isn't read yet is listed; missing files, release samples and built-in sounds said; nothing playable is empty", "[sfz]")
{
    Folder f;
    f.wav ("a.wav", 220.0);
    const auto program = sampler::sfz::load (f.sfz ("i.sfz",
        "<region> sample=a.wav loop_mode=loop_continuous loop_start=100 loop_end=2000 xfin_locc1=0 xfin_hicc1=64\n"
        "<region> sample=a.wav loop_mode=one_shot region_label=hit\n"
        "<region> sample=gone.wav\n"
        "<region> sample=a.wav trigger=release\n"
        "<region> sample=*sine\n"));
    CHECK (program->regions.size() == 2);
    CHECK (program->regions[1].oneShot);
    CHECK (program->problems.joinIntoString ("|").contains ("missing: gone.wav"));
    const auto unread = program->unread.joinIntoString ("|");
    INFO (unread);
    CHECK (unread.contains ("loop_mode=loop_continuous (1)"));
    CHECK (unread.contains ("loop_start (1)"));
    CHECK (unread.contains ("xfin_locc1 (1)"));
    CHECK (unread.contains ("release / legato samples (1)"));
    CHECK (unread.contains ("*sine"));
    CHECK_FALSE (unread.contains ("region_label"));   // names change nothing
    CHECK_FALSE (unread.contains ("one_shot"));       // read

    const auto empty = sampler::sfz::load (f.sfz ("empty.sfz", "// nothing\n"));
    CHECK (empty->regions.empty());
    CHECK_FALSE (empty->problems.isEmpty());
    const auto missing = sampler::sfz::load (f.dir.getChildFile ("nowhere.sfz"));
    CHECK (missing->regions.empty());
}

TEST_CASE ("SAMPLER: velocity layers: a note's velocity picks its layer; amp_veltrack's squared curve", "[sampler]")
{
    Folder f;
    f.wav ("p.wav", 220.0);
    f.wav ("mf.wav", 330.0);
    f.wav ("ff.wav", 440.0);
    const auto program = sampler::sfz::load (f.sfz ("layers.sfz",
        "<group> lokey=48 hikey=72 pitch_keycenter=60 amp_veltrack=0\n"
        "<region> sample=p.wav hivel=40\n<region> sample=mf.wav lovel=41 hivel=90\n<region> sample=ff.wav lovel=91\n"));
    REQUIRE (program->regions.size() == 3);
    CHECK (program->velocityLayersAt (60) == 3);
    CHECK (loudestOf (render (*program, noteOn (60, 20), 0.2)[0], { 220.0, 330.0, 440.0 }) == 220.0);
    CHECK (loudestOf (render (*program, noteOn (60, 64), 0.2)[0], { 220.0, 330.0, 440.0 }) == 330.0);
    CHECK (loudestOf (render (*program, noteOn (60, 127), 0.2)[0], { 220.0, 330.0, 440.0 }) == 440.0);
    // Only one region sounds for each note.
    const auto loud = render (*program, noteOn (60, 127), 0.2)[0];
    CHECK (levelAt (loud, 220.0, 0, 4800) < 0.01 * levelAt (loud, 440.0, 0, 4800));

    // amp_veltrack 100 (SFZ's default): velocity 64 about 12 dB under 127 (its squared curve), 127 full.
    const auto curve = sampler::sfz::load (f.sfz ("curve.sfz", "<region> sample=p.wav pitch_keycenter=60\n"));
    const auto full = levelAt (render (*curve, noteOn (60, 127), 0.2)[0], 220.0, 0, 4800);
    const auto mid = levelAt (render (*curve, noteOn (60, 64), 0.2)[0], 220.0, 0, 4800);
    CHECK_THAT (full, WithinAbs (0.5, 0.005));
    CHECK_THAT (db (mid / full), WithinAbs (20.0 * std::log10 (std::pow (64.0 / 127.0, 2.0)), 0.05));
}

TEST_CASE ("SAMPLER: round robins play in turn (seq_length / seq_position); lorand / hirand share the notes at random", "[sampler]")
{
    Folder f;
    f.wav ("1.wav", 220.0);
    f.wav ("2.wav", 330.0);
    f.wav ("3.wav", 440.0);
    const auto rr = sampler::sfz::load (f.sfz ("rr.sfz",
        "<group> key=60 seq_length=3 amp_veltrack=0\n"
        "<region> sample=1.wav seq_position=1\n<region> sample=2.wav seq_position=2\n<region> sample=3.wav seq_position=3\n"));
    REQUIRE (rr->regions.size() == 3);
    CHECK (rr->roundRobinsAt (60) == 3);
    sampler::Sampler s;
    s.prepare (sampleRate);
    std::vector<double> heard;
    for (int n = 0; n < 6; ++n)
    {
        s.reset();
        heard.push_back (loudestOf (render (*rr, noteOn (60), 0.12, {}, &s)[0], { 220.0, 330.0, 440.0 }));
    }
    CHECK (heard == std::vector<double> { 220.0, 330.0, 440.0, 220.0, 330.0, 440.0 });

    const auto random = sampler::sfz::load (f.sfz ("rand.sfz",
        "<group> key=60 amp_veltrack=0\n<region> sample=1.wav hirand=0.5\n<region> sample=2.wav lorand=0.5\n"));
    std::map<double, int> counts;
    s.seedRandom (7);
    for (int n = 0; n < 200; ++n)
    {
        s.reset();
        const auto out = render (*random, noteOn (60), 0.12, {}, &s)[0];
        ++counts[loudestOf (out, { 220.0, 330.0 })];
        // Exactly one of the two each time.
        CHECK (std::min (levelAt (out, 220.0, 0, 4800), levelAt (out, 330.0, 0, 4800)) < 0.01);
    }
    CHECK (counts[220.0] > 70);
    CHECK (counts[330.0] > 70);
}

TEST_CASE ("SAMPLER: pitch by key from the root, tune, transpose, keytrack; a file at another rate plays at its pitch", "[sampler]")
{
    Folder f;
    f.wav ("a.wav", 220.0, 1.0);
    f.wav ("b44.wav", 220.0, 1.0, 0.5f, 1, 44100.0);
    const auto frequency = [] (const sampler::Program& p, int key)
    {
        const auto out = render (p, noteOn (key), 0.6)[0];
        return frequencyOf (out, out.size() / 2, out.size());
    };
    const auto plain = sampler::sfz::load (f.sfz ("a.sfz", "<region> sample=a.wav pitch_keycenter=57\n"));
    CHECK_THAT (frequency (*plain, 57), WithinAbs (220.0, 0.05));
    CHECK_THAT (frequency (*plain, 69), WithinAbs (440.0, 0.1));
    CHECK_THAT (frequency (*plain, 52), WithinAbs (220.0 * std::pow (2.0, -5.0 / 12.0), 0.05));
    const auto tuned = sampler::sfz::load (f.sfz ("t.sfz", "<region> sample=a.wav pitch_keycenter=57 tune=-50 transpose=12\n"));
    CHECK_THAT (frequency (*tuned, 57), WithinAbs (440.0 * std::pow (2.0, -0.5 / 12.0), 0.1));
    const auto fixed = sampler::sfz::load (f.sfz ("k.sfz", "<region> sample=a.wav pitch_keycenter=57 pitch_keytrack=0\n"));
    CHECK_THAT (frequency (*fixed, 70), WithinAbs (220.0, 0.05));   // a percussive region: every key the same pitch
    const auto other = sampler::sfz::load (f.sfz ("r.sfz", "<region> sample=b44.wav pitch_keycenter=57\n"));
    CHECK_THAT (frequency (*other, 57), WithinAbs (220.0, 0.05));
}

TEST_CASE ("SAMPLER: level, pan, offset, end; a mono file in both sides, a stereo one kept apart", "[sampler]")
{
    Folder f;
    f.wav ("m.wav", 220.0);
    f.wav ("s.wav", 220.0, 0.5, 0.5f, 2);
    const auto program = sampler::sfz::load (f.sfz ("l.sfz",
        "<group> amp_veltrack=0 pitch_keycenter=60\n"
        "<region> key=60 sample=m.wav\n<region> key=61 sample=m.wav pitch_keycenter=61 volume=-6 amplitude=50\n"
        "<region> key=62 sample=m.wav pitch_keycenter=62 pan=100\n<region> key=63 sample=s.wav pitch_keycenter=63\n"
        "<region> key=64 sample=m.wav pitch_keycenter=64 offset=4800 end=9599\n"));
    REQUIRE (program->regions.size() == 5);
    const auto plain = render (*program, noteOn (60), 0.3);
    CHECK_THAT (peakOf (plain[0]), WithinAbs (0.5, 1.0e-3));
    CHECK (plain[0] == plain[1]);   // mono: the same both sides
    const auto quieter = render (*program, noteOn (61), 0.3);
    CHECK_THAT (db (peakOf (quieter[0]) / peakOf (plain[0])), WithinAbs (-12.04, 0.05));   // -6 dB, then half
    const auto right = render (*program, noteOn (62), 0.3);
    CHECK (peakOf (right[0]) == 0.0);
    CHECK_THAT (peakOf (right[1]), WithinAbs (0.5, 1.0e-3));
    const auto stereo = render (*program, noteOn (63), 0.3);
    for (size_t i = 0; i < 4800; ++i)
        REQUIRE (stereo[1][i] == -stereo[0][i]);   // the file's right side (written turned over) kept
    // offset / end: from 0.1 s into the file, 0.1 s of it.
    const auto part = render (*program, noteOn (64), 0.4);
    const auto whole = render (*program, noteOn (60), 0.4);
    for (size_t i = 0; i < 4790; ++i)
        REQUIRE (part[0][i] == whole[0][i + 4800]);
    CHECK (peakOf (part[0], 4810) == 0.0);
}

TEST_CASE ("SAMPLER: the envelope: attack rises, decay to sustain; note off releases; one_shot plays on", "[sampler]")
{
    Folder f;
    f.wav ("a.wav", 220.0, 2.0);
    const auto program = sampler::sfz::load (f.sfz ("env.sfz",
        "<group> amp_veltrack=0 pitch_keycenter=60\n"
        "<region> key=60 sample=a.wav ampeg_attack=0.1 ampeg_decay=0.2 ampeg_sustain=25 ampeg_release=0.05\n"
        "<region> key=61 sample=a.wav pitch_keycenter=61 loop_mode=one_shot\n"));
    const auto window = [] (const std::vector<float>& x, double at) { return levelAt (x, 220.0, (size_t) (at * sampleRate), 960); };
    auto midi = noteOn (60);
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), (int) (0.8 * sampleRate));
    const auto out = render (*program, midi, 1.2)[0];
    CHECK_THAT (window (out, 0.045) / 0.5, WithinAbs (0.5, 0.05));    // half way up the attack
    CHECK (window (out, 0.08) > 0.4);                                   // near the top (the decay starts at 0.1 s)
    CHECK_THAT (window (out, 0.6) / 0.5, WithinAbs (0.25, 0.01));      // SUSTAIN 25%
    CHECK (peakOf (out, (size_t) (0.86 * sampleRate)) < 1.0e-4);         // released (0.05 s)

    auto oneShot = noteOn (61);
    oneShot.addEvent (juce::MidiMessage::noteOff (1, 61), 4800);
    const auto played = render (*program, oneShot, 1.5)[0];
    CHECK (window (played, 1.0) > 0.4);   // still playing long after its note-off
}

TEST_CASE ("SAMPLER: POLYPHONY: past it the oldest note fades in 10 ms (no click); MIDI channel; all notes off", "[sampler]")
{
    auto program = sampler::programFrom (testkit::sine (220.0, 3.0), 60);
    program->regions[0].velTrack = 0.0f;
    sampler::Sampler s;
    s.prepare (sampleRate);
    sampler::Sampler::Settings settings;
    settings.polyphony = 4;
    juce::MidiBuffer chord;
    for (int k = 0; k < 6; ++k)
        chord.addEvent (juce::MidiMessage::noteOn (1, 48 + 2 * k, (juce::uint8) 100), k * 2400);
    const auto out = render (*program, chord, 0.5, settings, &s);
    CHECK (s.getActiveVoices() == 4);
    CHECK_FALSE (s.isKeyHeld (48));   // the oldest two went
    CHECK_FALSE (s.isKeyHeld (50));
    CHECK (s.isKeyHeld (58));
    // No jump bigger than the notes themselves make: the stolen voices faded.
    auto biggestStep = 0.0f;
    for (size_t i = 1; i < out[0].size(); ++i)
        biggestStep = std::max (biggestStep, std::abs (out[0][i] - out[0][i - 1]));
    CHECK (biggestStep < 0.15f);

    // The MIDI channel chosen; the regions' own lochan / hichan.
    settings.polyphony = 128;
    settings.channel = 2;
    CHECK (peakOf (render (*program, noteOn (60, 100, 0, 1), 0.1, settings)[0]) == 0.0);
    CHECK (peakOf (render (*program, noteOn (60, 100, 0, 2), 0.1, settings)[0]) > 0.1);
    program->regions[0].loChan = program->regions[0].hiChan = 3;
    settings.channel = 0;
    CHECK (peakOf (render (*program, noteOn (60, 100, 0, 1), 0.1, settings)[0]) == 0.0);
    CHECK (peakOf (render (*program, noteOn (60, 100, 0, 3), 0.1, settings)[0]) > 0.1);

    // All notes off (CC 123) releases everything.
    program->regions[0].loChan = 1;
    program->regions[0].hiChan = 16;
    auto midi = noteOn (60);
    midi.addEvent (juce::MidiMessage::allNotesOff (1), 4800);
    sampler::Sampler t;
    t.prepare (sampleRate);
    render (*program, midi, 0.3, {}, &t);
    CHECK (t.getActiveVoices() == 0);
}

TEST_CASE ("SAMPLER: a new instrument fades the old one's notes over 20 ms", "[sampler]")
{
    auto first = sampler::programFrom (testkit::sine (220.0, 3.0), 60);
    auto second = sampler::programFrom (testkit::sine (330.0, 3.0), 60);
    sampler::Sampler s;
    s.prepare (sampleRate);
    std::vector<float> left (9600), right (9600);
    s.render (first.get(), left.data(), right.data(), 4800, noteOn (60), 0, {});
    CHECK (s.getActiveVoices() == 1);
    s.render (second.get(), left.data() + 4800, right.data() + 4800, 4800, {}, 4800, {});
    CHECK (s.getActiveVoices() == 0);                         // gone within the block (20 ms)
    CHECK (peakOf (left, 4800, 4900) > 0.1);                  // not cut off at once
    CHECK (peakOf (left, 4800 + 960 + 10) == 0.0);            // silent after 20 ms
}

TEST_CASE ("SAMPLER: CPU, 64 notes sounding", "[sampler][performance]")
{
    auto program = sampler::programFrom (testkit::saw (110.0, 6.0, 0.1f, 2), 60);
    sampler::Sampler s;
    s.prepare (sampleRate);
    juce::MidiBuffer chord;
    for (int k = 0; k < 64; ++k)
        chord.addEvent (juce::MidiMessage::noteOn (1, 30 + k, (juce::uint8) 100), 0);
    std::vector<float> left ((size_t) blockSize), right ((size_t) blockSize);
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto blocks = (int) (0.8 * sampleRate) / blockSize;   // (the highest notes run through the 6 s file in under 0.9 s)
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (left.begin(), left.end(), 0.0f);
        s.render (program.get(), left.data(), right.data(), blockSize, b == 0 ? chord : juce::MidiBuffer(), 0, {});
    }
    const auto percent = (juce::Time::getMillisecondCounterHiRes() - started) / 800.0 * 100.0;
    WARN ("64 stereo notes, repitched (cubic): " << percent << "% of one core");
    CHECK (s.getActiveVoices() == 64);
    CHECK (percent < 50.0);   // generous for CI machines
}

TEST_CASE ("Kept for later stages: the mixer's effects still run (every type, finite, OUTPUT applied)", "[mixerfx]")
{
    for (int type = 1; type < mixerfx::typeNames.size(); ++type)
    {
        INFO (mixerfx::typeNames[type]);
        auto effect = mixerfx::make (type);
        REQUIRE (effect != nullptr);
        effect->prepare (sampleRate, blockSize);
        auto slot = mixerfx::defaultSlot (type);
        std::vector<float> left ((size_t) blockSize), right ((size_t) blockSize);
        auto finite = true, sound = false;
        for (int b = 0; b < 40; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
                left[(size_t) i] = right[(size_t) i] = 0.3f * (float) std::sin (0.05 * (b * blockSize + i));
            effect->run (left.data(), right.data(), blockSize, slot);
            for (const auto x : left)
            {
                finite = finite && std::isfinite (x);
                sound = sound || std::abs (x) > 1.0e-3f;
            }
        }
        CHECK (finite);
        CHECK (sound);
    }
}

TEST_CASE ("Kept for later stages: disk streaming reads a file as it is", "[streaming]")
{
    Folder f;
    const auto file = f.wav ("long.wav", 220.0, 1.0);
    sampler::StreamSource source;
    source.file = file;
    source.sampleRate = sampleRate;
    source.length = 48000;
    source.headFrames = 0;
    source.parts = { { 0, 0, 0, 1.0f, 0 }, { 0, 0, 1, 1.0f, 0 } };
    source.numOutputs = 1;
    sampler::Streamer streamer;
    const auto stream = streamer.claim (source);
    REQUIRE (stream >= 0);
    streamer.attach (stream, 0);
    for (juce::int64 frame = 0; frame < 30000; frame += 997)
    {
        const auto expected = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (double) frame / sampleRate);
        CHECK_THAT (streamer.sample (stream, 0, 0, frame, true), WithinAbs (expected, 1.0e-6));
        streamer.consumed (stream, 0, frame);
    }
    CHECK (streamer.getUnderruns() == 0);
}
