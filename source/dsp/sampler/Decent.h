#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

// DECENT SAMPLER INSTRUMENTS (PR7-O 0.18.0; the owner: "can we load decent sampler instruments in our pr7-0?", then "Go
// with DS Orchestra Complete"). A .dspreset is XML: <groups> (envelope, velocity tracking, volume...) holding <group>s
// holding <sample>s (path, root / lo / hi note, lo / hi velocity, start / end, loop points, volume, tuning, pan,
// trigger, round robins, tags), plus its own screen (<ui>), effects, <tags> and <midi>, tied together by BINDINGS: a
// knob, a controller or the note's velocity sets an envelope time, the instrument's volume, a tag's volume...
//
// PR7-O reads the sound and turns each <sample> into the same region an SFZ file's <region> becomes (the opcode names
// below), so everything else (sections, the stage, the mixer, the ensemble, legato, streaming) plays it as it plays SFZ.
// What it does with the rest:
//   - the screen's knobs, menus and the tags' starting values: read once, as the instrument opens (their bindings set
//     the envelope, velocity tracking, volumes);
//   - a controller bound to the instrument's or a tag's volume (directly, or through one of the screen's knobs, the way
//     DS Orchestra Complete's mod wheel moves its dynamics): followed as it moves, as a level curve on each region
//     ("pr7o_cccurve<cc>": 128 gains, one for each controller value);
//   - the note's velocity bound to the attack time: "pr7o_velattack" (velocity 0-1, seconds);
//   - its own effects (reverb, delay, chorus, a filter): left out; PR7-O's STAGE and FX do that work.
namespace sampler::decent
{
    using Opcodes = std::map<juce::String, juce::String>;

    // The regions of a .dspreset as SFZ opcodes ("sample" a path from the preset's folder); problems added to
    // `problems`, and what it uses that PR7-O doesn't play to `unread` (with how many places use each).
    std::vector<Opcodes> read (const juce::File& preset, juce::StringArray& problems, std::map<juce::String, int>& unread);

    bool isPreset (const juce::File&);   // a .dspreset
}
