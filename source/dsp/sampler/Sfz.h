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
// Read in 0.1.0 (the text side is PR7-D's reader: comments, #define, #include (8 deep), <control> default_path,
// note_offset / octave_offset, <global> <master> <group> <region> each passing its settings on):
//   sample, key / lokey / hikey (numbers or names: c4 = 60), pitch_keycenter, lovel / hivel, lochan / hichan,
//   seq_length / seq_position (round robins), lorand / hirand (random choice), tune (pitch), transpose, pitch_keytrack,
//   volume, amplitude, pan, amp_veltrack, offset, end, ampeg_delay / attack / hold / decay / sustain / release,
//   loop_mode (one_shot: the note plays to its end; the others: see below), bend_up / bend_down, trigger (attack only).
// Not read yet, and listed as such (`unread`): loops (loop_start / loop_end: held notes stop at the sample's end), release
// samples, crossfades on the mod wheel (plan stage 2), keyswitches (stage 4), filters, LFOs, other envelopes, curves.
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
        int numFiles = 0;
        // The audio thread's round-robin counters, one per region (only the sampler playing this program writes them).
        mutable std::vector<int> sequence;

        bool plays (int key) const noexcept { return key >= 0 && key < 128 && ! byKey[(size_t) key].empty(); }
        int lowestKey() const noexcept;    // -1: none
        int highestKey() const noexcept;
        int velocityLayersAt (int key) const;   // different velocity ranges at a key
        int roundRobinsAt (int key) const;      // most regions sharing one velocity range at a key
        void index();                           // byKey and sequence, from regions
    };

    namespace sfz
    {
        // The whole file: its regions with their samples loaded (each file once). `progress` (0 to 1, by files) may
        // return false to stop (the program then has what was loaded). Never null; check `regions`.
        std::shared_ptr<Program> load (const juce::File& file, const std::function<bool (double)>& progress = {});
        // A note's number from SFZ's text: a number or a name (c4 = 60, c#4, db4).
        int noteNumber (const juce::String& text);
    }

    // An instrument of one sample over the whole keyboard (its root key `keyCenter`). Tests, and a single WAV loaded.
    std::shared_ptr<Program> programFrom (std::shared_ptr<const Sample> sample, int keyCenter = 60);
}
