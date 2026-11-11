#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// Presets ("rigs" in the code, as across the PR7 family): files, the
// library, loading and saving, and the factory presets.
#include "Parameters.h"
#include "PluginProcessor.h"
#include "rigs/FactoryRigs.h"
#include "rigs/Expansions.h"
#include "dsp/sampler/Section.h"
#include "library/Downloads.h"
#include "library/Library.h"
#include "ui/RigBrowser.h"
#include "PluginTestUtils.h"

using Catch::Matchers::WithinAbs;

namespace
{
    // A fresh, empty rigs folder for each test.
    struct TempRigFolder
    {
        TempRigFolder() { folder.deleteRecursively(); }
        ~TempRigFolder() { folder.deleteRecursively(); }
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ParagonX1RigTests")
                                .getChildFile (juce::Uuid().toString());
    };

    // Stand-ins for the tests below: a number in the preset (with a default),
    // another (that can go below zero), an on/off that's off by default.
    const juce::String someTime = ParamIDs::instLevel;           // dB, -60 to 12 (0 by default)
    const juce::String someFine = ParamIDs::instTranspose;       // semitones, -24 to 24 (0 by default)
    const juce::String someSwitch = ParamIDs::delayPingPong;     // off by default

    struct RigRig   // a processor with its presets in a temporary folder
    {
        RigRig()
        {
            processor.setPlayConfigDetails (1, 2, 48000.0, 256);
            processor.prepareToPlay (48000.0, 256);
            processor.useRigFolder (temp.folder);
        }

        void set (const juce::String& id, float plainValue)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        }

        float get (const juce::String& id) { return processor.getParameters().getRawParameterValue (id)->load(); }

        const RigLibrary::Entry& entryNamed (const juce::String& name)
        {
            for (const auto& entry : processor.getRigLibrary().getEntries())
                if (entry.rig.name == name)
                    return entry;
            FAIL ("No preset called " << name);
            return processor.getRigLibrary().getEntries().front();
        }

        TempRigFolder temp;
        ParagonX1AudioProcessor processor;
    };
}

TEST_CASE ("A rig survives being written to a file and read back", "[rigs]")
{
    Rig rig;
    rig.name = "AC/DC: \"Back in Black\" & <more>";
    rig.bank = 2;
    rig.number = 17;
    rig.favourite = true;
    rig.tags = { "Rock", "High Gain" };
    rig.parameters = { { "instLevel", -6.5f }, { "instTranspose", -4.0f } };
    rig.instrument = "/libraries/VSCO 2 CE/Strings/Violin Section.sfz";

    const auto restored = Rig::fromXml (*juce::XmlDocument::parse (rig.toXml()->toString()));
    REQUIRE (restored.has_value());
    CHECK (restored->name == rig.name);
    CHECK (restored->getSlotName() == "C17");
    CHECK (restored->favourite);
    CHECK (restored->tags == rig.tags);
    CHECK (restored->parameters == rig.parameters);
    CHECK (restored->instrument == rig.instrument);
}

TEST_CASE ("Files that aren't rigs, or are from a newer version, are refused", "[rigs]")
{
    CHECK_FALSE (Rig::fromXml (*juce::XmlDocument::parse ("<SomethingElse/>")).has_value());
    CHECK_FALSE (Rig::fromXml (*juce::XmlDocument::parse ("<ParagonRig version=\"99\" name=\"x\"/>")).has_value());
}

TEST_CASE ("Every factory rig is valid", "[rigs]")
{
    ParagonX1AudioProcessor processor;
    juce::StringArray names, slots;

    for (const auto& rig : makeFactoryRigs())
    {
        INFO (rig.name);
        names.addIfNotAlreadyThere (rig.name);
        slots.addIfNotAlreadyThere (rig.getSlotName());

        for (const auto& [id, value] : rig.parameters)
        {
            INFO (id);
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters().getParameter (id));
            REQUIRE (parameter != nullptr);                          // no typos in parameter IDs
            CHECK_FALSE (ParamIDs::notInRigs.contains (id));        // rigs don't touch global settings
            const auto range = parameter->getNormalisableRange();
            CHECK (value >= range.start);
            CHECK (value <= range.end);
        }
    }

    CHECK (names.size() == (int) makeFactoryRigs().size());   // unique names
    CHECK (slots.size() == (int) makeFactoryRigs().size());   // no two in the same slot
}

TEST_CASE ("PR7-O's factory presets: Init at A01, no instrument, everything at its default; nothing of PR7-D's", "[rigs]")
{
    const auto rigs = makeFactoryRigs();
    REQUIRE (rigs.size() == 15);   // Init, then the expansions' 14 (0.13.0)
    CHECK (rigs.front().name == "Init");
    for (size_t i = 1; i < rigs.size(); ++i)
        CHECK (Rig::isExpansionBank (rigs[i].bank));
    CHECK (rigs.front().getSlotName() == "A01");
    CHECK (rigs.front().parameters.empty());
    CHECK (rigs.front().instrument.isEmpty());
    CHECK (rigs.front().notes.isNotEmpty());

    RigRig r;
    for (const auto* drumRig : { "Room Kit", "Room Beat", "Djent Kit", "Crocell Metal", "Lead Vocal" })
        CHECK (r.processor.getRigLibrary().findFactory (drumRig) == nullptr);
}

TEST_CASE ("A preset keeps its instrument: saving one, loading another and back loads it again; Init has none", "[rigs]")
{
    RigRig r;
    juce::File folder = r.temp.folder.getChildFile ("library");
    folder.createDirectory();
    testkit::writeWav (folder.getChildFile ("cello.wav"), *testkit::sine (220.0, 0.1), 16);
    folder.getChildFile ("cello.sfz").replaceWithText ("<region> sample=cello.wav lokey=36 hikey=76 pitch_keycenter=48\n");
    r.processor.loadInstrument (folder.getChildFile ("cello.sfz"));
    REQUIRE (r.processor.waitForInstrument());
    REQUIRE (r.processor.getProgram() != nullptr);
    REQUIRE (r.processor.saveCurrentRigAs ("Cello"));
    CHECK (r.entryNamed ("Cello").rig.instrument == folder.getChildFile ("cello.sfz").getFullPathName());

    r.processor.loadRig (*r.processor.getRigLibrary().findFactory ("Init"));
    CHECK (r.processor.getProgram() == nullptr);
    CHECK (r.processor.getInstrumentFile().isEmpty());

    r.processor.loadRig (r.entryNamed ("Cello"));
    REQUIRE (r.processor.waitForInstrument());
    REQUIRE (r.processor.getProgram() != nullptr);
    CHECK (r.processor.getProgram()->lowestKey() == 36);
    CHECK_FALSE (r.processor.isRigModified());
}

TEST_CASE ("A rig's notes are saved with it and kept by Save As", "[rigs]")
{
    Rig rig;
    rig.name = "Noted";
    rig.notes = "For the mix bus.\nTrim first, then limit.";
    const auto loaded = Rig::fromXml (*rig.toXml());
    REQUIRE (loaded.has_value());
    CHECK (loaded->notes == rig.notes);

    // A sound made from Init starts with no notes or tags of Init's (it isn't "a blank page")...
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    REQUIRE (r.processor.saveCurrentRigAs ("My Strings"));
    const auto* saved = r.processor.getRigLibrary().find (r.processor.getCurrentRig().file);
    REQUIRE (saved != nullptr);
    CHECK (saved->rig.notes.isEmpty());
    CHECK (saved->rig.tags.isEmpty());

    // ...and a copy of one of yours keeps its notes and tags.
    auto details = saved->rig;
    details.tags = { "Strings" };
    REQUIRE (r.processor.updateRigDetails (*saved, details));
    auto noted = r.entryNamed ("My Strings").rig;
    noted.notes = "Long notes for the chorus.";
    REQUIRE (r.processor.getRigLibrary().overwrite (r.entryNamed ("My Strings").file, noted));
    r.processor.loadRig (r.entryNamed ("My Strings"));
    REQUIRE (r.processor.saveCurrentRigAs ("My Strings 2"));
    CHECK (r.entryNamed ("My Strings 2").rig.notes == "Long notes for the chorus.");
    CHECK (r.entryNamed ("My Strings 2").rig.tags == juce::StringArray { "Strings" });
}

TEST_CASE ("The rig library saves, finds, renames and deletes rigs", "[rigs]")
{
    TempRigFolder temp;
    RigLibrary library (temp.folder);
    const auto factoryCount = library.getEntries().size();

    Rig rig;
    rig.name = "Cyber Drive";
    const auto first = library.saveAsNew (rig);
    REQUIRE (first.existsAsFile());
    REQUIRE (library.find (first) != nullptr);

    SECTION ("A taken name gets a number")
    {
        const auto second = library.saveAsNew (rig);
        REQUIRE (library.find (second) != nullptr);
        CHECK (library.find (second)->rig.name == "Cyber Drive 2");
        CHECK (second != first);
    }

    SECTION ("Names that can't be file names still save")
    {
        rig.name = "AC/DC: Live?";
        const auto file = library.saveAsNew (rig);
        REQUIRE (file.existsAsFile());
        CHECK (library.find (file)->rig.name == "AC/DC: Live?");
    }

    SECTION ("Overwrite and delete")
    {
        auto changed = library.find (first)->rig;
        changed.tags = { "Lead" };
        REQUIRE (library.overwrite (first, changed));
        CHECK (library.find (first)->rig.tags == juce::StringArray { "Lead" });

        REQUIRE (library.remove (first));
        CHECK (library.find (first) == nullptr);
        CHECK (library.getEntries().size() == factoryCount);
    }

    SECTION ("A corrupt rig file is skipped, not fatal")
    {
        temp.folder.getChildFile (juce::String ("broken") + Rig::fileExtension).replaceWithText ("<ParagonRig name=");
        library.rescan();
        CHECK (library.getEntries().size() == factoryCount + 1);
    }
}

TEST_CASE ("Favourites stick, including on factory rigs", "[rigs]")
{
    TempRigFolder temp;
    {
        RigLibrary library (temp.folder);
        library.setFavourite (*library.findFactory ("Init"), true);
    }
    {
        RigLibrary reopened (temp.folder);
        CHECK (reopened.findFactory ("Init")->rig.favourite);
        reopened.setFavourite (*reopened.findFactory ("Init"), false);
    }

    RigLibrary reopenedAgain (temp.folder);
    CHECK_FALSE (reopenedAgain.findFactory ("Init")->rig.favourite);
}

TEST_CASE ("Rigs are sorted by bank and number, and < > wrap round", "[rigs]")
{
    TempRigFolder temp;
    RigLibrary library (temp.folder);

    Rig late;
    late.name = "Late";
    late.bank = Rig::numBanks - 1;   // the last user bank: after it only the expansions (0.13.0)
    late.number = 50;
    library.saveAsNew (late);

    const auto& entries = library.getEntries();
    for (size_t i = 1; i < entries.size(); ++i)
        CHECK (entries[i - 1].rig.bank * 100 + entries[i - 1].rig.number <= entries[i].rig.bank * 100 + entries[i].rig.number);

    CHECK (entries.back().rig.name == "Sonatina Choir");   // SO06, the last expansion's last
    CHECK (Rig::isExpansionBank (entries[entries.size() - 14].rig.bank));
    CHECK (entries[entries.size() - 14].rig.getSlotName() == "VO01");
    CHECK (entries[entries.size() - 15].rig.name == "Late");
    CHECK (library.step (&entries.back(), 1) == &entries.front());
    CHECK (library.step (&entries.front(), -1) == &entries.back());
    CHECK (library.step (nullptr, 1) == &entries.front());

    CHECK (library.nextFreeNumber (0) == 2);   // the factory presets fill A01 (Init)
}

TEST_CASE ("Saving and loading a preset restores the sound but not the global settings", "[rigs]")
{
    RigRig r;
    r.set (ParamIDs::reverbOn, 1.0f);
    r.set (someTime, 7.5f);
    r.set (someFine, 6.0f);
    r.set (ParamIDs::patchLevel, -3.0f);
    REQUIRE (r.processor.saveCurrentRigAs ("My Sound"));

    // Change everything, including the global OUTPUT.
    r.set (ParamIDs::reverbOn, 0.0f);
    r.set (someTime, 1.0f);
    r.set (someFine, -2.0f);
    r.set (ParamIDs::patchLevel, 0.0f);
    r.set (ParamIDs::outputGain, -4.0f);

    r.processor.loadRig (r.entryNamed ("My Sound"));

    CHECK (r.get (ParamIDs::reverbOn) > 0.5f);
    CHECK_THAT (r.get (someTime), WithinAbs (7.5, 0.05));
    CHECK_THAT (r.get (someFine), WithinAbs (6.0, 0.01));
    CHECK_THAT (r.get (ParamIDs::patchLevel), WithinAbs (-3.0, 0.01));
    // Global settings are left alone.
    CHECK_THAT (r.get (ParamIDs::outputGain), WithinAbs (-4.0, 0.01));
}

TEST_CASE ("A preset that doesn't mention a parameter sets it to its default", "[rigs]")
{
    // E.g. a preset saved by an older version, before that parameter existed.
    RigRig r;
    r.set (someSwitch, 1.0f);
    r.set (someFine, 6.0f);

    Rig old;
    old.name = "Old Preset";
    old.parameters = { { ParamIDs::reverbOn, 1.0f } };
    r.processor.getRigLibrary().saveAsNew (old);
    r.processor.loadRig (r.entryNamed ("Old Preset"));

    CHECK (r.get (ParamIDs::reverbOn) > 0.5f);
    CHECK (r.get (someSwitch) < 0.5f);
    CHECK_THAT (r.get (someFine), WithinAbs (0.0, 0.01));
}

TEST_CASE ("The modified marker tracks changes since loading or saving", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    CHECK_FALSE (r.processor.isRigModified());

    SECTION ("Turning a knob marks it modified")
    {
        r.set (someTime, 9.0f);
        CHECK (r.processor.isRigModified());
    }

    SECTION ("The global settings don't count")
    {
        r.set (ParamIDs::outputGain, -3.0f);
        CHECK_FALSE (r.processor.isRigModified());
    }

    SECTION ("Saving clears it")
    {
        r.set (someTime, 9.0f);
        REQUIRE (r.processor.saveCurrentRigAs ("Saved"));
        CHECK_FALSE (r.processor.isRigModified());
        r.set (someTime, 2.0f);
        CHECK (r.processor.isRigModified());
        REQUIRE (r.processor.saveCurrentRig());   // a user preset now, so plain save works
        CHECK_FALSE (r.processor.isRigModified());
    }
}

TEST_CASE ("Factory presets can't be saved over", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    r.set (someFine, 9.0f);
    CHECK_FALSE (r.processor.saveCurrentRig());   // the UI then asks for a new name
    CHECK (r.entryNamed ("Init").rig.parameters.count (someFine) == 0);
}

TEST_CASE ("< and > step through the presets in order", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    REQUIRE (r.processor.saveCurrentRigAs ("Second"));   // filed after bank A's factory presets, at A02
    REQUIRE (r.processor.saveCurrentRigAs ("Third"));    // A03
    CHECK (r.entryNamed ("Second").rig.getSlotName() == "A02");
    r.processor.loadRig (r.entryNamed ("Init"));   // A01, the only factory one
    r.processor.stepRig (1);
    CHECK (r.processor.getCurrentRig().name == "Second");
    r.processor.stepRig (1);
    CHECK (r.processor.getCurrentRig().name == "Third");
    r.processor.stepRig (1);   // then the expansions (0.13.0): VO01
    CHECK (r.processor.getCurrentRig().name == "String Section");
    r.processor.stepRig (-1);
    CHECK (r.processor.getCurrentRig().name == "Third");
    r.processor.loadRig (r.entryNamed ("Init"));
    r.processor.stepRig (-1);   // wraps round to the last: SO06
    CHECK (r.processor.getCurrentRig().name == "Sonatina Choir");
    r.processor.stepRig (1);   // and back to the first
    CHECK (r.processor.getCurrentRig().name == "Init");
}

TEST_CASE ("Deleting the loaded preset keeps the sound, now unsaved", "[rigs]")
{
    RigRig r;
    r.set (someTime, 8.0f);
    REQUIRE (r.processor.saveCurrentRigAs ("Temporary"));
    REQUIRE (r.processor.deleteRig (r.entryNamed ("Temporary")));

    CHECK_FALSE (r.processor.getCurrentRig().isUserRig());
    CHECK (r.processor.isRigModified());
    CHECK_THAT (r.get (someTime), WithinAbs (8.0, 0.01));
}

TEST_CASE ("The DAW project remembers which preset was loaded", "[rigs][state]")
{
    juce::MemoryBlock saved;
    juce::File rigFile;
    {
        RigRig r;
        r.set (someTime, 8.0f);
        REQUIRE (r.processor.saveCurrentRigAs ("Project Preset"));
        r.set (someTime, 3.0f);   // tweaked after saving
        rigFile = r.processor.getCurrentRig().file;
        r.processor.getStateInformation (saved);
    }

    ParagonX1AudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK (restored.getCurrentRig().name == "Project Preset");
    CHECK (restored.getCurrentRig().file == rigFile);
    CHECK (restored.isRigModified());   // the tweak is remembered as unsaved
    CHECK_THAT (restored.getParameters().getRawParameterValue (someTime)->load(), WithinAbs (3.0, 0.01));
}

TEST_CASE ("Each preset's LEVEL adds to OUTPUT", "[rigs]")
{
    const auto play = [] (float levelDb)
    {
        RigRig r;
        testkit::useSample (r.processor, testkit::sine (220.0));
        r.set (ParamIDs::patchLevel, levelDb);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
        double sum = 0.0;
        for (int block = 0; block < 40; ++block)
        {
            r.processor.processBlock (buffer, midi);
            midi.clear();
            if (block >= 20)
                sum += buffer.getRMSLevel (0, 0, 256);
        }
        return sum;
    };
    CHECK_THAT (play (6.0f) / play (0.0f), WithinAbs (1.9953, 0.01));
}

TEST_CASE ("Presets live in their own Paragon PR7-O folder, apart from its siblings' rigs", "[rigs]")
{
    const auto folder = RigLibrary::defaultFolder();
    CHECK (folder.getFileName() == "Rigs");
    CHECK (folder.getParentDirectory().getFileName() == "Paragon PR7-O");
    CHECK (folder.getParentDirectory().getParentDirectory().getFileName() == "Paragon");

    // PR7-E's (mixing), PR7-B's (bass), PR7-G's (guitar) and PR7-K's (synth) rigs folders sit next to it, and are
    // different folders.
    for (const auto* sibling : { "Paragon PR7-E", "Paragon PR7-B", "Paragon PR7-G", "Paragon PR7-K" })
    {
        const auto siblingFolder = folder.getParentDirectory().getSiblingFile (sibling).getChildFile ("Rigs");
        CHECK (folder != siblingFolder);
        CHECK_FALSE (folder.isAChildOf (siblingFolder.getParentDirectory()));
    }
}

TEST_CASE ("Only .pr7rig files are rigs: the old Paragon X1 .x1rig files aren't read", "[rigs]")
{
    TempRigFolder temp;
    REQUIRE (temp.folder.createDirectory());
    Rig old;
    old.name = "From X1";
    REQUIRE (old.toXml()->writeTo (temp.folder.getChildFile ("From X1.x1rig")));

    RigLibrary library (temp.folder);
    CHECK (std::none_of (library.getEntries().begin(), library.getEntries().end(),
                         [] (const auto& entry) { return entry.rig.name == "From X1"; }));
}

// 0.13.0: THE EXPANSIONS, PR7-K's system: a bank per built-in expansion after A-E, named by its code, factory only.
TEST_CASE ("Expansions: VSCO ORCHESTRA and SONATINA are banks after E, their presets named VO01..., SO01...", "[rigs][expansions]")
{
    CHECK (Rig::numBanksWithExpansions() == Rig::numBanks + 2);
    CHECK (Rig::bankName (4) == "E");
    CHECK (Rig::bankName (Rig::numBanks) == "VSCO ORCHESTRA");
    CHECK (Rig::bankName (Rig::numBanks + 1) == "SONATINA");
    juce::StringArray vo, so;
    for (const auto& rig : makeFactoryRigs())
        if (rig.bank == Rig::numBanks) vo.add (rig.getSlotName());
        else if (rig.bank == Rig::numBanks + 1) so.add (rig.getSlotName());
    CHECK (vo.joinIntoString (" ") == "VO01 VO02 VO03 VO04 VO05 VO06 VO07 VO08");
    CHECK (so.joinIntoString (" ") == "SO01 SO02 SO03 SO04 SO05 SO06");

    // Written to a file, an expansion's bank (which a user's preset can't be in) reads back as A: never a user's VO slot.
    auto rig = makeFactoryRigs()[1];
    const auto back = Rig::fromXml (*rig.toXml());
    REQUIRE (back.has_value());
    CHECK (back->bank == 0);
}

TEST_CASE ("Expansions: each preset plays its own library by reference, its parts in key order inside each file's range", "[rigs][expansions]")
{
    for (const auto& rig : makeFactoryRigs())
    {
        if (! Rig::isExpansionBank (rig.bank))
            continue;
        INFO (rig.name);
        const auto& wanted = rig.bank == Rig::numBanks ? downloads::vsco() : downloads::sonatina();
        std::vector<sampler::section::Part> parts;
        if (const auto section = sampler::section::fromText (rig.instrument))
            parts = section->parts;
        else
            parts.push_back ({ rig.instrument, 0, 127 });
        REQUIRE_FALSE (parts.empty());
        CHECK (parts.size() <= 8);   // the sampler's most parts
        auto lastHigh = -1;
        for (const auto& part : parts)
        {
            INFO (part.file);
            CHECK (downloads::isRef (part.file));
            REQUIRE (downloads::libraryFor (part.file) != nullptr);
            CHECK (downloads::libraryFor (part.file)->folder == wanted.folder);
            CHECK (part.file.endsWithIgnoreCase (".sfz"));
            CHECK (part.low <= part.high);
            CHECK (part.low > lastHigh);   // split, one part a key
            lastHigh = part.high;
        }
        CHECK (rig.notes.isNotEmpty());
        CHECK (rig.parameters.count (ParamIDs::stageOn) == 1);
    }
}

TEST_CASE ("Expansions: a copy saved from one goes to bank A; the browser lists them and offers GET for a missing library", "[rigs][expansions]")
{
    const auto userFolders = library::folders();
    library::setFolders ({});
    const auto libraries = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("pr7o-exp-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    downloads::setFolder (libraries);   // nothing downloaded

    {
        RigRig r;
        const auto* strings = r.processor.getRigLibrary().findFactory ("String Section");
        REQUIRE (strings != nullptr);
        CHECK (strings->rig.getSlotName() == "VO01");
        r.processor.loadRig (*strings);
        REQUIRE (r.processor.saveCurrentRigAs ("My Strings"));
        CHECK (r.entryNamed ("My Strings").rig.bank == 0);
        CHECK (r.entryNamed ("My Strings").rig.instrument == strings->rig.instrument);

        RigBrowser browser (r.processor);
        browser.setSize (1400, 800);
        juce::StringArray banks;
        for (auto* c : browser.getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getRadioGroupId() == 2)
                banks.add (b->getButtonText());
        CHECK (banks.joinIntoString ("|") == "All Banks|A|B|C|D|E|VSCO ORCHESTRA|SONATINA");
        CHECK_FALSE (browser.getExpansionsHeading().isEmpty());

        browser.selectForTest (r.processor.getRigLibrary().findFactory ("Sonatina Brass"));
        CHECK (browser.getInfoText().contains ("SONATINA: "));                    // the bank's description
        CHECK (browser.getInfoText().contains ("PLAYS SONATINA: NOT DOWNLOADED YET"));
        REQUIRE (browser.getNeededLibrary() == &downloads::sonatina());
        CHECK (browser.getGetButton().isVisible());
        CHECK (browser.getGetButton().getButtonText().startsWith ("GET SONATINA"));

        browser.selectForTest (r.processor.getRigLibrary().findFactory ("Init"));
        CHECK (browser.getNeededLibrary() == nullptr);
        CHECK_FALSE (browser.getGetButton().isVisible());
    }

    downloads::setFolder ({});
    libraries.deleteRecursively();
    library::setFolders (userFolders);
}
