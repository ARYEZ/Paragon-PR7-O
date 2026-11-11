#include "FactoryRigs.h"

#include "Parameters.h"
#include "dsp/sampler/Section.h"
#include "library/Downloads.h"
#include "rigs/Expansions.h"

// PR7-O's factory presets: Init in bank A, then the expansions (0.13.0, the owner's ask after the mock-up: "Build
// it"): whole sections from the two libraries PR7-O can download, named by library reference so they play on any
// computer that has the library. Each part's keys: the plugin's own split rule (section::fromFiles) worked out on the
// real files, except Sonatina's eight woodwinds (split by register: the rule, with eight sharing the keyboard, put the
// bass clarinet over the bassoons). Our own choices of files, splits and settings.
namespace
{
    struct Part { const char* file; const char* label; int low, high; };

    // A key's name as the plugin shows it by default (middle C = C3).
    juce::String keyName (int key) { return juce::MidiMessage::getMidiNoteName (key, true, true, 3); }

    // The preset's notes: each part's keys (written from the split itself, so they can't disagree), then `more`.
    juce::String layoutOf (std::initializer_list<Part> parts, const juce::String& more)
    {
        juce::StringArray each;
        for (const auto& p : parts)
            if (p.low > 0 || p.high < 127)
                each.add (juce::String (p.label) + " " + keyName (p.low) + " to " + keyName (p.high));
        return (each.isEmpty() ? juce::String() : each.joinIntoString (", ") + ". ") + more;
    }

    Rig sectionRig (int bank, int number, const char* name, const downloads::Library& library, const char* folder,
                    std::initializer_list<Part> parts, const juce::String& tags, const juce::String& more,
                    std::initializer_list<std::pair<const char*, float>> parameters)
    {
        Rig rig;
        rig.name = name;
        rig.bank = bank;
        rig.number = number;
        rig.tags = juce::StringArray::fromTokens (tags, ",", "");
        rig.tags.trim();
        rig.notes = layoutOf (parts, more);
        sampler::section::Section section;
        for (const auto& p : parts)
            section.parts.push_back ({ downloads::refFor (library, juce::String (folder) + p.file), p.low, p.high });
        rig.instrument = section.parts.size() == 1 && section.parts[0].low == 0 && section.parts[0].high == 127
                             ? section.parts[0].file : section.toText();
        rig.parameters[ParamIDs::instLevel] = -6.0f;   // (a full section's chord: VSCO's files are +20 dB each)
        for (const auto& [id, value] : parameters)
            rig.parameters[id] = value;
        return rig;
    }

    constexpr float modWheel = 1.0f, concertHall = 2.0f, scoringStage = 1.0f, studio = 0.0f, cathedral = 3.0f, octaveDown = 2.0f;
}

std::vector<Rig> makeFactoryRigs()
{
    Rig init;
    init.name = "Init";
    init.bank = 0;
    init.number = 1;
    init.tags = { "Init" };
    init.notes = "A blank page: no instrument loaded, every setting at its start, dry. Load an SFZ on the INSTRUMENT page.";
    std::vector<Rig> rigs { init };

    using namespace ParamIDs;
    const auto& vsco = downloads::vsco();
    const auto& sso = downloads::sonatina();
    const auto vo = Rig::numBanks, so = Rig::numBanks + 1;
    const auto strings = std::initializer_list<Part> { { "Contrabass-KS.sfz", "basses", 24, 35 }, { "CelloEns-KS.sfz", "cellos", 36, 47 },
                                                       { "ViolaEns-KS.sfz", "violas", 48, 54 }, { "ViolinEns-KS.sfz", "violins", 55, 86 } };
    const auto stringKeys = "Articulations on " + keyName (96) + " to " + keyName (100)
                          + ": sustain, non-vibrato, tremolo, spiccato, pizzicato. Dynamics on the mod wheel.";
    const juce::String wheel = "Dynamics on the mod wheel.";
    const juce::String seated = " STAGE on: each part where an orchestra seats it,";

    // VSCO ORCHESTRA
    rigs.push_back (sectionRig (vo, 1, "String Section", vsco, "", strings, "strings, section", stringKeys + seated + " in a Concert Hall.",
                                { { instDynamics, modWheel }, { stageOn, 1.0f }, { stageRoom, concertHall } }));
    rigs.push_back (sectionRig (vo, 2, "String Section Wide", vsco, "", strings, "strings, section, wide",
                                stringKeys + " Three players a part, spread wide across the stage, in a Concert Hall.",
                                { { instDynamics, modWheel }, { stageOn, 1.0f }, { stageRoom, concertHall }, { ensPlayers, 3.0f }, { stageSpread, 1.3f } }));
    rigs.push_back (sectionRig (vo, 3, "Pizzicato Strings", vsco, "",
                                { { "ContrabassPizz.sfz", "basses", 24, 35 }, { "CelloEnsPizz.sfz", "cellos", 36, 47 },
                                  { "ViolaEnsPizz.sfz", "violas", 48, 54 }, { "ViolinEnsPizz.sfz", "violins", 55, 86 } },
                                "strings, pizzicato", "Plucked, played by velocity." + seated + " on a Scoring Stage.",
                                { { stageOn, 1.0f }, { stageRoom, scoringStage } }));
    rigs.push_back (sectionRig (vo, 4, "Tremolo Strings", vsco, "",
                                { { "ContrabassTrem.sfz", "basses", 24, 35 }, { "CelloEnsTrem.sfz", "cellos", 36, 47 },
                                  { "ViolaEnsTrem.sfz", "violas", 48, 54 }, { "ViolinEnsTrem.sfz", "violins", 55, 86 } },
                                "strings, tremolo", "Tremolo bowing. " + wheel + seated + " in a Concert Hall.",
                                { { instDynamics, modWheel }, { stageOn, 1.0f }, { stageRoom, concertHall } }));
    const auto brass = std::initializer_list<Part> { { "TubaSus.sfz", "tuba", 29, 47 }, { "TromboneSus.sfz", "trombones", 48, 56 },
                                                     { "FHornSus.sfz", "horns", 57, 67 }, { "TrumpetSus.sfz", "trumpets", 68, 84 } };
    rigs.push_back (sectionRig (vo, 5, "Brass Section", vsco, "", brass, "brass, section", wheel + seated + " in a Concert Hall.",
                                { { instDynamics, modWheel }, { stageOn, 1.0f }, { stageRoom, concertHall } }));
    rigs.push_back (sectionRig (vo, 6, "Epic Brass", vsco, "", brass, "brass, section, epic",
                                "Every note doubled an octave down, two players a part, on a big Scoring Stage. " + wheel,
                                { { instDynamics, modWheel }, { stageOn, 1.0f }, { stageRoom, scoringStage }, { stageDecay, 1.4f },
                                  { ensPlayers, 2.0f }, { ensDouble, octaveDown }, { ensDoubleLevel, -4.0f } }));
    rigs.push_back (sectionRig (vo, 7, "Woodwind Section", vsco, "",
                                { { "BassoonSus.sfz", "bassoon", 34, 49 }, { "ClarinetSus.sfz", "clarinet", 50, 57 }, { "OboeSusVib.sfz", "oboe", 58, 74 },
                                  { "FluteSusVib.sfz", "flute", 75, 83 }, { "PiccoloSus.sfz", "piccolo", 84, 91 } },
                                "woodwinds, section", wheel + seated + " in a Concert Hall.",
                                { { instDynamics, modWheel }, { stageOn, 1.0f }, { stageRoom, concertHall } }));
    rigs.push_back (sectionRig (vo, 8, "Chamber Woodwinds", vsco, "",
                                { { "BassoonSus.sfz", "bassoon", 34, 49 }, { "ClarinetSus.sfz", "clarinet", 50, 57 },
                                  { "OboeSusVib.sfz", "oboe", 58, 74 }, { "FluteSusVib.sfz", "flute", 75, 96 } },
                                "woodwinds, chamber, legato", "Four players. LEGATO on: play a line and the notes join. " + wheel
                                    + " STAGE on, a Studio's close sound.",
                                { { instDynamics, modWheel }, { instLegato, 1.0f }, { stageOn, 1.0f }, { stageRoom, studio } }));

    // SONATINA (its names say so: a factory preset is found by its name, in projects and THE DEFAULT, so no two match)
    const char* sstr = "Sonatina Symphonic Orchestra/Strings - Performance/";
    const char* sbr = "Sonatina Symphonic Orchestra/Brass - Performance/";
    const char* sww = "Sonatina Symphonic Orchestra/Woodwinds - Performance/";
    const juce::String switches = "Articulations on keyswitches (the INSTRUMENT page lists them). " + wheel;
    rigs.push_back (sectionRig (so, 1, "Sonatina Strings", sso, sstr,
                                { { "Basses KS.sfz", "basses", 24, 35 }, { "Celli KS.sfz", "cellos", 36, 47 }, { "Violas KS.sfz", "violas", 48, 54 },
                                  { "2nd Violins KS.sfz", "2nd violins", 55, 75 }, { "1st Violins KS.sfz", "1st violins", 76, 96 } },
                                "strings, section", "Five parts. " + switches + seated + " in a Concert Hall.",
                                { { instDynamics, modWheel }, { stageOn, 1.0f }, { stageRoom, concertHall } }));
    rigs.push_back (sectionRig (so, 2, "Sonatina Pizzicato", sso, sstr,
                                { { "Basses Pizzicato.sfz", "basses", 24, 35 }, { "Celli Pizzicato.sfz", "cellos", 36, 47 },
                                  { "Violas Pizzicato.sfz", "violas", 48, 54 }, { "2nd Violins Pizzicato.sfz", "2nd violins", 55, 75 },
                                  { "1st Violins Pizzicato.sfz", "1st violins", 76, 96 } },
                                "strings, pizzicato", "Plucked, five parts, played by velocity." + seated + " on a Scoring Stage.",
                                { { stageOn, 1.0f }, { stageRoom, scoringStage } }));
    rigs.push_back (sectionRig (so, 3, "Sonatina Brass", sso, sbr,
                                { { "Tuba KS.sfz", "tuba", 28, 39 }, { "Trombones KS.sfz", "trombones", 40, 58 },
                                  { "Horns KS.sfz", "horns", 59, 68 }, { "Trumpets KS.sfz", "trumpets", 69, 88 } },
                                "brass, section", switches + seated + " in a Concert Hall.",
                                { { instDynamics, modWheel }, { stageOn, 1.0f }, { stageRoom, concertHall } }));
    rigs.push_back (sectionRig (so, 4, "Sonatina Woodwinds", sso, sww,
                                { { "Bassoons KS.sfz", "bassoons", 34, 49 }, { "Clarinets KS.sfz", "clarinets", 50, 57 }, { "Oboes KS.sfz", "oboes", 58, 75 },
                                  { "Flutes KS.sfz", "flutes", 76, 86 }, { "Piccolo Solo KS.sfz", "piccolo", 87, 103 } },
                                "woodwinds, section", switches + seated + " in a Concert Hall.",
                                { { instDynamics, modWheel }, { stageOn, 1.0f }, { stageRoom, concertHall } }));
    rigs.push_back (sectionRig (so, 5, "Sonatina Full Woodwinds", sso, sww,
                                { { "Contrabassoon Solo KS.sfz", "contrabassoon", 22, 33 }, { "Bassoons KS.sfz", "bassoons", 34, 45 },
                                  { "Bass Clarinet Solo KS.sfz", "bass clarinet", 46, 52 }, { "Clarinets KS.sfz", "clarinets", 53, 62 },
                                  { "Cor Anglais Solo KS.sfz", "English horn", 63, 67 }, { "Oboes KS.sfz", "oboes", 68, 77 },
                                  { "Flutes KS.sfz", "flutes", 78, 89 }, { "Piccolo Solo KS.sfz", "piccolo", 90, 103 } },
                                "woodwinds, section, full", "All eight, low to high. " + switches + seated + " in a Concert Hall.",
                                { { instDynamics, modWheel }, { stageOn, 1.0f }, { stageRoom, concertHall } }));
    rigs.push_back (sectionRig (so, 6, "Sonatina Choir", sso, "Sonatina Symphonic Orchestra/Chorus - Performance/",
                                { { "Mixed Chorus.sfz", "choir", 0, 127 } }, "choir, voices",
                                "Sonatina's mixed choir over the whole keyboard, in a Cathedral.", { { stageOn, 1.0f }, { stageRoom, cathedral } }));
    return rigs;
}
