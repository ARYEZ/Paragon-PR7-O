#pragma once

#include "dsp/sampler/Sfz.h"

#include <optional>

// A SECTION (PR7-O 0.4.0, our own design; the owner: "Split, let's build the all strings section"): several SFZ
// instruments on one keyboard, each playing its own keys (a split: VSCO 2 CE's basses at the bottom, then cellos,
// violas, violins), as one instrument with one set of keyswitches.
//
//   - Kept as text where an instrument's file name would go (presets, projects, undo all keep it as they keep a
//     path): <Section><Part file="..." low="24" high="35"/>...</Section>.
//   - Its default split points: the parts ordered by the middle of their ranges, each playing from its own lowest key
//     to the key under the next part's start; the top one to its highest. 0.6.0: where a part's lowest key would leave
//     the part beneath fewer than 7 keys (or is under its start: a horn's under a trombone's), the two split their
//     shared range at its middle instead.
//   - Each part loads only the regions on its keys (cut to them), so the samples elsewhere aren't read. `reuse` gives
//     samples already in memory (a split moved: only what's newly needed is read).
//   - ARTICULATIONS matched by name across the parts (our own rule, from their labels: sustain, sustain non-vibrato,
//     tremolo, spiccato, staccato, pizzicato, else the label itself): one switch each, on keys of their own just above
//     the section's highest note (the first C over it; under its lowest if they'd pass C8), since every part's own
//     switch keys sit inside another part's range. A part without an articulation plays its nearest (a sustain for a
//     sustain); none: it's silent under that switch. A part with no keyswitches plays under every switch. Starts on
//     Sustain where there is one.
namespace sampler::section
{
    struct Part
    {
        juce::String file;   // its SFZ's full path
        int low = 0, high = 127;
        bool operator== (const Part& o) const { return file == o.file && low == o.low && high == o.high; }
    };

    struct Section
    {
        std::vector<Part> parts;   // low to high
        juce::String toText() const;
        bool operator== (const Section& o) const { return parts == o.parts; }
    };

    // Whether an instrument's text is a section (rather than a file's path), and reading one (nothing if it isn't).
    bool isSection (const juce::String& text);
    std::optional<Section> fromText (const juce::String& text);

    // A section of these SFZ files with its default split points (files that play nothing are left out).
    Section fromFiles (const juce::StringArray& files);

    // A part's name as shown: its file's name. An instrument's (a file's path or a section's text): its file's name,
    // or its parts' joined with " + ". Where its files are: the file's folder, or the first part's.
    juce::String nameOf (const Part&);
    // 0.14.0: a part's name as the page shows it: a keyswitch file's "KS" ending left off ("ViolinEns-KS" -> "ViolinEns").
    // Only for showing: seats and the rest go by nameOf.
    juce::String labelOf (const juce::String& name);
    juce::String describe (const juce::String& instrument);
    juce::File folderOf (const juce::String& instrument);

    // The section made: one program (name: its parts' names joined; problems and unread with their part's name).
    // `progress` 0 to 1 over all the parts; false stops.
    std::shared_ptr<Program> load (const Section&, const std::function<bool (double)>& progress = {},
                                   const std::function<std::shared_ptr<const Sample> (const juce::String&)>& reuse = {});

    // An articulation's kind from its label ("C2 Sustain Vibrato" -> "sustain"), our own rule; and its name as the
    // section shows it.
    juce::String kindOf (const juce::String& label);
    juce::String nameOfKind (const juce::String& kind, const juce::String& label);
}
