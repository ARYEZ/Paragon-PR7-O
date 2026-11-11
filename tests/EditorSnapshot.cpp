#include <catch2/catch_test_macros.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/mixer/MixerEffects.h"
#include "dsp/effects/Drive.h"
#include "dsp/effects/LoFi.h"
#include "dsp/effects/Reverb.h"
#include "rigs/FactoryRigs.h"
#include "ui/Finish.h"
#include "ui/ParagonLookAndFeel.h"

// Hidden test (the "[.]" tag): renders the editor to a PNG for reviewing the UI without a DAW. Run with:
//   PARAGON_SNAPSHOT=editor.png ./ParagonX1Tests "[snapshot]"
// Options (environment variables):
//   PARAGON_SNAPSHOT_WIDTH=n         the window's width (default 1600; 3200 for a sharp 2x look)
//   PARAGON_SNAPSHOT_PAGE=instrument THE INSTRUMENT, the page it opens on (also with no PAGE); =fx, =presets (with two
//                                    example user presets), =settings
//   PARAGON_SNAPSHOT_INSTRUMENT=1    a demo instrument made here (invented: sines in 16 zones x 3 velocity layers x 2 round
//                                    robins over C1 to C6, as "Demo Strings.sfz"), loaded
//   PARAGON_SNAPSHOT_SFZ=<file>      instead, an SFZ from your computer (none ship with the tests), loaded
//   PARAGON_SNAPSHOT_SECTION=<file>,<file>...   instead, a section of these SFZ files, split over the keys
//   PARAGON_SNAPSHOT_STREAM=0                   load them whole into memory (0.11.0: long recordings stream)
//   PARAGON_SNAPSHOT_SWITCH=<key>    with its keyswitch `key` (a MIDI note number) chosen
//   PARAGON_SNAPSHOT_ROUTING=1       the INSTRUMENT page's rack on its ROUTING page (0.5.0)
//   PARAGON_SNAPSHOT_REGIONS=1       the region map instead of the instrument's picture (0.5.0)
//   PARAGON_SNAPSHOT_STAGE=1         the STAGE view and the rack's STAGE page (0.8.0)
//   PARAGON_SNAPSHOT_ENSEMBLE=1      the rack's ENSEMBLE page (0.10.0)
//   PARAGON_SNAPSHOT_SWITCHES=1      the rack's ARTICULATIONS page (0.14.0)
//   PARAGON_SNAPSHOT_PAGE=mixer      THE MIXER (0.15.0; PARAGON_SNAPSHOT_MIXERDEMO=1: strips set; _MIXERFX=<strip>,<slot>: its editor)
//   PARAGON_SNAPSHOT_LIBRARY=<folder>,<folder>...   the library browser open on these folders (not remembered), its
//                                    strings, the instrument's file chosen if it's in them (0.11.0)
//   PARAGON_SNAPSHOT_DOWNLOAD=<0-100>   with LIBRARY: VSCO 2 CE shown downloading that far (nothing fetched)
//   PARAGON_SNAPSHOT_LIBRARY_SECTION=<n>   that section shown (0 strings, 1 brass...); PARAGON_SNAPSHOT_SEARCH=<text>
//   PARAGON_SNAPSHOT_NOTES=1         and a C major chord (C2 G2 E3 C4) held on the keyboard, sounding (0.12.0: or
//                                    a list of MIDI notes, "67,72")
//   PARAGON_SNAPSHOT_LOADING=1       the instrument page while an instrument loads, its progress box 62% done
//   PARAGON_SNAPSHOT_KEYBOARD=0      the keyboard hidden (the shorter window)
//   PARAGON_SNAPSHOT_PRESET=name     that factory preset loaded
//   PARAGON_SNAPSHOT_PARAMS=id=value,...   parameters set (real units)
//   PARAGON_SNAPSHOT_FINISH=0|1|2    the plugin's finish (pearl white, pearl black, pearl white + neon; not saved)
//   PARAGON_SNAPSHOT_PAGE=fx         THE RACK (PARAGON_SNAPSHOT_RACK=1: at work on the demo instrument's chords, the sends
//                                    and bus A too; PARAGON_SNAPSHOT_FX=eq|dominion|comp|limiter|sends|delay|reverb|modfx|
//                                    mod|mod2|lofi|inserts|insdrive|insglitch|insmod|inslofi: that view, the inserts' on bus
//                                    PARAGON_SNAPSHOT_BUS=1-4; PARAGON_SNAPSHOT_MATCH=1 with FX=eq: EQ Match open)
//   PARAGON_SNAPSHOT_MIDI=1          demo MIDI links and a knob learning (with PAGE=settings: the links list)
namespace
{
    // The demo instrument: our own sines, as an orchestral library lays out its samples (a zone every minor third,
    // three dynamics, two round robins), so the region map and the info look like a real instrument's.
    std::shared_ptr<sampler::Program> demoInstrument()
    {
        auto program = std::make_shared<sampler::Program>();
        program->name = "Demo Strings";
        for (int zone = 0; zone < 16; ++zone)
        {
            const auto root = 37 + zone * 3;
            for (int layer = 0; layer < 3; ++layer)
                for (int rr = 1; rr <= 2; ++rr)
                {
                    auto sample = std::make_shared<sampler::Sample>();
                    sample->name = "zone";
                    const auto hz = 440.0 * std::pow (2.0, (root - 69) / 12.0);
                    sample->channels.assign (2, std::vector<float> (48000 * 4));
                    for (size_t i = 0; i < sample->channels[0].size(); ++i)
                    {
                        const auto t = (double) i / 48000.0;
                        const auto x = (float) ((0.15 + 0.1 * layer) * std::sin (juce::MathConstants<double>::twoPi * hz * t)
                                                * std::min (1.0, t / 0.08));
                        sample->channels[0][i] = x;
                        sample->channels[1][i] = x * 0.9f;
                    }
                    program->bytes += sample->bytes();
                    sampler::Region r;
                    r.sample = std::move (sample);
                    r.loKey = zone == 0 ? 36 : root - 1;
                    r.hiKey = zone == 15 ? 96 : root + 1;
                    r.keyCenter = root;
                    r.loVel = layer == 0 ? 1 : layer == 1 ? 51 : 96;
                    r.hiVel = layer == 0 ? 50 : layer == 1 ? 95 : 127;
                    r.seqLength = 2;
                    r.seqPosition = rr;
                    r.release = 0.4f;
                    program->regions.push_back (std::move (r));
                }
        }
        program->numFiles = (int) program->regions.size();
        program->unread.add ("loop_mode=loop_continuous (96)");
        program->unread.add ("xfin_locc1 (32)");
        program->index();
        return program;
    }
}

TEST_CASE ("Render the editor to a PNG", "[.][snapshot]")
{
    const auto env = [] (const char* name) { return juce::SystemStats::getEnvironmentVariable (name, {}); };
    const auto path = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT", "editor.png");

    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);

    const auto rigFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonX1SnapshotRigs");
    rigFolder.deleteRecursively();
    processor.useRigFolder (rigFolder);

    const auto page = env ("PARAGON_SNAPSHOT_PAGE");
    const auto demo = env ("PARAGON_SNAPSHOT_INSTRUMENT") == "1" || env ("PARAGON_SNAPSHOT_RACK") == "1" || page == "presets";
    if (demo)
        processor.setProgram (demoInstrument(), "C:/Libraries/Demo Orchestra/Strings/Demo Strings.sfz");
    if (env ("PARAGON_SNAPSHOT_STREAM") == "0")
        processor.setStreamFromDisk (false);   // 0.11.0: whole recordings in memory, as before
    if (const auto sfz = env ("PARAGON_SNAPSHOT_SFZ"); sfz.isNotEmpty())
    {
        processor.loadInstrument (juce::File (sfz));
        REQUIRE (processor.waitForInstrument());
        REQUIRE (processor.getProgram() != nullptr);
    }
    if (const auto files = env ("PARAGON_SNAPSHOT_SECTION"); files.isNotEmpty())
    {
        processor.loadSection (sampler::section::fromFiles (juce::StringArray::fromTokens (files, ",", "")));
        REQUIRE (processor.waitForInstrument (120000));
        REQUIRE (processor.getProgram() != nullptr);
    }
    if (const auto key = env ("PARAGON_SNAPSHOT_SWITCH"); key.isNotEmpty())
    {
        processor.chooseKeyswitch (key.getIntValue());
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
    }
    if (page == "presets")
    {
        processor.saveCurrentRigAs ("Demo Strings Hall");
        processor.saveCurrentRigAs ("Demo Strings Dry");
    }

    if (const auto name = env ("PARAGON_SNAPSHOT_PRESET"); name.isNotEmpty())
    {
        const auto* entry = processor.getRigLibrary().findFactory (name);
        REQUIRE (entry != nullptr);
        processor.loadRig (*entry);
    }

    for (const auto& pair : juce::StringArray::fromTokens (env ("PARAGON_SNAPSHOT_PARAMS"), ",", {}))
    {
        const auto id = pair.upToFirstOccurrenceOf ("=", false, false).trim();
        auto* parameter = processor.getParameters().getParameter (id);
        INFO (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (pair.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    }

    // A chord held on the keyboard, played for a moment so it's sounding.
    if (const auto notes = env ("PARAGON_SNAPSHOT_NOTES"); notes.isNotEmpty())
    {
        const auto* program = processor.getProgram().get();
        const auto lowest = program != nullptr ? program->lowestKey() : 36;
        if (notes == "1")
            for (const auto note : { 0, 7, 16, 24 })   // C major from the instrument's lowest C (C2 G2 E3 C4 on the demo)
                processor.getKeyboardState().noteOn (1, (lowest + 11) / 12 * 12 + note, 0.8f);
        else   // 0.12.0: these MIDI notes
            for (const auto& n : juce::StringArray::fromTokens (notes, ",", ""))
                processor.getKeyboardState().noteOn (1, n.getIntValue(), 0.8f);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (int block = 0; block < 40; ++block)
            processor.processBlock (buffer, midi);
    }

    if (const auto f = env ("PARAGON_SNAPSHOT_FINISH"); f.isNotEmpty())
        Finish::set ((Finish::Type) juce::jlimit (0, 2, f.getIntValue()), false);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    if (page == "mixer")
    {
        // THE MIXER (0.15.0). PARAGON_SNAPSHOT_MIXERDEMO=1: strips set as a mix might be (levels, pans, effects, a
        // solo, a send); PARAGON_SNAPSHOT_MIXERFX=<strip>,<slot>: that slot's editor open.
        view->showPage (MainView::Page::mixer);
        auto& mixerPage = view->getMixerPage();
        mixerPage.refresh();
        if (env ("PARAGON_SNAPSHOT_MIXERDEMO") == "1")
        {
            const auto parts = processor.getStageParts();
            const float levels[] = { -2.0f, -4.5f, -1.0f, -6.0f, -3.0f, -2.5f, -5.0f, -1.5f };
            const float pans[] = { -0.35f, -0.1f, 0.2f, 0.4f, -0.5f, 0.5f, 0.0f, 0.1f };
            for (size_t i = 0; i < parts.size(); ++i)
            {
                auto strip = processor.getStrip (parts[i].name);
                strip.levelDb = levels[i % 8];
                strip.pan = pans[i % 8];
                strip.reverbDb = -6.0f - (float) i;
                strip.delayDb = i == 0 ? -12.0f : mixer::levelOffDb;
                if (i % 2 == 0)
                    strip.fx[0] = mixerfx::defaultSlot (mixerfx::eq);
                if (i == 0)
                    strip.fx[1] = mixerfx::defaultSlot (mixerfx::compressor);
                if (i == 2)
                    strip.fx[1] = mixerfx::defaultSlot (mixerfx::saturation);
                strip.solo = i == 2;
                processor.setStrip (parts[i].name, strip);
            }
            auto section = processor.getSectionStrip();
            section.fx[0] = mixerfx::defaultSlot (mixerfx::eq);
            section.fx[1] = mixerfx::defaultSlot (mixerfx::compressor);
            processor.setSectionStrip (section);
            mixerPage.refresh();
        }
        if (const auto fx = juce::StringArray::fromTokens (env ("PARAGON_SNAPSHOT_MIXERFX"), ",", ""); fx.size() == 2)
            mixerPage.openEditor (fx[0].getIntValue(), fx[1].getIntValue());
    }
    if (page == "fx")
    {
        // THE RACK. PARAGON_SNAPSHOT_RACK=1: all four units at work on the demo instrument's chords (ECHELON's low
        // cut, a dip, a dynamic presence band and an air shelf; DOMINION; CONSTRICTOR; the LIMITER on MODERN with TRUE
        // PEAK, 6 dB in), the editor's timers run so the ladders and screens read it.
        // PARAGON_SNAPSHOT_FX=eq|dominion|comp|limiter opens that unit's page.
        view->showPage (MainView::Page::fx);
        if (env ("PARAGON_SNAPSHOT_RACK") == "1")
        {
            const auto set = [&] (const juce::String& id, float value)
            {
                auto* parameter = processor.getParameters().getParameter (id);
                REQUIRE (parameter != nullptr);
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            };
            struct Band { eq::Shape shape; float hz, db, q, dynamic; };
            const Band bands[] = { { eq::Shape::lowCut, 40.0f, 0.0f, 0.7f, 0.0f }, { eq::Shape::bell, 280.0f, -3.0f, 1.2f, 0.0f },
                                   { eq::Shape::bell, 3200.0f, 1.5f, 1.5f, -4.0f }, { eq::Shape::highShelf, 11000.0f, 2.0f, 0.7f, 0.0f } };
            for (int i = 0; i < 4; ++i)
            {
                set (ParamIDs::eqBand (i, "Used"), 1.0f);
                set (ParamIDs::eqBand (i, "Shape"), (float) (int) bands[i].shape);
                set (ParamIDs::eqBand (i, "Freq"), bands[i].hz);
                set (ParamIDs::eqBand (i, "Gain"), bands[i].db);
                set (ParamIDs::eqBand (i, "Q"), bands[i].q);
                set (ParamIDs::eqBand (i, "Dyn"), bands[i].dynamic);
            }
            set (ParamIDs::eqOn, 1.0f);
            set (ParamIDs::domOn, 1.0f);
            set (ParamIDs::domDepth, 7.0f);
            set (ParamIDs::compOn, 1.0f);
            set (ParamIDs::compThreshold, -26.0f);
            set (ParamIDs::compRatio, 3.0f);
            set (ParamIDs::limitOn, 1.0f);
            set (ParamIDs::limitTruePeak, 1.0f);
            set (ParamIDs::limitGain, 6.0f);
            // The SENDS: DELAY in ping-pong eighths dotted, REVERB a hall; the instrument sends to both.
            set (ParamIDs::delayOn, 1.0f);
            set (ParamIDs::delayPingPong, 1.0f);
            set (ParamIDs::delaySync, 7.0f);
            set (ParamIDs::delayFeedback, 0.45f);
            set (ParamIDs::delayToReverb, 0.3f);
            set (ParamIDs::reverbOn, 1.0f);
            set (ParamIDs::reverbType, (float) (int) effects::Reverb::Type::hall);
            set (ParamIDs::reverbSize, 0.65f);
            set (ParamIDs::instSendDelay, 0.3f);
            set (ParamIDs::instSendReverb, 0.4f);
            // MOD / LO-FI: the Vortex in STEREO, AEON after it, a gently worn cassette.
            set (ParamIDs::modOn, 1.0f);
            set (ParamIDs::modStereo, 1.0f);
            set (ParamIDs::modMix, 0.6f);
            set (ParamIDs::mod2On, 1.0f);
            set (ParamIDs::mod2Depth, 0.6f);
            set (ParamIDs::mod2Res, 6.5f);
            set (ParamIDs::lofiOn, 1.0f);
            set (ParamIDs::lofiType, (float) (int) effects::LoFi::Type::cassette);
            set (ParamIDs::lofiAmount, 3.0f);
            set (ParamIDs::lofiNoise, 2.0f);
            // THE INSERTS: the instrument through bus A: SKYLINE driven, a SEQUENCE glitch.
            set (ParamIDs::instInsert, 1.0f);
            set (ParamIDs::ins (0, "DriveOn"), 1.0f);
            set (ParamIDs::ins (0, "DriveModel"), (float) (int) effects::Drive::Model::skyline);
            set (ParamIDs::ins (0, "DriveGain"), 7.0f);
            set (ParamIDs::ins (0, "GlitchOn"), 1.0f);
            set (ParamIDs::ins (0, "GlitchMix"), 0.6f);
            struct Song final : juce::AudioPlayHead
            {
                double ppq = 0.0;
                juce::Optional<PositionInfo> getPosition() const override
                {
                    PositionInfo info;
                    info.setBpm (120.0);
                    info.setPpqPosition (ppq);
                    info.setIsPlaying (true);
                    return info;
                }
            };
            static Song song;
            processor.setPlayHead (&song);
            juce::AudioBuffer<float> buffer (2, 256);
            for (int block = 0; block < 700; ++block)
            {
                juce::MidiBuffer midi;   // a chord every second
                if (block % 188 == 0)
                    for (const auto note : { 48, 55, 64, 72 })
                        midi.addEvent (juce::MidiMessage::noteOn (1, note + (block / 188) % 3 * 2, (juce::uint8) 100), 0);
                processor.processBlock (buffer, midi);
                song.ppq += 256.0 / 48000.0 * 2.0;
                if (block > 600 && block % 10 == 0)   // the timers fire only when they're due
                {
                    juce::Thread::sleep (35);
                    juce::Timer::callPendingTimersSynchronously();
                }
            }
            processor.setPlayHead (nullptr);
        }
        const auto fx = env ("PARAGON_SNAPSHOT_FX");
        auto& fxPage = view->getFxPage();
        if (const auto bus = env ("PARAGON_SNAPSHOT_BUS").getIntValue(); bus >= 1 && bus <= 4)
            fxPage.getInsertStrip().setBus (bus - 1);
        fxPage.show (fx == "eq" ? FxPage::View::eq : fx == "dominion" ? FxPage::View::dominion : fx == "comp" ? FxPage::View::comp
                     : fx == "limiter" ? FxPage::View::limiter : fx == "sends" ? FxPage::View::sends : fx == "delay" ? FxPage::View::delay
                     : fx == "reverb" ? FxPage::View::reverb : fx == "modfx" ? FxPage::View::modFx : fx == "mod" ? FxPage::View::mod
                     : fx == "mod2" ? FxPage::View::mod2 : fx == "lofi" ? FxPage::View::loFi : fx == "inserts" ? FxPage::View::inserts
                     : fx == "insdrive" ? FxPage::View::insertDrive : fx == "insglitch" ? FxPage::View::insertGlitch
                     : fx == "insmod" ? FxPage::View::insertMod : fx == "inslofi" ? FxPage::View::insertLoFi : FxPage::View::rack);
        // A display reads its bands and analyser on its own timer, but only while it's on screen, which a snapshot
        // never is (as PR7-E's snapshots, refresh them by hand). ECHELON with its dynamic band 3 selected.
        if (fx == "eq")
        {
            fxPage.getEqPage().getDisplay().refresh();
            fxPage.getEqPage().getDisplay().select (2);
            fxPage.getEqPage().refreshControls();
            if (env ("PARAGON_SNAPSHOT_MATCH") == "1")
            {
                fxPage.getEqPage().showMatch (true);
                fxPage.getEqPage().getMatchPanel().refresh();
            }
        }
        if (fx == "dominion")
            fxPage.getDominionPage().getDisplay().refresh();
        if (fx == "comp")
            fxPage.getCompPage().getDisplay().refresh();
        if (fx == "limiter")
            fxPage.getLimiterPage().refresh();
    }
    if (env ("PARAGON_SNAPSHOT_MIDI") == "1")
    {
        // A few MIDI links on the instrument's knobs, and TRANSPOSE learning.
        MidiLinks links;
        links.link (74, ParamIDs::instRelease);
        links.link (7, ParamIDs::instLevel);
        links.link (91, ParamIDs::instSendReverb);
        processor.setMidiLinks (links);
        processor.startMidiLearn (ParamIDs::instTranspose);
        processor.controllerForTest (74, 0.6f);
    }
    if (page == "presets")
        view->showPage (MainView::Page::presets);
    if (page == "settings")
        view->showPage (MainView::Page::settings);
    view->getInstrumentPage().refresh();
    // 0.5.0: the rack's ROUTING page; the region map instead of the picture.
    if (env ("PARAGON_SNAPSHOT_ROUTING") == "1")
        view->getInstrumentPage().getRack().setPage (InstrumentRack::Page::routing);
    if (env ("PARAGON_SNAPSHOT_REGIONS") == "1")
        view->getInstrumentPage().chooseRegions (true);
    if (const auto folders = env ("PARAGON_SNAPSHOT_LIBRARY"); folders.isNotEmpty())
    {
        auto& page = view->getInstrumentPage();
        page.showLibrary (true);
        juce::Array<juce::File> list;
        for (const auto& f : juce::StringArray::fromTokens (folders, ",", {}))
            list.add (juce::File (f));
        auto& browser = page.getLibraryBrowser();
        // 0.13.0: PARAGON_SNAPSHOT_DOWNLOAD=<0-100>: VSCO 2 CE shown downloading that far (nothing fetched)
        if (const auto d = env ("PARAGON_SNAPSHOT_DOWNLOAD"); d.isNotEmpty())
        {
            processor.getDownload().showForTest (downloads::vsco(), downloads::Download::State::downloading, (float) d.getIntValue() / 100.0f);
            browser.setDownload (&processor.getDownload());
        }
        browser.setFolders (list, false);
        if (const auto s = env ("PARAGON_SNAPSHOT_LIBRARY_SECTION"); s.isNotEmpty())
            browser.chooseSection ((library::Section) s.getIntValue());
        if (const auto file = juce::File (processor.getInstrumentFile()); juce::File::isAbsolutePath (processor.getInstrumentFile()))
            browser.chooseFile (file);
        if (const auto q = env ("PARAGON_SNAPSHOT_SEARCH"); q.isNotEmpty())
            browser.setSearch (q);
    }
    if (env ("PARAGON_SNAPSHOT_ENSEMBLE") == "1")
        view->getInstrumentPage().getRack().setPage (InstrumentRack::Page::ensemble);
    if (env ("PARAGON_SNAPSHOT_STAGE") == "1")
    {
        view->getInstrumentPage().getRack().setPage (InstrumentRack::Page::stage);
        view->getInstrumentPage().chooseStage();
    }
    if (env ("PARAGON_SNAPSHOT_SWITCHES") == "1")   // (0.14.0: after STAGE, so the stage picture shows with it)
        view->getInstrumentPage().getRack().setPage (InstrumentRack::Page::articulations);

    // PARAGON_SNAPSHOT_LOADING=1: an instrument loading, the progress box over the page.
    if (env ("PARAGON_SNAPSHOT_LOADING") == "1")
        view->getProgressBox().show ({ "LOADING THE INSTRUMENT", [] { return 0.62; }, [] {} });

    const auto width = juce::jmax (960, juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_WIDTH", "1600").getIntValue());
    if (env ("PARAGON_SNAPSHOT_KEYBOARD") == "0")
        view->setKeyboardShown (false);
    editor->setSize (width, width * view->getHeight() / MainView::width);
    // The keyboard and the region map read what's sounding on their timers.
    for (int i = 0; i < 3; ++i)
    {
        juce::Thread::sleep (300);
        juce::Timer::callPendingTimersSynchronously();
    }
    editor->resized();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    REQUIRE (stream.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}

// PARAGON_SNAPSHOT=menu.png ./ParagonX1Tests "[snapshot-menu]": a drop-down
// menu as the plugin draws it (menus open in their own window, so the editor
// snapshot can't show one): INSERT BUS's choices, A ticked, one highlighted.
TEST_CASE ("Render a drop-down menu to a PNG", "[.][snapshot-menu]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT", "menu.png");
    ParagonLookAndFeel look;
    const auto& items = ParamIDs::insertBusNames;
    constexpr int scale = 2, width = 150, itemHeight = 26;
    juce::Image image (juce::Image::ARGB, width * scale, (itemHeight * items.size() + 8) * scale, true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale ((float) scale));
    look.drawPopupMenuBackground (g, width, itemHeight * items.size() + 8);
    for (int i = 0; i < items.size(); ++i)
        look.drawPopupMenuItem (g, { 0, 4 + i * itemHeight, width, itemHeight }, false, true, i == 2, items[i] == "A", false,
                                items[i], {}, nullptr, nullptr);

    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    REQUIRE (stream.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}
