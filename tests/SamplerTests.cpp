// THE INSTRUMENT (PR7-O 0.1.0): SFZ files read (dsp/sampler/Sfz.h) and played (Sampler.h), measured on what comes out:
// which region a note plays, at what pitch and level, through its envelope, its round robins and random choices, and
// the voices. Every SFZ here is invented and written by the test, with generated tones (no library is ever committed).
// Also the PR7-D pieces kept for later stages (the mixer's effects, disk streaming), still working.
#include "TestUtils.h"
#include "PluginTestUtils.h"

#include "dsp/mixer/MixerEffects.h"
#include "dsp/sampler/DiskStreamer.h"
#include "dsp/sampler/Sampler.h"
#include "dsp/sampler/Section.h"
#include "dsp/sampler/Sfz.h"

#include <functional>
#include <map>
#include <set>

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
        "<region> sample=a.wav loop_mode=loop_continuous loop_start=100 loop_end=2000 xfin_locc1=0 xfin_hicc1=64 lfo01_freq=5\n"
        "<region> sample=a.wav loop_mode=one_shot region_label=hit\n"
        "<region> sample=gone.wav\n"
        "<region> sample=a.wav trigger=release\n"
        "<region> sample=*sine\n"));
    CHECK (program->regions.size() == 3);   // 0.9.0: the release sample is read
    CHECK (program->regions[1].oneShot);
    CHECK (program->problems.joinIntoString ("|").contains ("missing: gone.wav"));
    const auto unread = program->unread.joinIntoString ("|");
    INFO (unread);
    CHECK (program->regions[0].loop == sampler::Region::Loop::continuous);   // 0.2.0: loops are read
    CHECK_FALSE (unread.contains ("loop_"));
    CHECK_FALSE (unread.contains ("xfin_locc1"));     // 0.9.0: read
    CHECK_FALSE (unread.contains ("release"));
    CHECK (program->regions[2].trigger == sampler::Region::Trigger::release);
    CHECK (unread.contains ("lfo01_freq (1)"));        // the newer numbered LFOs: still not
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

TEST_CASE ("SAMPLER: POLYPHONY: past it a note fades in 10 ms (no click); MIDI channel; all notes off", "[sampler]")
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
    CHECK (s.isKeyHeld (48));         // (0.2.0) the chord's lowest kept
    CHECK_FALSE (s.isKeyHeld (50));   // the oldest inner two went
    CHECK_FALSE (s.isKeyHeld (52));
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

//==============================================================================
// 0.2.0: what VSCO 2 CE's strings ask for. Every file here is invented, laid out as theirs are (measured on the real
// files in the scratchpad, never committed).
namespace
{
    // A tone shaped as a bowed note: `attack` seconds up, steady, `release` seconds of bow-off at the end.
    std::shared_ptr<sampler::Sample> bowed (double hz, double seconds, double attack = 0.3, double release = 0.6, float level = 0.4f)
    {
        auto s = std::make_shared<sampler::Sample>();
        s->name = "bowed";
        s->sampleRate = sampleRate;
        const auto n = (size_t) (seconds * sampleRate);
        s->channels.assign (1, std::vector<float> (n));
        for (size_t i = 0; i < n; ++i)
        {
            const auto t = (double) i / sampleRate;
            const auto shape = std::min ({ 1.0, t / attack, (seconds - t) / release });
            s->channels[0][i] = (float) (level * std::max (0.0, shape) * std::sin (juce::MathConstants<double>::twoPi * hz * t));
        }
        return s;
    }

    // A plucked note: a quick attack, falling 40 dB a second.
    std::shared_ptr<sampler::Sample> plucked (double hz, double seconds)
    {
        auto s = std::make_shared<sampler::Sample>();
        s->name = "plucked";
        s->sampleRate = sampleRate;
        const auto n = (size_t) (seconds * sampleRate);
        s->channels.assign (1, std::vector<float> (n));
        for (size_t i = 0; i < n; ++i)
        {
            const auto t = (double) i / sampleRate;
            s->channels[0][i] = (float) (0.5 * std::min (1.0, t / 0.005) * std::pow (10.0, -2.0 * t) * std::sin (juce::MathConstants<double>::twoPi * hz * t));
        }
        return s;
    }

    double levelOver (const std::vector<float>& x, double hz, double from, double length = 0.2)
    {
        return levelAt (x, hz, (size_t) (from * sampleRate), (size_t) (length * sampleRate));
    }
}

TEST_CASE ("SFZ 0.2.0: each <control> holds for the regions after it; keyswitches read as VSCO 2 CE writes them", "[sfz][keyswitch]")
{
    Folder f;
    f.wav ("Strings/Violin Section/susVib/sus_A3.wav", 220.0, 0.5);
    f.wav ("Strings/Violin Section/Pizz/pizz_A3.wav", 330.0, 0.5);
    // As ViolinEns-KS.sfz: a <control> per articulation, its own default_path (Windows separators), keys by name.
    const auto file = f.sfz ("ViolinKS.sfz",
        "<control>\ndefault_path=Strings\\Violin Section\\susVib\\\n\n"
        "<group> //Begin Sustain Vibrato Group\nsw_default=c2\nsw_lokey=c2\nsw_hikey=d#2\nsw_last=c2\nsw_label=C2 Sustain Vibrato\n"
        "ampeg_attack=0.001\nampeg_release=0.8\nampeg_dynamic=1\n"
        "<region>\nsample=sus_A3.wav\nlokey=55\nhikey=60\npitch_keycenter=57\nlovel=0\nhivel=127\nvolume=20\n"
        "<control>\ndefault_path=Strings\\Violin Section\\Pizz\\\n\n"
        "<group> //Begin Pizzicato Group 1\nsw_lokey=c2\nsw_hikey=d#2\nsw_last=d#2\nsw_label=D#2 Pizzicato\nlorand=0.0 hirand=1.0\n"
        "<region>\nsample=pizz_A3.wav\nlokey=55\nhikey=60\npitch_keycenter=57\n");
    const auto program = sampler::sfz::load (file);
    INFO (program->problems.joinIntoString ("|"));
    REQUIRE (program->problems.isEmpty());
    REQUIRE (program->regions.size() == 2);
    CHECK (program->regions[0].sample->name == "sus_A3");    // each from its own <control>'s folder
    CHECK (program->regions[1].sample->name == "pizz_A3");
    CHECK_THAT (program->regions[0].gain, WithinAbs (10.0, 1.0e-4));   // volume=20: +20 dB
    CHECK (program->unread.isEmpty());   // ampeg_dynamic, sw_label: read
    CHECK (program->swLoKey == 36);
    CHECK (program->swHiKey == 39);
    CHECK (program->swDefault == 36);
    REQUIRE (program->articulations.size() == 2);
    CHECK (program->articulations[0].label == "C2 Sustain Vibrato");
    CHECK (program->articulations[1].lo == 39);
    CHECK (program->articulations[1].label == "D#2 Pizzicato");
    CHECK (program->isKeyswitch (37));
    CHECK_FALSE (program->isKeyswitch (40));
    CHECK (program->articulationOf (39) == 1);
}

TEST_CASE ("SAMPLER 0.2.0: KEYSWITCHES: the default plays; a switch key chooses and is silent; the chosen one stays", "[sampler][keyswitch]")
{
    auto program = std::make_shared<sampler::Program>();
    const auto region = [&] (std::shared_ptr<const sampler::Sample> s, int sw)
    {
        sampler::Region r;
        r.sample = std::move (s);
        r.loKey = 50;
        r.hiKey = 70;
        r.keyCenter = 60;
        r.velTrack = 0.0f;
        r.swLoLast = r.swHiLast = sw;
        program->regions.push_back (r);
    };
    region (testkit::sine (220.0, 1.0), 36);
    region (testkit::sine (330.0, 1.0), 37);
    program->swLoKey = 36;
    program->swHiKey = 39;
    program->swDefault = 36;
    program->articulations = { { 36, 36, "Sustain" }, { 37, 37, "Tremolo" } };
    sampler::prepareRegions (*program);
    program->index();

    sampler::Sampler s;
    s.prepare (sampleRate);
    const auto first = render (*program, noteOn (60), 0.2, {}, &s)[0];
    CHECK (s.getKeyswitch() == 36);
    CHECK (loudestOf (first, { 220.0, 330.0 }) == 220.0);
    CHECK (levelAt (first, 330.0, 0, 4800) < 1.0e-4);
    // The switch key: no sound of its own; from then on the other articulation.
    s.reset();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 37, (juce::uint8) 100), 0);
    const auto switched = render (*program, midi, 0.1, {}, &s)[0];
    CHECK (peakOf (switched) == 0.0);
    CHECK (s.getKeyswitch() == 37);
    const auto after = render (*program, noteOn (60), 0.2, {}, &s)[0];
    CHECK (loudestOf (after, { 220.0, 330.0 }) == 330.0);
    CHECK (levelAt (after, 220.0, 0, 4800) < 1.0e-4);
    // Chosen from the page (as its ARTICULATIONS button does).
    s.chooseKeyswitch (36);
    s.reset();
    CHECK (loudestOf (render (*program, noteOn (60), 0.2, {}, &s)[0], { 220.0, 330.0 }) == 220.0);
}

TEST_CASE ("SAMPLER 0.16.0: ARTICULATIONS switched by a controller's value or by the MIDI channel; the keys switch too", "[sampler][keyswitch]")
{
    // Three articulations: 220 Hz (switch key 36), 330 Hz (37), 440 Hz (38), all on keys 50-70.
    auto program = std::make_shared<sampler::Program>();
    const std::array<double, 3> hz { 220.0, 330.0, 440.0 };
    for (int a = 0; a < 3; ++a)
    {
        sampler::Region r;
        r.sample = testkit::sine (hz[(size_t) a], 1.0);
        r.loKey = 50;
        r.hiKey = 70;
        r.keyCenter = 60;
        r.velTrack = 0.0f;
        r.swLoLast = r.swHiLast = 36 + a;
        program->regions.push_back (r);
    }
    program->swLoKey = 36;
    program->swHiKey = 38;
    program->swDefault = 36;
    program->articulations = { { 36, 36, "Sustain" }, { 37, 37, "Tremolo" }, { 38, 38, "Pizzicato" } };
    sampler::prepareRegions (*program);
    program->index();
    const std::initializer_list<double> all { 220.0, 330.0, 440.0 };

    // CONTROLLER: CC 20's value split evenly (0-42 the first, 43-85 the second, 86-127 the third).
    sampler::Sampler::Settings byCC;
    byCC.switchBy = 1;
    byCC.switchCC = 20;
    sampler::Sampler s;
    s.prepare (sampleRate);
    for (const auto& [value, expected] : { std::pair { 0, 220.0 }, std::pair { 42, 220.0 }, std::pair { 43, 330.0 },
                                           std::pair { 85, 330.0 }, std::pair { 86, 440.0 }, std::pair { 127, 440.0 } })
    {
        INFO ("CC 20 = " << value);
        s.reset();
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::controllerEvent (1, 20, value), 0);
        m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1);
        CHECK (loudestOf (render (*program, m, 0.2, byCC, &s)[0], all) == expected);
    }
    // Another controller does nothing; the keys still switch.
    {
        s.reset();
        s.chooseKeyswitch (36);
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::controllerEvent (1, 21, 127), 0);
        m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1);
        CHECK (loudestOf (render (*program, m, 0.2, byCC, &s)[0], all) == 220.0);
        s.reset();
        juce::MidiBuffer k;
        k.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100), 0);
        k.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1);
        CHECK (loudestOf (render (*program, k, 0.2, byCC, &s)[0], all) == 440.0);
    }

    // MIDI CHANNEL: channel n plays the n-th, several at once; past the last, the one chosen.
    sampler::Sampler::Settings byChannel;
    byChannel.switchBy = 2;
    sampler::Sampler c;
    c.prepare (sampleRate);
    c.chooseKeyswitch (36);
    juce::MidiBuffer both;
    both.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);   // the second: 330 Hz
    both.addEvent (juce::MidiMessage::noteOn (3, 60, (juce::uint8) 100), 0);   // the third: 440 Hz
    const auto out = render (*program, both, 0.3, byChannel, &c)[0];
    CHECK (levelAt (out, 330.0, 4800, 4800) > 0.1);
    CHECK (levelAt (out, 440.0, 4800, 4800) > 0.1);
    CHECK (levelAt (out, 220.0, 4800, 4800) < 1.0e-3);
    c.reset();
    CHECK (loudestOf (render (*program, noteOn (60, 100, 0, 9), 0.2, byChannel, &c)[0], all) == 220.0);   // channel 9: the chosen
    // As keyswitches (the default), the channel doesn't choose.
    c.reset();
    CHECK (loudestOf (render (*program, noteOn (60, 100, 0, 3), 0.2, {}, &c)[0], all) == 220.0);
}

TEST_CASE ("SAMPLER 0.2.0: a loop goes round seamlessly; loop_sustain stops at note-off; its crossfade keeps the level", "[sampler][loop]")
{
    // A 1 s sine of whole cycles (so a plain loop over whole cycles joins exactly): the loop's frames continue as the
    // sine would.
    auto program = sampler::programFrom (testkit::sine (240.0, 1.0), 60);
    auto& r = program->regions[0];
    r.velTrack = 0.0f;
    r.loop = sampler::Region::Loop::continuous;
    r.loopStart = 12000;
    r.loopEnd = 36000;   // 24000 frames: 120 cycles
    r.loopCrossfade = 0;
    sampler::prepareRegions (*program);
    const auto out = render (*program, noteOn (60), 3.0)[0];
    auto worst = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
        worst = std::max (worst, std::abs ((double) out[i] - 0.5 * std::sin (juce::MathConstants<double>::twoPi * 240.0 * (double) i / sampleRate)));
    CHECK (worst < 1.0e-4);   // 3 s from a 1 s file, as one unbroken sine

    // loop_sustain, let go at 2 s: it stops going round and plays on to the file's end (its release here 3 s, long).
    r.loop = sampler::Region::Loop::sustain;
    r.release = 3.0f;
    auto midi = noteOn (60);
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), (int) (2.0 * sampleRate));
    const auto sustained = render (*program, midi, 3.5, {}, nullptr)[0];
    CHECK (peakOf (sustained, (size_t) (1.9 * sampleRate), (size_t) (2.0 * sampleRate)) > 0.4);
    // After the note-off, at most the frames to the loop's end and the file's last third remain: under 1 s.
    CHECK (peakOf (sustained, (size_t) (3.0 * sampleRate)) == 0.0);

    // A crossfaded loop over a noise-like signal (uncorrelated across the join): its level steady through the crossfade.
    auto noise = std::make_shared<sampler::Sample>();
    noise->sampleRate = sampleRate;
    noise->channels.assign (1, std::vector<float> (48000));
    juce::Random random (3);
    for (auto& x : noise->channels[0])
        x = 0.5f * (random.nextFloat() * 2.0f - 1.0f);
    auto np = sampler::programFrom (noise, 60);
    auto& nr = np->regions[0];
    nr.velTrack = 0.0f;
    nr.loop = sampler::Region::Loop::continuous;
    nr.loopStart = 12000;
    nr.loopEnd = 40000;
    nr.loopCrossfade = 9600;   // 0.2 s
    const auto noisy = render (*np, noteOn (60), 2.5)[0];
    const auto plain = rms (noisy, 4800, 24000);
    for (double t = 0.65; t < 2.3; t += 0.05)
    {
        INFO (t);
        CHECK_THAT (db (rms (noisy, (size_t) (t * sampleRate), (size_t) ((t + 0.05) * sampleRate)) / plain), WithinAbs (0.0, 1.0));
    }
}

TEST_CASE ("SAMPLER 0.2.0: AUTO SUSTAIN: a bowed note held past its recording keeps going at its level; a plucked one doesn't loop", "[sampler][autosustain]")
{
    auto program = sampler::programFrom (bowed (220.0, 4.0), 60);
    program->regions[0].velTrack = 0.0f;
    program->regions[0].release = 0.3f;
    sampler::prepareRegions (*program);
    const auto& r = program->regions[0];
    REQUIRE (r.held);
    REQUIRE (r.autoLoop);
    // Its loop in the steady part: after the attack (0.3 s) and before the bow-off (the last 0.6 s).
    CHECK ((double) r.loopStart / sampleRate > 0.6);
    CHECK ((double) r.loopEnd / sampleRate < 3.6);
    CHECK (r.loopCrossfade > (int) (0.2 * sampleRate));

    const auto steady = [] (const std::vector<float>& x, double at) { return levelOver (x, 220.0, at); };
    const auto on = render (*program, noteOn (60), 12.0)[0];
    const auto level = steady (on, 1.5);
    REQUIRE (level > 0.3);
    for (double t = 2.0; t < 11.5; t += 0.25)
    {
        INFO (t);
        CHECK_THAT (db (steady (on, t) / level), WithinAbs (0.0, 1.5));   // the whole 12 s, near its level
    }
    // Let go, it releases as ever.
    auto midi = noteOn (60);
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), (int) (8.0 * sampleRate));
    const auto released = render (*program, midi, 9.5)[0];
    CHECK (peakOf (released, (size_t) (8.5 * sampleRate)) < 0.01);
    // AUTO SUSTAIN off: the recording as it is, gone after 4 s.
    sampler::Sampler::Settings off;
    off.autoSustain = false;
    const auto plain = render (*program, noteOn (60), 6.0, off)[0];
    CHECK (peakOf (plain, (size_t) (4.05 * sampleRate)) == 0.0);

    // A plucked note isn't held: nothing loops (and MOD WHEEL leaves it to velocity).
    auto pluck = sampler::programFrom (plucked (220.0, 4.0), 60);
    CHECK_FALSE (pluck->regions[0].held);
    CHECK_FALSE (pluck->regions[0].autoLoop);
    CHECK (sampler::isSustained (*bowed (220.0, 4.0)));
    CHECK_FALSE (sampler::isSustained (*plucked (220.0, 4.0)));
}

TEST_CASE ("SAMPLER 0.2.0: DYNAMICS ON THE MOD WHEEL: a held note's layers crossfade by the wheel; shorts keep velocity", "[sampler][modwheel]")
{
    // Two held layers (pp 220 Hz, ff 330 Hz; velocity 1-63, 64-127) and a short (440 Hz, any velocity) on another key.
    auto program = std::make_shared<sampler::Program>();
    const auto add = [&] (std::shared_ptr<const sampler::Sample> s, int lo, int hi, int key)
    {
        sampler::Region r;
        r.sample = std::move (s);
        r.loKey = r.hiKey = r.keyCenter = key;
        r.loVel = lo;
        r.hiVel = hi;
        r.velTrack = 0.0f;   // level from the layers alone (the wheel's own curve tested below)
        program->regions.push_back (r);
    };
    add (bowed (220.0, 4.0), 1, 63, 60);
    add (bowed (330.0, 4.0), 64, 127, 60);
    add (plucked (440.0, 1.0), 1, 127, 62);
    sampler::prepareRegions (*program);
    program->index();
    sampler::Sampler::Settings wheel;
    wheel.dynamics = sampler::Sampler::Dynamics::modWheel;
    const auto withWheel = [&] (int cc, int velocity = 100)
    {
        auto midi = juce::MidiBuffer();
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, cc), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) velocity), 1);
        return render (*program, midi, 1.0, wheel)[0];
    };
    // The layers' middles are 32 and 95.5 (in velocity): the wheel at the bottom plays pp alone, at the top ff alone,
    // half way between the middles both at equal power (each 3 dB down). (0.13.1: the wheel runs over velocities 32 to
    // 127, so half way between the middles, 63.75, is the wheel at 42.)
    const auto low = withWheel (0), high = withWheel (127), middle = withWheel (42);
    CHECK (levelOver (low, 220.0, 0.6) > 0.35);
    CHECK (levelOver (low, 330.0, 0.6) < 1.0e-3);
    CHECK (levelOver (high, 330.0, 0.6) > 0.35);
    CHECK (levelOver (high, 220.0, 0.6) < 1.0e-3);
    CHECK_THAT (db (levelOver (middle, 220.0, 0.6) / levelOver (low, 220.0, 0.6)), WithinAbs (-3.0, 0.6));
    CHECK_THAT (db (levelOver (middle, 330.0, 0.6) / levelOver (high, 330.0, 0.6)), WithinAbs (-3.0, 0.6));
    // The note's velocity doesn't choose a held layer on the wheel: a soft note with the wheel up plays ff.
    CHECK (levelOver (withWheel (127, 10), 330.0, 0.6) > 0.35);
    // Moving the wheel while the note is held moves it.
    auto midi = juce::MidiBuffer();
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 0), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), (int) (1.0 * sampleRate));
    const auto moved = render (*program, midi, 2.0, wheel)[0];
    CHECK (levelOver (moved, 220.0, 0.6) > 0.35);
    CHECK (levelOver (moved, 330.0, 1.5) > 0.35);
    CHECK (levelOver (moved, 220.0, 1.5) < 1.0e-3);
    // A short still goes by velocity (amp_veltrack 100 here: velocity 64 about 12 dB under 127), whatever the wheel.
    program->regions[2].velTrack = 1.0f;
    const auto shortAt = [&] (int velocity)
    {
        auto m = juce::MidiBuffer();
        m.addEvent (juce::MidiMessage::controllerEvent (1, 1, 0), 0);
        m.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) velocity), 1);
        return levelOver (render (*program, m, 0.2, wheel)[0], 440.0, 0.0, 0.05);
    };
    CHECK_THAT (db (shortAt (64) / shortAt (127)), WithinAbs (20.0 * std::log10 (std::pow (64.0 / 127.0, 2.0)), 0.3));
    // The wheel's own level curve: amp_veltrack 100 on the held layers, the wheel half way down about 12 dB down.
    program->regions[0].velTrack = program->regions[1].velTrack = 1.0f;
    const auto top = levelOver (withWheel (127), 330.0, 0.6);
    const auto half = withWheel (85);   // the ff layer's middle: it alone, at velocity 95.5 (32 + 85 x 95 / 127 = 95.6)
    CHECK_THAT (db (levelOver (half, 330.0, 0.6) / top), WithinAbs (20.0 * std::log10 (std::pow (95.5 / 127.0, 2.0)), 0.5));
}

TEST_CASE ("SAMPLER 0.13.1: on the mod wheel, a note plays before the wheel is touched, and the wheel at 0 is soft, not silent", "[sampler][modwheel]")
{
    // The owner: "whenever I load a preset I get no volume whatsoever". The presets play DYNAMICS on the mod wheel; the
    // wheel started at 0 and 0 played like velocity 1 (about -84 dB): silence until the wheel was pushed up.
    auto program = std::make_shared<sampler::Program>();
    sampler::Region r;
    r.sample = bowed (220.0, 4.0);
    r.loKey = r.hiKey = r.keyCenter = 60;
    r.velTrack = 1.0f;   // full velocity tracking, as VSCO's files
    program->regions.push_back (r);
    sampler::prepareRegions (*program);
    program->index();
    sampler::Sampler::Settings wheel;
    wheel.dynamics = sampler::Sampler::Dynamics::modWheel;
    const auto play = [&] (int cc)
    {
        auto midi = juce::MidiBuffer();
        if (cc >= 0)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, cc), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1);
        return levelOver (render (*program, midi, 1.0, wheel)[0], 220.0, 0.6);
    };
    const auto top = play (127);
    REQUIRE (top > 0.1);
    // Untouched: the wheel at 100, velocity 32 + 100 x 95 / 127 = 106.8: about 3 dB under the top.
    CHECK_THAT (db (play (-1) / top), WithinAbs (20.0 * std::log10 (std::pow (106.8 / 127.0, 2.0)), 0.3));
    // At 0: velocity 32, about 24 dB under the top: soft, but there.
    CHECK_THAT (db (play (0) / top), WithinAbs (20.0 * std::log10 (std::pow (32.0 / 127.0, 2.0)), 0.3));
}

TEST_CASE ("SAMPLER 0.2.0: CC 11 EXPRESSION scales everything (its squared curve)", "[sampler]")
{
    auto program = sampler::programFrom (testkit::sine (220.0, 1.0), 60);
    program->regions[0].velTrack = 0.0f;
    auto midi = noteOn (60);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 11, 64), 0);
    const auto softer = render (*program, midi, 0.4)[0];
    const auto full = render (*program, noteOn (60), 0.4)[0];
    CHECK_THAT (db (levelOver (softer, 220.0, 0.1) / levelOver (full, 220.0, 0.1)), WithinAbs (20.0 * std::log10 (std::pow (64.0 / 127.0, 2.0)), 0.1));
}

TEST_CASE ("SAMPLER 0.2.0: POLYPHONY counts notes; past it a big chord keeps its lowest and highest notes", "[sampler]")
{
    // Every note three layers deep (on the wheel): one note, three voices.
    auto program = std::make_shared<sampler::Program>();
    for (int layer = 0; layer < 3; ++layer)
    {
        sampler::Region r;
        r.sample = bowed (220.0, 4.0);
        r.loKey = 40;
        r.hiKey = 80;
        r.keyCenter = 60;
        r.loVel = 1 + 42 * layer;
        r.hiVel = layer == 2 ? 127 : 42 + 42 * layer;
        program->regions.push_back (r);
    }
    sampler::prepareRegions (*program);
    program->index();
    sampler::Sampler s;
    s.prepare (sampleRate);
    sampler::Sampler::Settings settings;
    settings.dynamics = sampler::Sampler::Dynamics::modWheel;
    settings.polyphony = 4;
    juce::MidiBuffer chord;
    for (const auto key : { 48, 52, 55, 60, 64, 67 })   // one after another, lowest first
        chord.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 100), (key - 48) * 100);
    render (*program, chord, 0.3, settings, &s);
    CHECK (s.getActiveNotes() == 4);
    CHECK (s.getActiveVoices() == 12);
    CHECK (s.isKeyHeld (48));    // the bass kept
    CHECK (s.isKeyHeld (67));    // the top kept
    CHECK_FALSE (s.isKeyHeld (52));   // the oldest inner notes went
    CHECK_FALSE (s.isKeyHeld (55));
    CHECK (s.isKeyHeld (60));
    CHECK (s.isKeyHeld (64));
}

//==============================================================================
// 0.3.0: the rest of VSCO 2 CE (wind, brass, organ, percussion, keys). Shapes measured on the real files (in the
// scratchpad, never committed), made here from generated tones.
namespace
{
    // A tone at `hz` whose level follows `dbAt (seconds)` (0 dB: peak 0.4), its pitch rising `centsPerSecond`.
    std::shared_ptr<sampler::Sample> shaped (double hz, double seconds, const std::function<double (double)>& dbAt, double centsPerSecond = 0.0)
    {
        auto s = std::make_shared<sampler::Sample>();
        s->name = "shaped";
        s->sampleRate = sampleRate;
        const auto n = (size_t) (seconds * sampleRate);
        s->channels.assign (1, std::vector<float> (n));
        double phase = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const auto t = (double) i / sampleRate;
            phase += hz * std::pow (2.0, centsPerSecond * t / 1200.0) / sampleRate;
            const auto fade = std::min ({ 1.0, t / 0.01, (seconds - t) / 0.01 });   // no click at either end
            s->channels[0][i] = (float) (0.4 * fade * juce::Decibels::decibelsToGain (dbAt (t), -200.0)
                                         * std::sin (juce::MathConstants<double>::twoPi * phase));
        }
        return s;
    }

    // Straight lines between (seconds, dB) points.
    std::function<double (double)> curve (std::vector<std::pair<double, double>> points)
    {
        return [points] (double t)
        {
            if (t <= points.front().first)
                return points.front().second;
            for (size_t i = 1; i < points.size(); ++i)
                if (t <= points[i].first)
                {
                    const auto& [t0, d0] = points[i - 1];
                    const auto& [t1, d1] = points[i];
                    return d0 + (d1 - d0) * (t - t0) / (t1 - t0);
                }
            return points.back().second;
        };
    }

    // The level (RMS, dB) of 0.1 s windows of `x` from `from` to `to` seconds: the widest swing.
    double swingOver (const std::vector<float>& x, double from, double to)
    {
        double lo = 1.0e9, hi = -1.0e9;
        for (auto t = from; t + 0.1 <= to; t += 0.1)
        {
            const auto level = db (rms (x, (size_t) (t * sampleRate), (size_t) ((t + 0.1) * sampleRate)));
            lo = std::min (lo, level);
            hi = std::max (hi, level);
        }
        return hi - lo;
    }
}

TEST_CASE ("SFZ 0.3.0: held or not, judged on the note itself: short brass, an organ's chiff, an accent, a roll are held; struck and plucked aren't", "[sfz][autosustain]")
{
    // A tuba's 3.5 s note: up in 0.3 s, steady to 2 s, then the breath runs out (VSCO's are 3-5 s). 0.2.0 measured the
    // middle of the file, which reaches into the breath-off, and called it short.
    const auto tuba = shaped (55.0, 3.5, curve ({ { 0.0, -20.0 }, { 0.3, 0.0 }, { 2.0, -1.0 }, { 3.5, -50.0 } }));
    CHECK (sampler::isSustained (*tuba));
    // An organ pipe: a chiff 16 dB over the note at its start, then perfectly steady for 10 s.
    const auto organ = shaped (262.0, 10.5, curve ({ { 0.0, 0.0 }, { 0.3, 0.0 }, { 0.5, -16.0 }, { 10.3, -16.0 }, { 10.5, -60.0 } }));
    CHECK (sampler::isSustained (*organ));
    // A brass sforzando into a held note: 15 dB over it for 0.2 s.
    const auto accent = shaped (233.0, 6.0, curve ({ { 0.0, 0.0 }, { 0.2, 0.0 }, { 0.4, -15.0 }, { 5.5, -15.0 }, { 6.0, -60.0 } }));
    CHECK (sampler::isSustained (*accent));
    // A timpani roll: settling 8 dB over 16 s, then the roll stops and the drum rings away over 4 s.
    const auto roll = shaped (98.0, 20.0, curve ({ { 0.0, 0.0 }, { 16.0, -8.0 }, { 20.0, -70.0 } }));
    CHECK (sampler::isSustained (*roll));
    // Struck and plucked: a piano note falling 6 dB a second, a mallet's 3 s ring, the 0.2.0 pluck.
    CHECK_FALSE (sampler::isSustained (*shaped (262.0, 8.0, curve ({ { 0.0, 0.0 }, { 8.0, -48.0 } }))));
    CHECK_FALSE (sampler::isSustained (*shaped (880.0, 3.0, curve ({ { 0.0, 0.0 }, { 0.05, -6.0 }, { 3.0, -60.0 } }))));
    CHECK_FALSE (sampler::isSustained (*plucked (220.0, 4.0)));

    // The short brass note loops in its steady part (0.3.0: a steady second is enough; 0.2.0 asked for 1.5 s).
    auto program = sampler::programFrom (tuba, 45);
    const auto& r = program->regions[0];
    REQUIRE (r.held);
    REQUIRE (r.autoLoop);
    CHECK ((double) (r.loopStart - r.loopCrossfade) / sampleRate >= 0.3);
    CHECK ((double) r.loopEnd / sampleRate <= 2.3);
    program->regions[0].velTrack = 0.0f;
    const auto on = render (*program, noteOn (45), 10.0)[0];
    const auto steady = db (rms (on, (size_t) (1.0 * sampleRate), (size_t) (1.5 * sampleRate)));
    for (double t = 2.0; t < 9.5; t += 0.5)
    {
        INFO (t);
        CHECK_THAT (db (rms (on, (size_t) (t * sampleRate), (size_t) ((t + 0.5) * sampleRate))) - steady, WithinAbs (0.0, 1.5));
    }
}

TEST_CASE ("SAMPLER 0.3.0: AUTO SUSTAIN keeps a burst in the recording out of the loop", "[sampler][autosustain]")
{
    // An organ note, steady for 11 s, with a burst of wind noise 10 dB over it at 9 s (as on VSCO's quiet organ). In
    // 0.2.0 the loop took the last 4 s of the steady part, so the burst came round every 4 s.
    const auto note = shaped (262.0, 12.0, curve ({ { 0.0, -40.0 }, { 0.1, 0.0 }, { 9.0, 0.0 }, { 9.05, 10.0 }, { 9.25, 10.0 },
                                                     { 9.3, 0.0 }, { 11.5, 0.0 }, { 12.0, -60.0 } }));
    auto program = sampler::programFrom (note, 60);
    program->regions[0].velTrack = 0.0f;
    const auto& r = program->regions[0];
    REQUIRE (r.autoLoop);
    const auto loopFrom = (double) (r.loopStart - r.loopCrossfade) / sampleRate, loopTo = (double) r.loopEnd / sampleRate;
    INFO (loopFrom << " to " << loopTo);
    CHECK ((loopTo <= 9.0 || loopFrom >= 9.3));
    CHECK (loopTo - loopFrom > 2.0);
    // Held for 30 s: after the recording's own pass (to 9.4 s), no burst: the level stays within 1 dB.
    const auto on = render (*program, noteOn (60), 30.0)[0];
    CHECK (swingOver (on, 9.5, 29.5) < 1.0);
}

TEST_CASE ("SAMPLER 0.3.0: AUTO SUSTAIN on a near-pure tone drifting in pitch: a shorter crossfade, so no dip", "[sampler][autosustain]")
{
    // A piccolo-like tone (784 Hz, no vibrato) whose pitch creeps up 1 cent a second, steady in level for 8 s. Two
    // places 4 s apart are 4 cents apart: over a 1 s crossfade they drift more than a cycle apart and cancel partway
    // (VSCO's piccolo G5 dipped 5 dB in 0.2.0); a crossfade of a quarter second or less stays in step.
    const auto note = shaped (784.0, 8.5, curve ({ { 0.0, -40.0 }, { 0.2, 0.0 }, { 8.2, 0.0 }, { 8.5, -60.0 } }), 1.0);
    auto program = sampler::programFrom (note, 79);
    program->regions[0].velTrack = 0.0f;
    const auto& r = program->regions[0];
    REQUIRE (r.autoLoop);
    CHECK (r.loopCrossfade <= (int) (0.25 * sampleRate));
    CHECK (r.loopCrossfade >= (int) (0.1 * sampleRate));
    CHECK (r.loopLikeness >= 0.7f);
    // The 1 s crossfade at the same loop would have matched badly: what this choice avoids.
    CHECK (sampler::loopLikeness (*note, r.loopStart, r.loopEnd, (int) sampleRate) < 0.5f);
    const auto on = render (*program, noteOn (79), 25.0)[0];
    CHECK (swingOver (on, 1.0, 24.5) < 1.5);
}

TEST_CASE ("SAMPLER 0.3.0: AUTO SUSTAIN tilts a loop on a note still settling, so each time round doesn't jump back up", "[sampler][autosustain]")
{
    // A roll settling evenly, 0.75 dB a second over 12 s (VSCO's timpani rolls settle 6-12 dB over 16 s): a 4 s loop's
    // end is 3 dB under the sound it goes back to.
    const auto note = shaped (98.0, 13.0, curve ({ { 0.0, -40.0 }, { 0.1, 0.0 }, { 12.0, -9.0 }, { 13.0, -60.0 } }));
    auto program = sampler::programFrom (note, 43);
    program->regions[0].velTrack = 0.0f;
    program->regions[0].release = 10.0f;   // slow, so a step at note-off would show
    const auto& r = program->regions[0];
    REQUIRE (r.held);
    REQUIRE (r.autoLoop);
    const auto loopSeconds = (double) (r.loopEnd - r.loopStart) / sampleRate;
    CHECK_THAT (r.loopTilt, WithinAbs (0.75 * loopSeconds, 0.3));
    // Going round: within 1 dB (0.2.0: a 3 dB step every time round).
    const auto on = render (*program, noteOn (43), 25.0)[0];
    const auto loopFrom = (double) r.loopEnd / sampleRate;   // the first wrap
    CHECK (swingOver (on, loopFrom, 24.5) < 1.0);
    // Let go halfway through a pass: no step in the level (the tilt holds past the loop, into the ending).
    const auto offAt = loopFrom + 2.0 * loopSeconds - 0.5 * loopSeconds;
    auto midi = noteOn (43);
    midi.addEvent (juce::MidiMessage::noteOff (1, 43), (int) (offAt * sampleRate));
    const auto released = render (*program, midi, offAt + 1.0)[0];
    const auto before = db (rms (released, (size_t) ((offAt - 0.05) * sampleRate), (size_t) (offAt * sampleRate)));
    const auto after = db (rms (released, (size_t) (offAt * sampleRate), (size_t) ((offAt + 0.05) * sampleRate)));
    CHECK (std::abs (after - before) < 1.0);
    // A steady note isn't tilted.
    CHECK (std::abs (sampler::findSustain (*shaped (98.0, 8.0, curve ({ { 0.0, 0.0 }, { 7.5, 0.0 }, { 8.0, -60.0 } })))
                         .tilt) < 0.3f);
}

//==============================================================================
// 0.4.0: SECTIONS, several SFZ split over the keys (the owner: "Split, let's build the all strings section"). Two
// invented parts laid out as VSCO 2 CE's basses and violins are: the low one switches at c6-d6 (84-86, inside the high
// one's range), the high one at c2-c#2 (36-37, inside the low one's). Each articulation its own tone, so the pitch
// tells who played.
namespace
{
    struct TwoParts
    {
        Folder folder;
        juce::File low, high;
        TwoParts()
        {
            folder.wav ("low/nv.wav", 110.0, 1.0);
            folder.wav ("low/sus_a.wav", 120.0, 1.0);
            folder.wav ("low/sus_b.wav", 160.0, 1.0);
            folder.wav ("low/pizz.wav", 140.0, 1.0);
            folder.wav ("high/sus.wav", 300.0, 1.0);
            folder.wav ("high/sus2.wav", 500.0, 1.0);
            folder.wav ("high/pizz.wav", 400.0, 1.0);
            low = folder.sfz ("Basses-KS.sfz",
                "<control> default_path=low/\n<global> sw_lokey=c6 sw_hikey=d6 sw_default=c6 amp_veltrack=0\n"
                "<group> sw_last=c6 sw_label=C6 Sustain Non-Vibrato\n<region> sample=nv.wav lokey=24 hikey=60 pitch_keycenter=36\n"
                "<group> sw_last=c#6 sw_label=C#6 Sustain Vibrato\n<region> sample=sus_a.wav lokey=24 hikey=47 pitch_keycenter=36\n"
                "<region> sample=sus_b.wav lokey=48 hikey=60 pitch_keycenter=52\n"
                "<group> sw_last=d6 sw_label=D6 Pizzicato\n<region> sample=pizz.wav lokey=24 hikey=60 pitch_keycenter=36\n");
            high = folder.sfz ("Violins-KS.sfz",
                "<control> default_path=high/\n<global> sw_lokey=c2 sw_hikey=c#2 sw_default=c2 amp_veltrack=0\n"
                "<group> sw_last=c2 sw_label=C2 Sustain Vibrato\n<region> sample=sus.wav lokey=48 hikey=72 pitch_keycenter=60\n"
                "<region> sample=sus2.wav lokey=73 hikey=86 pitch_keycenter=84\n"
                "<group> sw_last=c#2 sw_label=C#2 Pizzicato\n<region> sample=pizz.wav lokey=48 hikey=86 pitch_keycenter=60\n");
        }
    };

    // Which tone a note plays, after a switch (none: -1).
    double heard (const sampler::Program& program, int key, int keyswitch = -1)
    {
        auto midi = keyswitch >= 0 ? noteOn (keyswitch) : juce::MidiBuffer();
        midi.addEvents (noteOn (key, 100, 480), 0, -1, 0);
        const auto out = render (program, midi, 0.25)[0];
        if (peakOf (out, 480) < 1.0e-3)
            return 0.0;
        return loudestOf (out, { 110.0, 120.0, 140.0, 160.0, 300.0, 400.0, 500.0 }, 4800);
    }

    std::set<juce::String> filesIn (const sampler::Program& program)
    {
        std::set<juce::String> names;
        for (const auto& r : program.regions)
            names.insert (juce::File (r.sample->path).getFileName());
        return names;
    }
}

TEST_CASE ("SECTION 0.4.0: kept as text; split where each next part's range begins; only the split's samples loaded", "[sfz][section]")
{
    TwoParts files;
    // Chosen in any order: low to high, the low one up to the key under the high one's lowest.
    const auto section = sampler::section::fromFiles ({ files.high.getFullPathName(), files.low.getFullPathName(), "/not/there.sfz" });
    REQUIRE (section.parts.size() == 2);
    CHECK (section.parts[0].file == files.low.getFullPathName());
    CHECK (section.parts[0].low == 24);
    CHECK (section.parts[0].high == 47);
    CHECK (section.parts[1].file == files.high.getFullPathName());
    CHECK (section.parts[1].low == 48);
    CHECK (section.parts[1].high == 86);
    // Its text, where a file's path would be.
    const auto text = section.toText();
    CHECK (sampler::section::isSection (text));
    CHECK_FALSE (sampler::section::isSection (files.low.getFullPathName()));
    REQUIRE (sampler::section::fromText (text).has_value());
    CHECK (*sampler::section::fromText (text) == section);
    CHECK_FALSE (sampler::section::fromText ("<Section></Section>").has_value());
    CHECK_FALSE (sampler::section::fromText ("<Section><Part").has_value());

    const auto program = sampler::section::load (section);
    CHECK (program->problems.isEmpty());
    CHECK (program->name == "Basses-KS + Violins-KS");
    // The low part's keys stop at 47: its 48-60 recording (sus_b) isn't loaded; every region is cut to its part's keys.
    CHECK (filesIn (*program) == std::set<juce::String> { "nv.wav", "sus_a.wav", "pizz.wav", "sus.wav", "sus2.wav" });
    CHECK (program->numFiles == 6);   // the two pizz.wav are different files
    CHECK (program->lowestKey() == 24);
    CHECK (program->highestKey() == 86);
    for (const auto& r : program->regions)
    {
        const auto fromLow = (juce::File (r.sample->path).getParentDirectory().getFileName() == "low");
        CHECK ((fromLow ? r.hiKey <= 47 : r.loKey >= 48));
    }
}

TEST_CASE ("SECTION 0.4.0: one set of keyswitches over the top for every part, matched by name; the parts' own switch keys play notes", "[sampler][section]")
{
    TwoParts files;
    const auto program = sampler::section::load (sampler::section::fromFiles ({ files.low.getFullPathName(), files.high.getFullPathName() }));
    // Sustain, Sustain Non-Vibrato, Pizzicato: from the first C over the top note (86): 96, 97, 98. Starts on Sustain.
    REQUIRE (program->articulations.size() == 3);
    CHECK (program->swLoKey == 96);
    CHECK (program->swHiKey == 98);
    CHECK (program->swDefault == 96);
    CHECK (program->articulations[0].label == "C7 Sustain");
    CHECK (program->articulations[1].label == "C#7 Sustain Non-Vibrato");
    CHECK (program->articulations[2].label == "D7 Pizzicato");

    // Sustain (the default): each side of the split plays its own part's sustain.
    CHECK (heard (*program, 36) == 120.0);
    CHECK (heard (*program, 60) == 300.0);
    CHECK (heard (*program, 47) > 0.0);   // the low part's top key
    // Pizzicato: both parts switch.
    CHECK (heard (*program, 36, 98) == 140.0);
    CHECK (heard (*program, 60, 98) == 400.0);
    // Sustain Non-Vibrato: the low part has it; the high one plays its sustain.
    CHECK (heard (*program, 36, 97) == 110.0);
    CHECK (heard (*program, 60, 97) == 300.0);
    // The parts' own switch keys are notes now: 36 (the high part's) a bass note, 84 (the low part's) a violin note.
    CHECK (heard (*program, 84) == 500.0);
    CHECK_FALSE (program->isKeyswitch (36));
    CHECK_FALSE (program->isKeyswitch (84));

    // The kinds, from VSCO 2 CE's labels.
    CHECK (sampler::section::kindOf ("C2 Sustain") == "sustain");
    CHECK (sampler::section::kindOf ("C#6 Sustain Vibrato") == "sustain");
    CHECK (sampler::section::kindOf ("C6 Sustain Non-Vibrato") == "sustain non-vibrato");
    CHECK (sampler::section::kindOf ("C#2 Tremolo") == "tremolo");
    CHECK (sampler::section::kindOf ("D#6 Spiccato") == "spiccato");
    CHECK (sampler::section::kindOf ("E6 Pizzicato") == "pizzicato");
    CHECK (sampler::section::kindOf ("C1 Staccato") == "staccato");
    CHECK (sampler::section::kindOf ("Col legno") == "col legno");
}

TEST_CASE ("SECTION 0.4.0: a split moved reads only what's new; the rest is the same samples, in memory", "[sfz][section]")
{
    TwoParts files;
    auto section = sampler::section::fromFiles ({ files.low.getFullPathName(), files.high.getFullPathName() });
    const auto before = sampler::section::load (section);
    std::map<juce::String, std::shared_ptr<const sampler::Sample>> inMemory;
    for (const auto& r : before->regions)
        inMemory[r.sample->path] = r.sample;
    // The split up two keys: the low part now plays 48-49, so its sus_b is needed.
    section.parts[0].high = 49;
    section.parts[1].low = 50;
    juce::StringArray read;
    const auto after = sampler::section::load (section, {}, [&] (const juce::String& path) -> std::shared_ptr<const sampler::Sample>
    {
        if (const auto it = inMemory.find (path); it != inMemory.end())
            return it->second;
        read.add (juce::File (path).getFileName());
        return nullptr;
    });
    CHECK (read == juce::StringArray { "sus_b.wav" });
    for (const auto& r : after->regions)
        if (const auto it = inMemory.find (r.sample->path); it != inMemory.end())
            CHECK (r.sample == it->second);
    // Key 49 is the low part's now, 50 the high part's.
    const auto partAt = [&after] (int key)
    {
        juce::StringArray parts;
        for (const auto i : after->byKey[(size_t) key])
            if (after->regions[(size_t) i].playsUnder (after->swDefault))
                parts.addIfNotAlreadyThere (juce::File (after->regions[(size_t) i].sample->path).getParentDirectory().getFileName() == "low" ? "low" : "high");
        return parts.joinIntoString (",");
    };
    CHECK (partAt (49) == "low");
    CHECK (partAt (50) == "high");
}

TEST_CASE ("SECTION 0.6.0: default splits by the middle of each range; a part that would squeeze the one beneath shares the middle of the overlap (brass)", "[sfz][section]")
{
    // Invented files with VSCO 2 CE's brass sustains' ranges: tuba 29-62, tenor trombone 34-65, horn 33-77, trumpet
    // 52-84.
    Folder folder;
    folder.wav ("t.wav", 220.0, 0.2);
    const auto file = [&folder] (const char* name, int low, int high)
    {
        return folder.sfz (name, "<region> sample=t.wav lokey=" + juce::String (low) + " hikey=" + juce::String (high) + " pitch_keycenter=60\n")
            .getFullPathName();
    };
    const auto tuba = file ("TubaSus.sfz", 29, 62), trombone = file ("TromboneSus.sfz", 34, 65);
    const auto horn = file ("FHornSus.sfz", 33, 77), trumpet = file ("TrumpetSus.sfz", 52, 84);
    const auto section = sampler::section::fromFiles ({ trumpet, horn, tuba, trombone });
    REQUIRE (section.parts.size() == 4);
    CHECK (section.parts[0].file == tuba);
    CHECK (section.parts[1].file == trombone);
    CHECK (section.parts[2].file == horn);   // above the trombone, though its lowest note is under the trombone's
    CHECK (section.parts[3].file == trumpet);
    CHECK ((section.parts[0].low == 29 && section.parts[0].high == 47));   // the trombone from 34 would leave it 5 keys
    CHECK ((section.parts[1].low == 48 && section.parts[1].high == 56));   // 48: the middle of 34-62
    CHECK ((section.parts[2].low == 57 && section.parts[2].high == 67));   // 57: the middle of 49-65
    CHECK ((section.parts[3].low == 68 && section.parts[3].high == 84));   // 68: the middle of 58-77
    // The strings' splits are as they were (each from its own lowest, the violas keeping 7 keys): basses 24-60,
    // cellos 36-77, violas 48-86, violins 55-86.
    const auto strings = sampler::section::fromFiles ({ file ("V.sfz", 55, 86), file ("C.sfz", 36, 77), file ("B.sfz", 24, 60), file ("A.sfz", 48, 86) });
    REQUIRE (strings.parts.size() == 4);
    CHECK ((strings.parts[0].low == 24 && strings.parts[0].high == 35));
    CHECK ((strings.parts[1].low == 36 && strings.parts[1].high == 47));
    CHECK ((strings.parts[2].low == 48 && strings.parts[2].high == 54));
    CHECK ((strings.parts[3].low == 55 && strings.parts[3].high == 86));
}

namespace
{
    // 0.7.0: a held note per key, each its own tone (no keytracking between them), every one rising from -30 dB to its
    // level over 0.4 s (a slow bowed attack), 4 s long; a plucked one on key 64.
    std::shared_ptr<sampler::Program> legatoInstrument()
    {
        auto program = std::make_shared<sampler::Program>();
        const auto add = [&] (std::shared_ptr<const sampler::Sample> s, int key)
        {
            sampler::Region r;
            r.sample = std::move (s);
            r.loKey = r.hiKey = r.keyCenter = key;
            r.velTrack = 0.0f;
            r.release = 0.05f;
            program->regions.push_back (r);
        };
        const auto bowedAttack = curve ({ { 0.0, -30.0 }, { 0.4, 0.0 }, { 4.0, 0.0 } });
        add (shaped (220.0, 4.0, bowedAttack), 60);
        add (shaped (330.0, 4.0, bowedAttack), 62);
        add (plucked (440.0, 1.0), 64);
        add (shaped (550.0, 4.0, bowedAttack), 73);   // 13 semitones over 60: past an octave
        sampler::prepareRegions (*program);
        program->index();
        return program;
    }

    juce::MidiBuffer notes (std::initializer_list<std::tuple<bool, int, double>> events)   // (on, key, seconds)
    {
        juce::MidiBuffer m;
        for (const auto& [on, key, at] : events)
            m.addEvent (on ? juce::MidiMessage::noteOn (1, key, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, key),
                        (int) std::lround (at * sampleRate));
        return m;
    }
}

TEST_CASE ("SFZ 0.7.0: a held recording's LEGATO start is where its attack is over; a short has none", "[sfz][legato]")
{
    const auto program = legatoInstrument();
    const auto& bowedNote = program->regions[0];
    REQUIRE (bowedNote.held);
    // Its level reaches within 3 dB of its body at 0.3 s (-30 dB to 0 dB over 0.4 s); the 50 ms windows place it there.
    CHECK_THAT ((double) bowedNote.legatoStart / sampleRate, WithinAbs (0.3, 0.06));
    CHECK (bowedNote.legatoStart <= bowedNote.loopStart);   // before AUTO SUSTAIN's loop
    CHECK (program->regions[2].legatoStart == -1);           // the plucked note
}

TEST_CASE ("SAMPLER 0.7.0: LEGATO joins a note played over a held one: past its attack, an equal-power crossfade, timed by how fast you play", "[sampler][legato]")
{
    const auto program = legatoInstrument();
    sampler::Sampler::Settings legato;
    legato.legato = true;
    const auto level = [] (const std::vector<float>& x, double hz, double at, double length = 0.03)
    {
        return levelAt (x, hz, (size_t) (at * sampleRate), (size_t) (length * sampleRate));
    };
    // The tones' steady levels (one note held alone).
    const auto steady220 = level (render (*program, noteOn (60), 1.5)[0], 220.0, 1.2);
    const auto steady330 = level (render (*program, noteOn (62), 1.5)[0], 330.0, 1.2);
    REQUIRE (steady220 > 0.2);

    // 60 held, 62 played at 1.0 s: joined. A second since the first note: the crossfade is its longest, 0.15 s.
    const auto line = notes ({ { true, 60, 0.0 }, { true, 62, 1.0 } });
    const auto joined = render (*program, line, 1.6, legato)[0];
    CHECK_THAT (db (level (joined, 330.0, 1.17) / steady330), WithinAbs (0.0, 1.0));   // at its level: no 0.4 s attack again
    CHECK (level (joined, 220.0, 1.05) > 0.3 * steady220);                            // the old note in the crossfade...
    CHECK (level (joined, 220.0, 1.17) < 0.01 * steady220);                           // ...and gone after it
    // Equal power: the two tones' power together stays at one tone's through the crossfade (within 1 dB).
    for (double t = 0.95; t < 1.25; t += 0.01)
    {
        const auto a = level (joined, 220.0, t, 0.01) / steady220, b = level (joined, 330.0, t, 0.01) / steady330;
        INFO (t);
        CHECK_THAT (db (std::sqrt (a * a + b * b)), WithinAbs (0.0, 1.0));
    }
    // LEGATO off (as 0.6.0): both sound, the new one from its attack (-30 dB at its start, about -12 dB 0.15 s in).
    const auto plain = render (*program, line, 1.6)[0];
    CHECK (level (plain, 220.0, 1.17) > 0.9 * steady220);
    CHECK (db (level (plain, 330.0, 1.13) / steady330) < -8.0);

    // Played fast (the second note 0.2 s after the first): a crossfade of a quarter of that, 0.05 s.
    const auto fast = render (*program, notes ({ { true, 60, 0.0 }, { true, 62, 0.2 } }), 0.6, legato)[0];
    CHECK (level (fast, 220.0, 0.26) < 0.01 * steady220);
    CHECK_THAT (sampler::Sampler::joinSeconds (0.2), WithinAbs (0.05, 1.0e-9));
    CHECK_THAT (sampler::Sampler::joinSeconds (0.01), WithinAbs (sampler::Sampler::shortestJoin, 1.0e-9));
    CHECK_THAT (sampler::Sampler::joinSeconds (5.0), WithinAbs (sampler::Sampler::longestJoin, 1.0e-9));

    // A chord (pressed 10 ms apart, under the 40 ms window) isn't joined: both sound.
    const auto chord = render (*program, notes ({ { true, 60, 0.0 }, { true, 62, 0.01 } }), 0.8, legato)[0];
    CHECK (level (chord, 220.0, 0.6) > 0.9 * steady220);
    CHECK (level (chord, 330.0, 0.6) > 0.9 * steady330);
    // Past an octave isn't joined either (key 73 over 60).
    const auto wide = render (*program, notes ({ { true, 60, 0.0 }, { true, 73, 1.0 } }), 1.5, legato)[0];
    CHECK (level (wide, 220.0, 1.3) > 0.9 * steady220);
    // A short (plucked) note over a held one is never joined: the held one goes on.
    const auto pluck = render (*program, notes ({ { true, 60, 0.0 }, { true, 64, 1.0 } }), 1.5, legato)[0];
    CHECK (level (pluck, 220.0, 1.3) > 0.9 * steady220);
    CHECK (level (pluck, 440.0, 1.02) > 0.05);

    // Let go of the joined note while the first key is still down: it joins back, at its level at once.
    const auto trill = render (*program, notes ({ { true, 60, 0.0 }, { true, 62, 1.0 }, { false, 62, 2.0 } }), 2.5, legato)[0];
    CHECK_THAT (db (level (trill, 220.0, 2.17) / steady220), WithinAbs (0.0, 1.0));
    CHECK (level (trill, 330.0, 2.2) < 0.01 * steady330);
    // ...and letting go of both ends it.
    const auto both = render (*program, notes ({ { true, 60, 0.0 }, { true, 62, 1.0 }, { false, 62, 2.0 }, { false, 60, 2.5 } }), 3.0, legato)[0];
    CHECK (peakOf (both, (size_t) (2.9 * sampleRate)) < 1.0e-3);
    // The first key let go first: the joined note goes on alone; letting it go ends it (nothing to join back to).
    const auto firstUp = render (*program, notes ({ { true, 60, 0.0 }, { true, 62, 1.0 }, { false, 60, 1.5 }, { false, 62, 2.0 } }), 2.5, legato)[0];
    CHECK (level (firstUp, 330.0, 1.8) > 0.9 * steady330);
    CHECK (peakOf (firstUp, (size_t) (2.4 * sampleRate)) < 1.0e-3);
}

TEST_CASE ("SAMPLER 0.7.0: GLIDE slides a joined note's pitch from the old note's over its time", "[sampler][legato]")
{
    const auto program = legatoInstrument();
    sampler::Sampler::Settings glide;
    glide.legato = true;
    glide.glideSeconds = 0.2f;
    const auto out = render (*program, notes ({ { true, 60, 0.0 }, { true, 62, 1.0 } }), 1.6, glide)[0];
    // Past the crossfade (0.15 s) and before the glide's end (0.2 s): on its way up, 87% of the 7 semitones at 1.175 s.
    const auto during = frequencyOf (out, (size_t) (1.16 * sampleRate), (size_t) (1.19 * sampleRate));
    CHECK (during > 295.0);
    CHECK (during < 328.0);
    CHECK_THAT (frequencyOf (out, (size_t) (1.3 * sampleRate), (size_t) (1.5 * sampleRate)), WithinAbs (330.0, 2.0));
    // No glide: at its own pitch as soon as the crossfade is over.
    sampler::Sampler::Settings none;
    none.legato = true;
    const auto straight = render (*program, notes ({ { true, 60, 0.0 }, { true, 62, 1.0 } }), 1.6, none)[0];
    CHECK_THAT (frequencyOf (straight, (size_t) (1.16 * sampleRate), (size_t) (1.19 * sampleRate)), WithinAbs (330.0, 4.0));
}

// ================================================================ 0.9.0: the rest of SFZ's sound (Sonatina's files)
namespace
{
    // Tones summed (each peak 0.25), `seconds` long, both sides (the right one's tones `rightScale` x).
    std::shared_ptr<sampler::Sample> tones (std::initializer_list<double> hz, double seconds, float rightScale = 1.0f)
    {
        auto s = std::make_shared<sampler::Sample>();
        s->name = "tones";
        s->sampleRate = sampleRate;
        const auto n = (size_t) (seconds * sampleRate);
        s->channels.assign (2, std::vector<float> (n));
        for (size_t i = 0; i < n; ++i)
            for (const auto f : hz)
            {
                const auto x = 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * f * (double) i / sampleRate);
                s->channels[0][i] += x;
                s->channels[1][i] += x * rightScale;
            }
        return s;
    }

    // A program of regions written as SFZ text over one generated sample (the reader's own parsing, the sample swapped in).
    std::shared_ptr<sampler::Program> withSample (Folder& f, const juce::String& text, std::shared_ptr<const sampler::Sample> sample)
    {
        f.wav ("t.wav", 220.0, 0.1);
        auto program = sampler::sfz::load (f.sfz ("i.sfz", text));
        for (auto& r : program->regions)
            r.sample = sample;
        sampler::prepareRegions (*program);
        program->index();
        return program;
    }

    juce::MidiBuffer events (std::initializer_list<juce::MidiMessage> messages, std::initializer_list<double> seconds)
    {
        juce::MidiBuffer m;
        auto at = seconds.begin();
        for (const auto& message : messages)
            m.addEvent (message, (int) std::lround (*at++ * sampleRate));
        return m;
    }
}

TEST_CASE ("SFZ 0.9.0: FILTERS: low-pass and high-pass with their slopes; the cutoff moved by key, velocity and a controller", "[sfz][sampler][filter]")
{
    Folder f;
    const auto both = tones ({ 200.0, 5000.0 }, 1.0);
    const auto measure = [&] (const juce::String& opcodes, juce::MidiBuffer midi = noteOn (60))
    {
        const auto program = withSample (f, "<region> sample=t.wav key=60 amp_veltrack=0 " + opcodes + "\n", both);
        const auto out = render (*program, midi, 0.5)[0];
        return std::pair { db (levelOver (out, 200.0, 0.3, 0.1) / 0.25), db (levelOver (out, 5000.0, 0.3, 0.1) / 0.25) };
    };
    // Read: the type, cutoff, tracking.
    const auto read = withSample (f, "<region> sample=t.wav fil_type=hpf_1p cutoff=800 resonance=3 fil_keytrack=100 fil_keycenter=c4 fil_veltrack=1200 cutoff_cc1=2400\n", both);
    REQUIRE (read->regions.size() == 1);
    const auto& filter = read->regions[0].filters[0];
    CHECK (filter.type == sampler::Region::Filter::Type::highpass1);
    CHECK ((filter.cutoff == 800.0f && filter.resonance == 3.0f && filter.keytrack == 100.0f && filter.keycenter == 60 && filter.veltrack == 1200.0f));
    REQUIRE (filter.cc.size() == 1);
    CHECK ((filter.cc[0].first == 1 && filter.cc[0].second == 2400.0f));
    // A 2-pole low-pass at 1 kHz: 200 Hz through, 5 kHz (2.3 octaves up) about 28 dB down; 1-pole: about 14 dB.
    const auto [low2, high2] = measure ("fil_type=lpf_2p cutoff=1000");
    CHECK_THAT (low2, WithinAbs (0.0, 0.5));
    CHECK_THAT (high2, WithinAbs (-28.0, 3.0));
    const auto [low1, high1] = measure ("fil_type=lpf_1p cutoff=1000");
    CHECK_THAT (high1, WithinAbs (-14.0, 2.0));
    CHECK (low1 > -0.5);
    // A high-pass at 2 kHz: the reverse.
    const auto [lowH, highH] = measure ("fil_type=hpf_2p cutoff=2000");
    CHECK (lowH < -30.0);
    CHECK (highH > -1.0);
    // The mod wheel up 4 octaves (cutoff_cc1=4800 cents): 5 kHz through again.
    juce::MidiBuffer wheelUp;
    wheelUp.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);
    wheelUp.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1);
    CHECK (measure ("fil_type=lpf_2p cutoff=1000 cutoff_cc1=4800", wheelUp).second > -3.0);
    // Key tracking: an octave up the cutoff follows (fil_keytrack=100): the note at 72 lets more of 5 kHz through.
    const auto program = withSample (f, "<region> sample=t.wav lokey=48 hikey=84 pitch_keycenter=60 pitch_keytrack=0 amp_veltrack=0 fil_type=lpf_2p cutoff=1000 fil_keytrack=100\n", both);
    const auto at60 = render (*program, noteOn (60), 0.5)[0], at72 = render (*program, noteOn (72), 0.5)[0];
    CHECK_THAT (db (levelOver (at72, 5000.0, 0.3, 0.1) / levelOver (at60, 5000.0, 0.3, 0.1)), WithinAbs (12.0, 2.0));
}

TEST_CASE ("SFZ 0.9.0: controllers: gain_cc, crossfades between layers on the mod wheel (equal power), set_cc as the start", "[sfz][sampler][cc]")
{
    Folder f;
    const auto a = tones ({ 220.0 }, 1.0), b = tones ({ 330.0 }, 1.0);
    f.wav ("t.wav", 220.0, 0.1);
    auto program = sampler::sfz::load (f.sfz ("i.sfz",
        "<control> set_cc1=0 label_cc1=Dynamics\n"
        "<group> key=60 amp_veltrack=0 xf_cccurve=power\n"
        "<region> sample=t.wav xfout_locc1=32 xfout_hicc1=96\n"
        "<region> sample=t.wav xfin_locc1=32 xfin_hicc1=96 gain_cc7=-12\n"));
    REQUIRE (program->regions.size() == 2);
    program->regions[0].sample = a;
    program->regions[1].sample = b;
    program->index();
    CHECK (program->initialCC[1] == 0);
    CHECK (program->ccLabels[1] == "Dynamics");
    CHECK (program->ownWheel);
    const auto at = [&] (int wheel, int seven = 0)
    {
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::controllerEvent (1, 1, wheel), 0);
        m.addEvent (juce::MidiMessage::controllerEvent (1, 7, seven), 0);
        m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1);
        const auto out = render (*program, m, 0.5)[0];
        return std::pair { levelOver (out, 220.0, 0.3, 0.1) / 0.25, levelOver (out, 330.0, 0.3, 0.1) / 0.25 };
    };
    const auto [lowA, lowB] = at (0);
    CHECK_THAT (lowA, WithinAbs (1.0, 0.02));
    CHECK (lowB < 1.0e-3);
    const auto [midA, midB] = at (64);   // half way: each at equal power (sqrt 0.5, -3 dB)
    CHECK_THAT (db (midA), WithinAbs (-3.0, 0.3));
    CHECK_THAT (db (midB), WithinAbs (-3.0, 0.3));
    const auto [highA, highB] = at (127);
    CHECK (highA < 1.0e-3);
    CHECK_THAT (highB, WithinAbs (1.0, 0.02));
    CHECK_THAT (db (at (127, 127).second), WithinAbs (-12.0, 0.3));   // gain_cc7=-12: CC 7 at the top, 12 dB down
    // Moving the wheel while the note is held moves the crossfade (and PR7-O's own MOD WHEEL dynamics leaves the file's be).
    sampler::Sampler::Settings wheelMode;
    wheelMode.dynamics = sampler::Sampler::Dynamics::modWheel;
    juce::MidiBuffer moving;
    moving.addEvent (juce::MidiMessage::controllerEvent (1, 1, 0), 0);
    moving.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1);
    moving.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 24000);
    const auto moved = render (*program, moving, 1.0, wheelMode)[0];
    CHECK (levelOver (moved, 220.0, 0.3, 0.1) > 0.2);
    CHECK (levelOver (moved, 330.0, 0.8, 0.1) > 0.2);
    CHECK (levelOver (moved, 220.0, 0.8, 0.1) < 0.01);
}

TEST_CASE ("SFZ 0.9.0: velocity crossfades, amp_velcurve, pitch_veltrack, amp_keytrack, width, delay", "[sfz][sampler]")
{
    Folder f;
    const auto a = tones ({ 440.0 }, 1.0);
    const auto levelAtVelocity = [&] (const juce::String& opcodes, int velocity)
    {
        const auto program = withSample (f, "<region> sample=t.wav key=60 " + opcodes + "\n", a);
        return levelOver (render (*program, noteOn (60, velocity), 0.4)[0], 440.0, 0.2, 0.1) / 0.25;
    };
    // xfin over velocity 32-96 (power): at 64 half in power (-3 dB), at 96 all, at 20 none (amp_veltrack 0).
    CHECK_THAT (db (levelAtVelocity ("amp_veltrack=0 xfin_lovel=32 xfin_hivel=96", 64)), WithinAbs (-3.0, 0.3));
    CHECK_THAT (levelAtVelocity ("amp_veltrack=0 xfin_lovel=32 xfin_hivel=96", 100), WithinAbs (1.0, 0.02));
    CHECK (levelAtVelocity ("amp_veltrack=0 xfin_lovel=32 xfin_hivel=96", 20) < 1.0e-3);
    CHECK_THAT (levelAtVelocity ("amp_veltrack=0 xfout_lovel=32 xfout_hivel=96 xf_velcurve=gain", 64), WithinAbs (0.5, 0.03));
    // amp_velcurve: its points, straight between them (velocity 64 at 0.5 here, 32 a quarter).
    CHECK_THAT (levelAtVelocity ("amp_velcurve_64=0.5 amp_velcurve_127=1", 32), WithinAbs (0.25, 0.02));
    CHECK_THAT (levelAtVelocity ("amp_velcurve_64=0.5 amp_velcurve_127=1", 64), WithinAbs (0.5, 0.02));
    // amp_keytrack: -1 dB a key from 60 (the note at 66: -6 dB).
    {
        const auto program = withSample (f, "<region> sample=t.wav lokey=48 hikey=84 pitch_keycenter=60 pitch_keytrack=0 amp_veltrack=0 amp_keytrack=-1\n", a);
        CHECK_THAT (db (levelOver (render (*program, noteOn (66), 0.4)[0], 440.0, 0.2, 0.1) / 0.25), WithinAbs (-6.0, 0.3));
    }
    // pitch_veltrack: 1200 cents at velocity 127 (an octave up).
    {
        const auto program = withSample (f, "<region> sample=t.wav key=60 pitch_veltrack=1200\n", a);
        CHECK_THAT (frequencyOf (render (*program, noteOn (60, 127), 0.4)[0], 4800, 14400), WithinAbs (880.0, 3.0));
    }
    // width 0: both sides the same (a sample whose right side is half its left).
    {
        const auto program = withSample (f, "<region> sample=t.wav key=60 amp_veltrack=0 width=0\n", tones ({ 440.0 }, 1.0, 0.5f));
        const auto out = render (*program, noteOn (60), 0.4);
        CHECK_THAT (db (levelOver (out[0], 440.0, 0.2, 0.1) / levelOver (out[1], 440.0, 0.2, 0.1)), WithinAbs (0.0, 0.05));
    }
    // delay=0.1: nothing for 0.1 s, then the recording from its start (sample for sample).
    {
        const auto program = withSample (f, "<region> sample=t.wav key=60 amp_veltrack=0 delay=0.1\n", a);
        const auto out = render (*program, noteOn (60), 0.3)[0];
        CHECK (peakOf (out, 0, 4799) == 0.0);
        CHECK_THAT (out[4800 + 10], WithinAbs (a->channels[0][10], 1.0e-5));
    }
}

TEST_CASE ("SFZ 0.9.0: RELEASE samples after the key (and the pedal), quieter the longer it was held; first and legato samples", "[sfz][sampler][release]")
{
    Folder f;
    const auto body = tones ({ 220.0 }, 3.0), tail = tones ({ 660.0 }, 1.0);
    f.wav ("t.wav", 220.0, 0.1);
    auto program = sampler::sfz::load (f.sfz ("i.sfz",
        "<group> key=60 amp_veltrack=0\n"
        "<region> sample=t.wav ampeg_release=0.05\n"
        "<region> sample=t.wav trigger=release rt_decay=6\n"));
    REQUIRE (program->regions.size() == 2);
    program->regions[0].sample = body;
    program->regions[1].sample = tail;
    program->index();
    using M = juce::MidiMessage;
    const auto out = render (*program, events ({ M::noteOn (1, 60, (juce::uint8) 100), M::noteOff (1, 60) }, { 0.0, 1.0 }), 1.5)[0];
    CHECK (levelOver (out, 660.0, 0.5, 0.1) < 1.0e-3);   // not while held
    CHECK (levelOver (out, 660.0, 1.1, 0.1) > 0.1);      // after
    // rt_decay 6 dB a second: held 2 s, 6 dB quieter than held 1 s.
    const auto later = render (*program, events ({ M::noteOn (1, 60, (juce::uint8) 100), M::noteOff (1, 60) }, { 0.0, 2.0 }), 2.5)[0];
    CHECK_THAT (db (levelOver (later, 660.0, 2.1, 0.1) / levelOver (out, 660.0, 1.1, 0.1)), WithinAbs (-6.0, 0.3));
    // Under the pedal: let go of the key, nothing yet; lift the pedal, the release sample.
    const auto pedal = render (*program, events ({ M::controllerEvent (1, 64, 127), M::noteOn (1, 60, (juce::uint8) 100), M::noteOff (1, 60),
                                                   M::controllerEvent (1, 64, 0) }, { 0.0, 0.01, 0.5, 1.0 }), 1.5)[0];
    CHECK (levelOver (pedal, 660.0, 0.7, 0.1) < 1.0e-3);
    CHECK (levelOver (pedal, 660.0, 1.1, 0.1) > 0.05);
    // release_key: at the key, whatever the pedal.
    program->regions[1].trigger = sampler::Region::Trigger::releaseKey;
    const auto keyOnly = render (*program, events ({ M::controllerEvent (1, 64, 127), M::noteOn (1, 60, (juce::uint8) 100), M::noteOff (1, 60) },
                                                   { 0.0, 0.01, 0.5 }), 1.0)[0];
    CHECK (levelOver (keyOnly, 660.0, 0.6, 0.1) > 0.1);

    // first / legato: the first note of a phrase plays one, a note while another's held the other.
    auto phrase = sampler::sfz::load (f.sfz ("p.sfz",
        "<group> lokey=60 hikey=62 pitch_keycenter=60 pitch_keytrack=0 amp_veltrack=0\n"
        "<region> sample=t.wav trigger=first\n"
        "<region> sample=t.wav trigger=legato\n"));
    REQUIRE (phrase->regions.size() == 2);
    phrase->regions[0].sample = body;
    phrase->regions[1].sample = tail;
    phrase->index();
    const auto played = render (*phrase, events ({ M::noteOn (1, 60, (juce::uint8) 100), M::noteOn (1, 62, (juce::uint8) 100) }, { 0.0, 0.5 }), 1.0)[0];
    CHECK (levelOver (played, 220.0, 0.2, 0.1) > 0.2);   // the first note: first's
    CHECK (levelOver (played, 660.0, 0.2, 0.1) < 1.0e-3);
    CHECK (levelOver (played, 660.0, 0.7, 0.1) > 0.2);   // the second, over it: legato's
}

TEST_CASE ("SFZ 0.9.0: GROUPS turn notes off (off_by, off_mode); locc / hicc; envelope by velocity; the level LFO", "[sfz][sampler]")
{
    Folder f;
    const auto a = tones ({ 220.0 }, 2.0), b = tones ({ 330.0 }, 2.0);
    f.wav ("t.wav", 220.0, 0.1);
    using M = juce::MidiMessage;
    // An open hi-hat cut by a closed one, as drum kits do: here A (group 1, off_by 2) stops when B (group 2) starts.
    auto choke = sampler::sfz::load (f.sfz ("c.sfz",
        "<region> sample=t.wav key=60 amp_veltrack=0 group=1 off_by=2\n"
        "<region> sample=t.wav key=62 amp_veltrack=0 group=2\n"));
    REQUIRE (choke->regions.size() == 2);
    choke->regions[0].sample = a;
    choke->regions[1].sample = b;
    choke->index();
    const auto cut = render (*choke, events ({ M::noteOn (1, 60, (juce::uint8) 100), M::noteOn (1, 62, (juce::uint8) 100) }, { 0.0, 0.5 }), 1.0)[0];
    CHECK (levelOver (cut, 220.0, 0.3, 0.1) > 0.2);
    CHECK (levelOver (cut, 220.0, 0.6, 0.1) < 1.0e-3);   // gone 6 ms after B starts
    CHECK (levelOver (cut, 330.0, 0.6, 0.1) > 0.2);
    // locc / hicc: a region only while CC 20 is 64 or more.
    {
        const auto program = withSample (f, "<region> sample=t.wav key=60 amp_veltrack=0 locc20=64 hicc20=127\n", a);
        CHECK (peakOf (render (*program, noteOn (60), 0.2)[0]) == 0.0);
        juce::MidiBuffer m;
        m.addEvent (M::controllerEvent (1, 20, 100), 0);
        m.addEvent (M::noteOn (1, 60, (juce::uint8) 100), 1);
        CHECK (peakOf (render (*program, m, 0.2)[0]) > 0.1);
    }
    // ampeg_vel2attack: 0.2 s more attack at velocity 127: half way up at 0.1 s (a soft note: at once).
    {
        const auto program = withSample (f, "<region> sample=t.wav key=60 amp_veltrack=0 ampeg_vel2attack=0.2\n", a);
        CHECK_THAT (levelOver (render (*program, noteOn (60, 127), 0.4)[0], 220.0, 0.095, 0.01) / 0.25, WithinAbs (0.5, 0.06));
        CHECK (levelOver (render (*program, noteOn (60, 1), 0.4)[0], 220.0, 0.095, 0.01) / 0.25 > 0.95);
    }
    // amplfo: +-3 dB at 2 Hz: the level swings 6 dB over half a second.
    {
        const auto program = withSample (f, "<region> sample=t.wav key=60 amp_veltrack=0 amplfo_depth=3 amplfo_freq=2\n", a);
        const auto out = render (*program, noteOn (60), 1.6)[0];
        auto lo = 1.0e9, hi = 0.0;
        for (double t = 0.6; t < 1.5; t += 0.02)
        {
            const auto level = db (levelOver (out, 220.0, t, 0.02) / 0.25);
            lo = std::min (lo, level);
            hi = std::max (hi, level);
        }
        CHECK_THAT (hi - lo, WithinAbs (6.0, 0.8));
    }
}

// ================================================================ 0.10.0: THE ENSEMBLE
namespace
{
    std::shared_ptr<sampler::Sample> monoNoise (double seconds)
    {
        auto s = std::make_shared<sampler::Sample>();
        s->name = "noise";
        s->sampleRate = sampleRate;
        s->channels.assign (1, std::vector<float> ((size_t) (seconds * sampleRate)));
        juce::Random random (7);
        for (auto& v : s->channels[0])
            v = 0.5f * (random.nextFloat() - 0.5f);
        return s;
    }

    std::shared_ptr<sampler::Program> keyboardOf (std::shared_ptr<const sampler::Sample> sample, int low = 24, int high = 108)
    {
        auto program = sampler::programFrom (std::move (sample), 60);
        program->regions[0].loKey = low;
        program->regions[0].hiKey = high;
        program->regions[0].velTrack = 0.0f;
        program->index();
        return program;
    }
}

TEST_CASE ("SAMPLER 0.10.0: PLAYERS: as loud as one, the first on time, the others later, out of tune and across", "[sampler][ensemble]")
{
    const auto noise = monoNoise (2.0);
    const auto program = keyboardOf (noise);
    sampler::Sampler::Settings one, three;
    three.players = 3;
    const auto a = render (*program, noteOn (60), 1.5, one), b = render (*program, noteOn (60), 1.5, three);
    // The level holds (each player at 1 / sqrt 3; their sounds unalike), within 1.5 dB.
    CHECK_THAT (db (rms (b[0], 24000, 72000) / rms (a[0], 24000, 72000)), WithinAbs (0.0, 1.5));
    // Spread across: one player, both sides the same; three, they differ.
    const auto correlation = [] (const std::vector<float>& x, const std::vector<float>& y)
    {
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = 24000; i < 72000; ++i) { xy += x[i] * y[i]; xx += x[i] * x[i]; yy += y[i] * y[i]; }
        return xy / std::sqrt (xx * yy);
    };
    CHECK (correlation (a[0], a[1]) > 0.999);
    CHECK (correlation (b[0], b[1]) < 0.95);
    // The first player on time, the others later (at least a quarter of TIMING): the first 5 ms are the recording alone,
    // at its share (three players, the others either side: each side 1 / sqrt (1 + 1 + 0.16)).
    sampler::Sampler::Settings late = three;
    late.timingMs = 20.0f;
    const auto c = render (*program, noteOn (60), 0.1, late)[0];
    for (size_t i = 0; i < 230; ++i)
    {
        INFO (i);
        REQUIRE_THAT (c[i], WithinAbs (noise->channels[0][i] / std::sqrt (2.16f), 1.0e-5));
    }
    // Out of tune: two players of a steady tone, DETUNE 30 cents and no lateness, beat (the level swings); one doesn't.
    const auto toneProgram = keyboardOf (tones ({ 440.0 }, 2.0));
    sampler::Sampler::Settings two;
    two.players = 2;
    two.detuneCents = 30.0f;
    two.timingMs = 0.0f;
    const auto swing = [] (const std::vector<float>& x)
    {
        auto lo = 1.0e9, hi = -1.0e9;
        for (double t = 0.3; t < 1.4; t += 0.02)
        {
            const auto level = db (rms (x, (size_t) (t * sampleRate), (size_t) ((t + 0.02) * sampleRate)));
            lo = std::min (lo, level);
            hi = std::max (hi, level);
        }
        return hi - lo;
    };
    CHECK (swing (render (*toneProgram, noteOn (60), 1.5, two)[0]) > 6.0);
    CHECK (swing (render (*toneProgram, noteOn (60), 1.5, one)[0]) < 0.5);
}

TEST_CASE ("SAMPLER 0.10.0: DOUBLE: the octaves at DOUBLE LEVEL, ended by the key played, counted as one note; a join takes them along", "[sampler][ensemble]")
{
    const auto program = keyboardOf (tones ({ 220.0 }, 3.0));   // key 60 plays 220 Hz
    const auto levels = [&] (int mode, juce::MidiBuffer midi = noteOn (60), double seconds = 0.6)
    {
        sampler::Sampler::Settings settings;
        settings.doubleMode = mode;
        settings.doubleGain = 0.5f;
        return render (*program, midi, seconds, settings)[0];
    };
    const auto up = levels (1), down = levels (2), both = levels (3), none = levels (0);
    CHECK (levelOver (none, 440.0, 0.3, 0.1) < 1.0e-3);
    CHECK_THAT (db (levelOver (up, 440.0, 0.3, 0.1) / levelOver (up, 220.0, 0.3, 0.1)), WithinAbs (-6.0, 0.3));
    CHECK (levelOver (up, 110.0, 0.3, 0.1) < 1.0e-3);
    CHECK_THAT (db (levelOver (down, 110.0, 0.3, 0.1) / levelOver (down, 220.0, 0.3, 0.1)), WithinAbs (-6.0, 0.3));
    CHECK ((levelOver (both, 110.0, 0.3, 0.1) > 0.05 && levelOver (both, 440.0, 0.3, 0.1) > 0.05));
    // Ended by the key played; one note for POLYPHONY.
    sampler::Sampler s;
    s.prepare (sampleRate);
    sampler::Sampler::Settings settings;
    settings.doubleMode = 3;
    std::vector<float> l (24000), r (24000);
    s.render (program.get(), l.data(), r.data(), 24000, noteOn (60), 0, settings);
    CHECK (s.getActiveNotes() == 1);
    auto off = juce::MidiBuffer();
    off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    std::vector<float> l2 (48000), r2 (48000);
    s.render (program.get(), l2.data(), r2.data(), 48000, off, 0, settings);
    CHECK (s.getActiveVoices() == 0);
    // A key the instrument doesn't have isn't doubled (an instrument of keys 55-65: 60 up is 72, not there).
    const auto narrow = keyboardOf (tones ({ 220.0 }, 1.0), 55, 65);
    sampler::Sampler::Settings upOnly;
    upOnly.doubleMode = 1;
    CHECK (levelOver (render (*narrow, noteOn (60), 0.4, upOnly)[0], 440.0, 0.2, 0.1) < 1.0e-3);
}

TEST_CASE ("SAMPLER 0.10.0: LEGATO carries the doubled octave along", "[sampler][ensemble][legato]")
{
    // A held instrument (the legato tests' bowed shape) over the keyboard, 220 Hz at key 60: 60 then 62 joined, an
    // octave up doubled: the old octave (72, 440 Hz) goes with its note, the new one (74, 493.9 Hz) comes.
    auto program = sampler::programFrom (shaped (220.0, 4.0, curve ({ { 0.0, -30.0 }, { 0.4, 0.0 }, { 4.0, 0.0 } })), 60);
    program->regions[0].velTrack = 0.0f;
    program->regions[0].loKey = 36;
    program->regions[0].hiKey = 96;
    sampler::prepareRegions (*program);
    program->index();
    REQUIRE (program->regions[0].held);
    sampler::Sampler::Settings settings;
    settings.legato = true;
    settings.doubleMode = 1;
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    m.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), 48000);
    const auto out = render (*program, m, 1.6, settings)[0];
    CHECK (levelOver (out, 440.0, 0.8, 0.1) > 0.05);
    // (gone: the 440 Hz reading no more than the new note's own leak into it, 62 and its octave played alone)
    juce::MidiBuffer alone;
    alone.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), 0);
    const auto baseline = render (*program, alone, 1.0, settings)[0];
    CHECK (db (levelOver (out, 440.0, 1.3, 0.1)) < db (levelOver (baseline, 440.0, 0.7, 0.1)) + 1.0);
    CHECK (levelOver (out, 493.88, 1.3, 0.1) > 0.05);
}

//==============================================================================
// 0.11.0: DISK STREAMING. A long recording keeps a few stretches in memory and the rest is read from its file as it
// plays: what comes out must be what the whole recording in memory gives, sample for sample.
namespace
{
    // 6 s of a sound no two stretches of which are alike (gliding tones, a different one each side), 32-bit float.
    juce::File unlikeTones (Folder& f, const juce::String& name, double seconds = 6.0, int channels = 2)
    {
        const auto file = f.dir.getChildFile (name);
        juce::AudioBuffer<float> buffer (channels, (int) (seconds * sampleRate));
        for (int c = 0; c < channels; ++c)
        {
            double phase = 0.0, phase2 = 0.0;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto t = (double) i / sampleRate;
                phase += juce::MathConstants<double>::twoPi * (180.0 + 70.0 * t + 40.0 * c) / sampleRate;
                phase2 += juce::MathConstants<double>::twoPi * (1300.0 - 90.0 * t) / sampleRate;
                buffer.setSample (c, i, (float) (0.3 * std::sin (phase) + 0.1 * std::sin (phase2) * (c == 0 ? 1.0 : -1.0)));
            }
        }
        testkit::writeWav (file, buffer, sampleRate);
        return file;
    }

    // Plays through `s` (prepared by the caller) for `seconds`; `beforeEachBlock` runs first each block.
    std::array<std::vector<float>, 2> renderWith (sampler::Sampler& s, const sampler::Program& program, const juce::MidiBuffer& midi,
                                                  double seconds, const sampler::Sampler::Settings& settings,
                                                  const std::function<void()>& beforeEachBlock = {})
    {
        std::array<std::vector<float>, 2> out;
        const auto total = (int) (seconds * sampleRate);
        out[0].assign ((size_t) total, 0.0f);
        out[1].assign ((size_t) total, 0.0f);
        for (int start = 0; start < total; start += blockSize)
        {
            if (beforeEachBlock)
                beforeEachBlock();
            const auto n = juce::jmin (blockSize, total - start);
            s.render (&program, out[0].data() + start, out[1].data() + start, n, midi, start, settings);
        }
        return out;
    }

    float largestDifference (const std::array<std::vector<float>, 2>& a, const std::array<std::vector<float>, 2>& b)
    {
        auto most = 0.0f;
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < a[c].size(); ++i)
                most = std::max (most, std::abs (a[c][i] - b[c][i]));
        return most;
    }
}

TEST_CASE ("Disk streaming: the stream follows the voice's timeline round its loop and out of it", "[streaming]")
{
    Folder f;
    const auto file = unlikeTones (f, "long.wav");
    const auto whole = sampler::sampleFiles::load (file);
    REQUIRE (whole != nullptr);
    sampler::StreamSource source;
    source.file = file;
    source.length = whole->length();
    source.numChannels = 2;
    source.resident = { { 0, 1000 }, { 49996, 50004 } };
    sampler::StreamPlan plan;
    plan.start = 0;
    plan.looping = true;
    plan.loopStart = 50000;
    plan.loopEnd = 90000;
    plan.crossfade = 5000;
    sampler::Streamer streamer;
    const auto stream = streamer.claim (source, plan);
    REQUIRE (stream >= 0);
    // Each timeline frame: the file's frame the plan says (back: a loop's length earlier, in the crossfade).
    const auto check = [&] (juce::int64 t, juce::int64 exitAt)
    {
        for (int k = 0; k < 2; ++k)
        {
            const float* l = nullptr;
            const float* r = nullptr;
            int n = 0;
            for (auto last = false; (n = streamer.span (stream, t, k == 1, l, r)) == 0 && ! last;)
                last = ! streamer.fillNow (stream);
            REQUIRE (n > 0);
            const auto frame = sampler::Streamer::fileFrame (plan, t, exitAt) - (k == 1 ? plan.loopEnd - plan.loopStart : 0);
            CHECK (l[0] == whole->channels[0][(size_t) frame]);
            CHECK (r[0] == whole->channels[1][(size_t) frame]);
            if (k == 0 && ! (t < exitAt && frame >= plan.loopEnd - plan.crossfade && frame < plan.loopEnd))
                break;   // (back reads only in the crossfade)
        }
    };
    // Up to the loop's end, then round twice (into its crossfade each time), then out past its end.
    for (juce::int64 t = 1000; t < 90000 + 2 * 40000; t += 777)
    {
        if (sampler::Streamer::fileFrame (plan, t, sampler::Streamer::never) >= plan.loopStart - 4
            && sampler::Streamer::fileFrame (plan, t, sampler::Streamer::never) < plan.loopStart + 4)
            continue;
        check (t, sampler::Streamer::never);
        streamer.consumed (stream, t - 2);
    }
    // Let go in the third time round: from that time's end, the file past the loop's end.
    const auto exitAt = (juce::int64) 90000 + 3 * 40000;
    streamer.stopLooping (stream, exitAt);
    for (juce::int64 t = 90000 + 2 * 40000 + 100; t < exitAt + 100000; t += 613)
    {
        check (t, exitAt);
        streamer.consumed (stream, t - 2);
    }
    CHECK (sampler::Streamer::fileFrame (plan, exitAt + 10, exitAt) == 90010);
    streamer.release (stream);
}

TEST_CASE ("Disk streaming: a streamed recording plays as the whole one does, sample for sample", "[streaming]")
{
    Folder f;
    unlikeTones (f, "long.wav");
    const auto sfzFile = f.sfz ("long.sfz", "<region> sample=long.wav pitch_keycenter=60 amp_veltrack=0 ampeg_release=0.4\n"
                                            "loop_mode=loop_sustain loop_start=96000 loop_end=180000 loop_crossfade=0.3\n");
    const auto whole = sampler::sfz::load (sfzFile);
    auto streamed = sampler::sfz::load (sfzFile);
    sampler::streamSamples (*streamed);
    REQUIRE (whole->regions.size() == 1);
    REQUIRE (streamed->regions[0].sample->streamed());
    // In memory: the start, the tail after the loop (0.5 s each), a few frames round the loop's start.
    INFO ("in memory " << streamed->bytes << " of " << streamed->wholeBytes);
    CHECK (streamed->wholeBytes == whole->bytes);
    CHECK ((double) streamed->bytes < 0.2 * (double) streamed->wholeBytes);
    {
        // A short one (1.5 s) stays whole: little to save.
        const auto shortFile = f.sfz ("short.sfz", "<region> sample=short.wav\n");
        unlikeTones (f, "short.wav", 1.5);
        auto shortProgram = sampler::sfz::load (shortFile);
        sampler::streamSamples (*shortProgram);
        REQUIRE (shortProgram->regions.size() == 1);
        CHECK (! shortProgram->regions[0].sample->streamed());
    }

    // Two notes: one at its root, one a tone up (between frames), held through several times round the loop; let go
    // inside the loop (the stream told to go on past it) and inside its crossfade (once more round first).
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 90), 3000);
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), (int) (5.3 * sampleRate));
    midi.addEvent (juce::MidiMessage::noteOff (1, 62), (int) (6.95 * sampleRate));
    sampler::Sampler::Settings settings;
    sampler::Sampler plain;
    plain.prepare (sampleRate);
    const auto expected = renderWith (plain, *whole, midi, 9.0, settings);
    CHECK (peakOf (expected[0], (size_t) (7.0 * sampleRate), (size_t) (7.3 * sampleRate)) > 0.01);   // (sounding past the loop's end)

    SECTION ("bouncing: what the disk hasn't read yet is read there and then")
    {
        sampler::Streamer streamer;
        sampler::Sampler s;
        s.prepare (sampleRate);
        s.setStreamer (&streamer);
        settings.waitForDisk = true;
        const auto got = renderWith (s, *streamed, midi, 9.0, settings);
        CHECK (largestDifference (expected, got) < 1.0e-6f);
        CHECK (streamer.getUnderruns() == 0);
    }
    SECTION ("live, with the disk keeping up")
    {
        sampler::Streamer streamer;
        sampler::Sampler s;
        s.prepare (sampleRate);
        s.setStreamer (&streamer);
        const auto got = renderWith (s, *streamed, midi, 9.0, settings, [&] { CHECK (streamer.waitUntilFilled (2000)); });
        CHECK (largestDifference (expected, got) < 1.0e-6f);
        CHECK (streamer.getUnderruns() == 0);
    }
    SECTION ("with no streamer, only what's in memory plays (and nothing breaks)")
    {
        sampler::Sampler s;
        s.prepare (sampleRate);
        const auto got = renderWith (s, *streamed, midi, 9.0, settings);
        for (int i = 0; i < 20000; ++i)   // (the start is in memory)
            CHECK (got[0][(size_t) i] == expected[0][(size_t) i]);
        // Past the start, nothing but the few frames kept round the loop's start (each note goes by them once).
        auto sounding = 0;
        for (size_t i = (size_t) (0.6 * sampleRate); i < (size_t) (3.5 * sampleRate); ++i)
            sounding += std::abs (got[0][i]) > 1.0e-6f ? 1 : 0;
        CHECK (sounding > 0);
        CHECK (sounding < 30);
    }
}

TEST_CASE ("Disk streaming: legato joins, AUTO SUSTAIN and the ensemble stream as they play from memory", "[streaming]")
{
    // A held recording (steady after its attack, a bow-off at its end): AUTO SUSTAIN loops it, a join starts it past
    // its attack, players start a little into it: all places the stream must start from.
    Folder f;
    {
        auto bowedNote = bowed (220.0, 9.0, 0.4, 0.8);
        bowedNote->channels.push_back (bowedNote->channels[0]);
        for (size_t i = 0; i < bowedNote->channels[1].size(); ++i)   // (the sides differ: noise on the right)
            bowedNote->channels[1][i] += 0.02f * (float) std::sin ((double) i * 0.731 + std::sin ((double) i * 0.0017) * 9.0);
        testkit::writeWav (f.dir.getChildFile ("held.wav"), *bowedNote);
    }
    const auto sfzFile = f.sfz ("held.sfz", "<region> sample=held.wav lokey=50 hikey=70 pitch_keycenter=57 amp_veltrack=0 ampeg_release=0.3\n");
    const auto whole = sampler::sfz::load (sfzFile);
    auto streamed = sampler::sfz::load (sfzFile);
    sampler::streamSamples (*streamed);
    REQUIRE (whole->regions.size() == 1);
    REQUIRE (whole->regions[0].autoLoop);
    REQUIRE (whole->regions[0].legatoStart > 0);
    REQUIRE (streamed->regions[0].sample->streamed());
    CHECK (streamed->regions[0].loopStart == whole->regions[0].loopStart);
    CHECK ((double) streamed->bytes < 0.35 * (double) streamed->wholeBytes);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 59, (juce::uint8) 100), (int) (1.0 * sampleRate));   // joins 57
    midi.addEvent (juce::MidiMessage::noteOff (1, 57), (int) (1.1 * sampleRate));
    midi.addEvent (juce::MidiMessage::noteOff (1, 59), (int) (11.0 * sampleRate));
    sampler::Sampler::Settings settings;
    settings.legato = true;
    settings.players = 3;
    settings.doubleMode = 1;
    sampler::Sampler plain;
    plain.prepare (sampleRate);
    plain.seedRandom (7);
    const auto expected = renderWith (plain, *whole, midi, 12.5, settings);
    sampler::Streamer streamer;
    sampler::Sampler s;
    s.prepare (sampleRate);
    s.seedRandom (7);
    s.setStreamer (&streamer);
    settings.waitForDisk = true;
    const auto got = renderWith (s, *streamed, midi, 12.5, settings);
    CHECK (largestDifference (expected, got) < 1.0e-6f);
    CHECK (streamer.getUnderruns() == 0);
    CHECK (peakOf (expected[0], (size_t) (11.0 * sampleRate) - 2000, (size_t) (11.0 * sampleRate)) > 0.05);   // (looped)
}
