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

    // VIRTUAL PLAYING ORCHESTRA (0.17.0; its "performance" files: the harder a note's played, the quicker its attack,
    // and the mod wheel sets the level, by the files' own gain_cc1, so PR7-O's DYNAMICS stays on velocity). Names start
    // "VPO" (unique, as above). LEVEL 0 dB (the others' -6 dB): with the wheel at its start, 64, VPO's sections sit
    // about 12 dB under VSCO's; the wheel's full lift (+17 dB) then peaks near 0 dBFS. The top parts end at B6 so each
    // section's keyswitches land on C7 up, as VSCO's strings' do (past it they'd go under the lowest note: C-1, off an
    // 88-key keyboard).
    const auto& vp = downloads::vpo();
    const auto vpo = Rig::numBanks + 2;
    const juce::String perf = " VPO's performance files: play harder for a quicker attack; the mod wheel sets the level.";
    const juce::String vpoSwitches = "Articulations on keyswitches (the rack's SWITCHES page lists them).";
    rigs.push_back (sectionRig (vpo, 1, "VPO String Section", vp, "Strings/",
                                { { "bass-SEC-PERF-KS-C6.sfz", "basses", 24, 35 }, { "cello-SEC-PERF-KS-C6.sfz", "cellos", 36, 47 },
                                  { "viola-SEC-PERF-KS-C2.sfz", "violas", 48, 54 }, { "2nd-violin-SEC-PERF-KS-C2.sfz", "2nd violins", 55, 75 },
                                  { "1st-violin-SEC-PERF-KS-C2.sfz", "1st violins", 76, 95 } },
                                "strings, section", "Five parts: sustain, tremolo, staccato, pizzicato. " + vpoSwitches + perf + seated + " in a Concert Hall.",
                                { { instLevel, 0.0f }, { stageOn, 1.0f }, { stageRoom, concertHall } }));
    rigs.push_back (sectionRig (vpo, 2, "VPO Pizzicato Strings", vp, "Strings/",
                                { { "bass-SEC-PERF-pizzicato.sfz", "basses", 24, 35 }, { "cello-SEC-PERF-pizzicato.sfz", "cellos", 36, 47 },
                                  { "viola-SEC-PERF-pizzicato.sfz", "violas", 48, 54 }, { "2nd-violin-SEC-PERF-pizzicato.sfz", "2nd violins", 55, 75 },
                                  { "1st-violin-SEC-PERF-pizzicato.sfz", "1st violins", 76, 95 } },
                                "strings, pizzicato", "Plucked, five parts, played by velocity." + seated + " on a Scoring Stage.",
                                { { instLevel, 0.0f }, { stageOn, 1.0f }, { stageRoom, scoringStage } }));
    rigs.push_back (sectionRig (vpo, 3, "VPO Brass Section", vp, "Brass/",
                                { { "tuba-SOLO-PERF-KS-C6.sfz", "tuba", 26, 45 }, { "trombone-SEC-PERF-KS-C6.sfz", "trombones", 46, 56 },
                                  { "french-horn-SEC-PERF-KS-C6.sfz", "horns", 57, 67 }, { "trumpet-SEC-PERF-KS-C2.sfz", "trumpets", 68, 84 } },
                                "brass, section", "Sustain and staccato. " + vpoSwitches + perf + seated + " in a Concert Hall.",
                                { { instLevel, 0.0f }, { stageOn, 1.0f }, { stageRoom, concertHall } }));
    rigs.push_back (sectionRig (vpo, 4, "VPO Woodwind Section", vp, "Woodwinds/",
                                { { "bassoon-SEC-PERF-KS-C6.sfz", "bassoons", 34, 49 }, { "clarinet-SEC-PERF-KS-C2.sfz", "clarinets", 50, 57 },
                                  { "oboe-SEC-PERF-KS-C2.sfz", "oboes", 58, 74 }, { "flute-SEC-PERF-KS-C2.sfz", "flutes", 75, 86 },
                                  { "piccolo-SOLO-PERF-KS-C2.sfz", "piccolo", 87, 95 } },
                                "woodwinds, section", "Sustain and staccato. " + vpoSwitches + perf + seated + " in a Concert Hall.",
                                { { instLevel, 0.0f }, { stageOn, 1.0f }, { stageRoom, concertHall } }));
    rigs.push_back (sectionRig (vpo, 5, "VPO Woodwind Colours", vp, "Woodwinds/",
                                { { "contrabassoon-SOLO-PERF-KS-C6.sfz", "contrabassoon", 22, 33 }, { "bass-clarinet-SOLO-PERF-KS-C6.sfz", "bass clarinet", 34, 51 },
                                  { "english-horn-SOLO-PERF-KS-C2.sfz", "English horn", 52, 68 }, { "alto-flute-SOLO-PERF-KS-C2.sfz", "alto flute", 69, 82 },
                                  { "piccolo-SOLO-PERF-KS-C2.sfz", "piccolo", 83, 95 } },
                                "woodwinds, solo, colours", "The woodwinds' relatives, one player each, low to high. " + vpoSwitches + perf
                                    + " STAGE on, a Studio's close sound.",
                                { { instLevel, 0.0f }, { stageOn, 1.0f }, { stageRoom, studio } }));
    rigs.push_back (sectionRig (vpo, 6, "VPO Staccato Strings", vp, "Strings/", { { "all-strings-SEC-PERF-staccato.sfz", "strings", 0, 127 } },
                                "strings, staccato, ostinato", "The whole string section in one, short notes for driving ostinatos: two players "
                                    "a note, on a Scoring Stage.",
                                { { instLevel, 6.0f }, { stageOn, 1.0f }, { stageRoom, scoringStage }, { ensPlayers, 2.0f } }));
    rigs.push_back (sectionRig (vpo, 7, "VPO Brass Stabs", vp, "Brass/", { { "all-brass-SEC-PERF-staccato.sfz", "brass", 0, 127 } },
                                "brass, staccato, stabs, epic", "The whole brass section in one, short, every note doubled an octave down: "
                                    "hits for heavy riffs, on a big Scoring Stage.",
                                { { instLevel, 0.0f }, { stageOn, 1.0f }, { stageRoom, scoringStage }, { stageDecay, 1.4f }, { ensDouble, octaveDown }, { ensDoubleLevel, -4.0f } }));
    rigs.push_back (sectionRig (vpo, 8, "VPO Choir", vp, "Vocals/", { { "choir-MIXED-PERF.sfz", "choir", 0, 127 } }, "choir, voices",
                                "VPO's mixed choir, in a Cathedral." + perf, { { instLevel, 0.0f }, { stageOn, 1.0f }, { stageRoom, cathedral } }));
    rigs.push_back (sectionRig (vpo, 9, "VPO Timpani", vp, "Percussion/", { { "timpani-KS-C6.sfz", "timpani", 0, 127 } }, "percussion, timpani",
                                "Hits and rolls on keyswitches (" + keyName (84) + " hit, " + keyName (86) + " roll), in a Concert Hall.",
                                { { instLevel, 0.0f }, { stageOn, 1.0f }, { stageRoom, concertHall } }));
    rigs.push_back (sectionRig (vpo, 10, "VPO Harp", vp, "Strings/", { { "harp-sustain.sfz", "harp", 0, 127 } }, "harp, keys",
                                "The concert harp, let ring, in a Concert Hall.", { { stageOn, 1.0f }, { stageRoom, concertHall } }));
    return rigs;
}
