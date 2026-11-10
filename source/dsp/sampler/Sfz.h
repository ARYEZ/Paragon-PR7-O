#pragma once

#include "dsp/sampler/Sample.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>

// AN SFZ INSTRUMENT (PR7-O 0.1.0). SFZ is an open, plain-text format: a list of REGIONS, each a sample file and when it
// plays (which keys, which velocities, which round robin) and how (its root key, tuning, level, pan, envelope). Most
// open orchestral libraries (VSCO 2, Sonatina, Virtual Playing Orchestra) come as SFZ files beside their WAVs.
//
// Read (the text side is PR7-D's reader: comments, #define, #include (8 deep), <global> <master> <group> <region> each
// passing its settings on; 0.2.0: each <control> header's default_path, note_offset and octave_offset hold for the
// regions after it, until the next <control>: VSCO 2 CE's keyswitch files start every articulation with its own):
//   sample, key / lokey / hikey (numbers or names: c4 = 60), pitch_keycenter, lovel / hivel, lochan / hichan,
//   seq_length / seq_position (round robins), lorand / hirand (random choice), tune (pitch), transpose, pitch_keytrack,
//   volume, amplitude, pan, amp_veltrack, offset, end, ampeg_delay / attack / hold / decay / sustain / release,
//   bend_up / bend_down, trigger (attack only).
//   0.2.0: KEYSWITCHES (sw_lokey / sw_hikey: the keys that switch, and don't sound; sw_last, or sw_lolast / sw_hilast:
//   the switch a region plays under; sw_default: the one at the start; sw_label: its name); LOOPS (loop_mode no_loop /
//   one_shot / loop_continuous / loop_sustain, loop_start / loop_end in frames, loop_crossfade in seconds: crossfaded
//   with a curve that keeps the level for how alike its two sides are, measured as it loads); ampeg_dynamic
//   (it re-reads the envelope when a controller moves it: PR7-O's envelope has no controllers, so it changes nothing).
//   0.9.0 (Sonatina Symphonic Orchestra's files, counted): trigger release / release_key / first / legato, rt_decay;
//   locc / hicc; fil_type / cutoff / resonance / fil_keytrack / fil_keycenter / fil_veltrack / cutoff_ccN (and fil2_,
//   cutoff2, resonance2, cutoff2_ccN); gain_ccN, amplitude_onccN; xfin_ / xfout_ loccN hiccN, lovel hivel, lokey hikey,
//   xf_cccurve / xf_velcurve / xf_keycurve; amp_velcurve_N; ampeg_vel2*, ampeg_*ccN; amp_keytrack / amp_keycenter;
//   pitch_veltrack; delay / delay_random / offset_random; group_ / master_ / global_ volume and tune; width; group /
//   off_by / off_mode / off_time; amplfo_ depth / freq / delay / fade; set_ccN, label_ccN, sustain_cc.
// Not read yet, and listed as such (`unread`): the newer numbered LFOs and envelopes (lfo01_..., eg01_...), equalisers,
// other curves.
namespace sampler
{
    struct Region
    {
        std::shared_ptr<const Sample> sample;
        int loKey = 0, hiKey = 127, keyCenter = 60;
        int loVel = 1, hiVel = 127;
        int loChan = 1, hiChan = 16;
        float loRand = 0.0f, hiRand = 1.0f;
        int seqLength = 1, seqPosition = 1;
        float keytrack = 100.0f;      // cents per key
        float tuneCents = 0.0f;       // tune + transpose x 100
        float gain = 1.0f;            // volume and amplitude
        float pan = 0.0f;             // -1 to 1
        float velTrack = 1.0f;        // amp_veltrack, 0 to 1
        juce::int64 offset = 0, end = -1;   // frames (end -1: the file's)
        float delay = 0.0f, attack = 0.0f, hold = 0.0f, decay = 0.0f, sustain = 1.0f, release = 0.001f;   // seconds; sustain 0-1
        bool oneShot = false;
        float bendUp = 200.0f, bendDown = -200.0f;   // cents at the wheel's ends
        // 0.2.0, KEYSWITCHES: the switches it plays under (-1: any).
        int swLoLast = -1, swHiLast = -1;
        // 0.2.0, LOOPS: the file's (loop_mode, in frames; crossfade in frames), or AUTO SUSTAIN's (our own, for a held
        // recording without loop points: `autoLoop`, found when it loads; only while AUTO SUSTAIN is on).
        enum class Loop { none, continuous, sustain };
        Loop loop = Loop::none;
        juce::int64 loopStart = 0, loopEnd = 0;
        int loopCrossfade = 0;
        float loopLikeness = 0.0f;    // how alike the two sides of the crossfade are (-1 to 1): sets its curve
        bool autoLoop = false;
        // 0.3.0, AUTO SUSTAIN's TILT (dB): how much quieter the loop's end is than the sound before its start (a roll or
        // a note still settling); the sampler turns the loop up across its length by this, so each time round it
        // arrives at the level it goes back to. 0 for the file's own loops.
        float loopTilt = 0.0f;
        // 0.7.0, LEGATO: where a held recording's attack is over (frames: the first 50 ms window at its steady level,
        // within 3 dB of its body), so a note joined to the one before starts there; -1: not held (never joined).
        juce::int64 legatoStart = -1;
        // A HELD recording: one that sustains (no one_shot, longer than 2.5 s, and shaped like a bowed or blown note:
        // isSustained), as against a short (spiccato, pizzicato). MOD WHEEL dynamics and AUTO SUSTAIN work on these only.
        bool held = false;
        // 0.8.0, THE STAGE: which part of a section it plays in (0 for a single instrument), so each part can sit apart.
        int part = 0;

        // 0.9.0, the rest of SFZ's sound that open libraries use (Sonatina Symphonic Orchestra's files, counted):
        // WHEN it plays: on the key going down (attack), only as the first note of a phrase or only joined to one held
        // (first / legato), or when the key is let go (release: after the pedal too; release_key: the key alone), its
        // level falling rt_decay dB for every second the key was held.
        enum class Trigger { attack, release, releaseKey, first, legato };
        Trigger trigger = Trigger::attack;
        float rtDecay = 0.0f;
        // Only while a controller is in a range (locc / hicc): (cc, lo, hi).
        struct CcRange { int cc = 0, lo = 0, hi = 127; };
        std::vector<CcRange> ccConditions;
        // Its FILTERS (two: fil_type / cutoff / resonance and fil2_type / cutoff2 / resonance2): the cutoff moved by the
        // key (fil_keytrack cents a key from fil_keycenter), the velocity (fil_veltrack cents at 127) and controllers
        // (cutoff_ccN cents at 127, followed as they move).
        struct Filter
        {
            enum class Type { none, lowpass1, lowpass2, highpass1, highpass2, bandpass2, notch2 };
            Type type = Type::none;
            float cutoff = 0.0f, resonance = 0.0f, keytrack = 0.0f, veltrack = 0.0f;   // Hz, dB, cents a key, cents
            int keycenter = 60;
            std::vector<std::pair<int, float>> cc;                                       // (cc, cents at 127)
        };
        std::array<Filter, 2> filters;
        // LEVEL from controllers (gain_ccN: dB at 127; amplitude_onccN: % at 127), and CROSSFADES: on controllers
        // (xfin_loccN / hiccN: fading in over that range; xfout_: out), on velocity and on keys, each "power" (equal
        // power, SFZ's default) or "gain" (straight); amp_velcurve_N: its own velocity curve (points, 0-1).
        std::vector<std::pair<int, float>> gainCC, amplitudeCC;
        struct CcFade { int cc = 1, lo = 0, hi = 127; bool in = true; };
        std::vector<CcFade> ccFades;
        bool ccFadePower = true, velFadePower = true, keyFadePower = true;
        int xfinLoVel = -1, xfinHiVel = -1, xfoutLoVel = -1, xfoutHiVel = -1;
        int xfinLoKey = -1, xfinHiKey = -1, xfoutLoKey = -1, xfoutHiKey = -1;
        std::vector<std::pair<int, float>> velCurve;
        // The ENVELOPE moved by velocity (ampeg_vel2attack... seconds at 127; vel2sustain %) and by controllers at the
        // note's start (ampeg_attackccN... seconds at 127; sustainccN %): [delay, attack, hold, decay, sustain, release].
        std::array<float, 6> velToEnvelope {};
        std::array<std::vector<std::pair<int, float>>, 6> ccToEnvelope;
        // Level by key (amp_keytrack dB a key from amp_keycenter); pitch by velocity (pitch_veltrack cents at 127).
        float ampKeytrack = 0.0f, pitchVeltrack = 0.0f;
        int ampKeycenter = 60;
        // A START held back (delay seconds, plus up to delay_random), a random later start (offset_random frames).
        float startDelay = 0.0f, startDelayRandom = 0.0f;
        juce::int64 offsetRandom = 0;
        // Stereo WIDTH (-1 to 1: 1 as recorded, 0 mono, -1 sides swapped).
        float width = 1.0f;
        // GROUPS: a note in group `offBy` stops this one (off_mode fast: 6 ms; normal: its release; time: off_time).
        int group = 0, offBy = 0;
        enum class OffMode { fast, normal, time };
        OffMode offMode = OffMode::fast;
        float offTime = 0.006f;
        // An LFO on its level (amplfo_depth dB, _freq Hz, starting after _delay, fading in over _fade seconds).
        float lfoDepth = 0.0f, lfoFreq = 0.0f, lfoDelay = 0.0f, lfoFade = 0.0f;
        bool usesController (int cc) const noexcept;   // its level, crossfades or filters follow controller `cc`

        bool playsUnder (int keyswitch) const noexcept { return swLoLast < 0 || (keyswitch >= swLoLast && keyswitch <= swHiLast); }

        bool covers (int key, int velocity, int channel) const noexcept
        {
            return key >= loKey && key <= hiKey && velocity >= loVel && velocity <= hiVel && channel >= loChan && channel <= hiChan;
        }
        juce::int64 lastFrame() const noexcept;   // where it stops: `end`, else the sample's last frame
    };

    struct Program
    {
        juce::String name;
        juce::File source;
        std::vector<Region> regions;
        std::array<std::vector<int>, 128> byKey;   // the regions each key can play, in file order
        juce::StringArray problems;   // files missing or unreadable, regions skipped
        juce::StringArray unread;     // opcodes read nowhere yet, with how many regions use each ("loop_mode=loop_continuous (12)")
        size_t bytes = 0;             // audio held in memory
        size_t wholeBytes = 0;        // 0.11.0: what it would be whole (set by streamSamples; 0: as bytes)
        int numFiles = 0;
        // 0.2.0, KEYSWITCHES: the keys that switch (-1: none), the one it starts on, and each switch's name (in the order
        // the file gives them; one entry per sw_last range).
        int swLoKey = -1, swHiKey = -1, swDefault = -1;
        struct Articulation { int lo = -1, hi = -1; juce::String label; };
        std::vector<Articulation> articulations;
        bool isKeyswitch (int key) const noexcept { return swLoKey >= 0 && key >= swLoKey && key <= swHiKey; }
        int articulationOf (int keyswitch) const noexcept;   // its index in `articulations` (-1: none)
        // 0.9.0: the controllers' values at the start (set_ccN, -1: not set), the sustain pedal's controller (sustain_cc),
        // their names (label_ccN), and whether its own regions follow the mod wheel (then MOD WHEEL dynamics, PR7-O's own,
        // leaves them be).
        std::array<int, 128> initialCC;
        int sustainCC = 64;
        std::map<int, juce::String> ccLabels;
        bool ownWheel = false;
        Program() { initialCC.fill (-1); }
        // The audio thread's round-robin counters, one per region (only the sampler playing this program writes them).
        mutable std::vector<int> sequence;

        bool plays (int key) const noexcept { return key >= 0 && key < 128 && ! byKey[(size_t) key].empty(); }
        int lowestKey() const noexcept;    // -1: none
        int highestKey() const noexcept;
        // At a key (0.2.0: under keyswitch `keyswitch`; anyKeyswitch: every region): its different velocity ranges, and
        // the most regions sharing one.
        static constexpr int anyKeyswitch = -2;
        int velocityLayersAt (int key, int keyswitch = anyKeyswitch) const;
        int roundRobinsAt (int key, int keyswitch = anyKeyswitch) const;
        void index();                           // byKey and sequence, from regions
    };

    namespace sfz
    {
        // 0.4.0: what to load. Only the regions playing keys loKey to hiKey, cut to them (a section's part: the rest's
        // samples aren't read); `audio` false: no samples at all (the regions' keys and keyswitches, to plan a
        // section); `reuse`: a sample already in memory for a file's full path (null: read it).
        struct LoadOptions
        {
            int loKey = 0, hiKey = 127;
            bool audio = true;
            std::function<std::shared_ptr<const Sample> (const juce::String&)> reuse;
        };
        // The whole file: its regions with their samples loaded (each file once). `progress` (0 to 1, by files) may
        // return false to stop (the program then has what was loaded). Never null; check `regions`.
        std::shared_ptr<Program> load (const juce::File& file, const std::function<bool (double)>& progress = {},
                                       const LoadOptions& options = {});
        // A note's number from SFZ's text: a number or a name (c4 = 60, c#4, db4).
        int noteNumber (const juce::String& text);
    }

    // 0.11.0, DISK STREAMING: each long recording made streamed (DiskStreamer.h), keeping in memory only what its
    // regions need kept (keptInMemory: `headSeconds` from each place a voice can start without warning). Call after
    // the program's regions are prepared; recordings already streamed are left as they are.
    inline constexpr double streamHeadSeconds = 0.5;
    std::vector<std::pair<juce::int64, juce::int64>> keptInMemory (const Region&, double headSeconds = streamHeadSeconds);
    void streamSamples (Program&, double headSeconds = streamHeadSeconds);

    // An instrument of one sample over the whole keyboard (its root key `keyCenter`). Tests, and a single WAV loaded.
    std::shared_ptr<Program> programFrom (std::shared_ptr<const Sample> sample, int keyCenter = 60);

    // AUTO SUSTAIN (0.2.0, our own design): where a held recording's steady part lies, to loop it with a long crossfade.
    // Its level in 50 ms windows; its core (0.3.0: from the first window to the last within 6 dB of the median of its
    // middle three fifths) and the "body", the core's median; steady from the first window within 3 dB of the body (plus
    // 0.3 s) to the last (less 0.1 s): the bow-off or breath-off after it is left out. The loop: the last stretch of
    // that, up to 4 s (80% of it), crossfaded over up to 1 s (40% of the stretch). Its start then moved (by up to 25 ms,
    // a cycle of the lowest string) to where the sound around the crossfade's middle best matches the sound a loop's
    // length later, so the two sides don't cancel; `likeness` how well (their correlation there). None (found false)
    // when the steady part is under `minimumSteady` (0.3.0: 1 s, for the 3-5 s brass notes; 0.2.0's 1.5 s) or the file
    // is silent.
    inline constexpr double minimumSteady = 1.0;
    // 0.3.0: its tilt (dB, within 6): the level over the crossfade's length before the start less that before the end.
    inline constexpr float maximumTilt = 6.0f;
    struct SustainPoints { bool found = false; juce::int64 start = 0, end = 0; int crossfade = 0; float likeness = 0.0f, tilt = 0.0f; };
    // Whether a recording is shaped like a held note (our own rule; 0.3.0 measured on all of VSCO 2 CE: of its 1914
    // distinct recordings outside the percussion kit, judged by their file's name (sustain, vibrato, tremolo, roll,
    // organ: held; staccato, spiccato, pizzicato, harp, mallets, piano: not), it misjudges 7 (0.2.0's rule: 41)): on its
    // core, the loudest moment no more than 18 dB over the median (an organ pipe's chiff, a brass accent), and the
    // core's first quarter no more than 9 dB louder than its last (a plucked or struck note peaks at its start and keeps
    // falling); the core at least 0.8 s.
    bool isSustained (const Sample&);
    // 0.7.0: where a recording reaches its steady level (frames; 0 if it never settles), our own rule for LEGATO.
    juce::int64 settledFrame (const Sample&);
    SustainPoints findSustain (const Sample&);
    // Sets each region's `held` and AUTO SUSTAIN points (regions without loops of their own). Called by sfz::load and
    // programFrom; tests that build regions by hand call it too.
    void prepareRegions (Program&);
    // How alike a loop's two sides are over its crossfade (their correlation, -1 to 1), for its curve.
    float loopLikeness (const Sample&, juce::int64 loopStart, juce::int64 loopEnd, int crossfade);
}
