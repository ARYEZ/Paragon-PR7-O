// The window: its layout, the pages and their tabs, and the on-screen keyboard.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PluginTestUtils.h"
#include "library/Library.h"
#include "dsp/mixer/MixerEffects.h"
#include "ui/MixerPage.h"
#include "NoteNames.h"
#include "ui/InstrumentPage.h"

using Catch::Matchers::WithinAbs;

namespace
{
    template <typename Type>
    void findAll (juce::Component& parent, std::vector<Type*>& found)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* match = dynamic_cast<Type*> (child))
                found.push_back (match);
            findAll (*child, found);
        }
    }

    // Every visible component sits inside its parent and has some size.
    void checkLayout (juce::Component& parent, const juce::String& path)
    {
        for (auto* child : parent.getChildren())
        {
            if (! child->isVisible() || dynamic_cast<juce::TooltipWindow*> (child) != nullptr)
                continue;
            // A list's rows are placed by JUCE beyond its visible part, for scrolling; so is a viewport's content.
            if (dynamic_cast<juce::ListBox*> (&parent) != nullptr
                || (parent.getParentComponent() != nullptr && dynamic_cast<juce::Viewport*> (parent.getParentComponent()) != nullptr))
                continue;

            const auto name = path + "/" + (child->getName().isNotEmpty() ? child->getName()
                                                                          : juce::String (typeid (*child).name()));
            INFO (name << " at " << child->getBounds().toString() << " in " << parent.getLocalBounds().toString());
            CHECK_FALSE (child->getBounds().isEmpty());
            CHECK (parent.getLocalBounds().expanded (1).contains (child->getBounds()));
            checkLayout (*child, name);
        }
    }

    float value (ParagonX1AudioProcessor& processor, const juce::String& id)
    {
        return processor.getParameters().getRawParameterValue (id)->load();
    }

    // The control showing `id` in the window, or nullptr.
    ParamControl* controlFor (juce::Component& root, const juce::String& id)
    {
        std::vector<ParamControl*> controls;
        findAll (root, controls);
        for (auto* control : controls)
            if (control->getParameterId() == id)
                return control;
        return nullptr;
    }

    template <typename Type>
    Type* inside (juce::Component& parent)
    {
        std::vector<Type*> found;
        findAll (parent, found);
        return found.empty() ? nullptr : found.front();
    }

    struct Window
    {
        Window()
        {
            editor.reset (processor.createEditor());
            editor->setSize (1600, 900);
            view = dynamic_cast<MainView*> (editor->getChildComponent (0));
            REQUIRE (view != nullptr);
        }
        ParagonX1AudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        MainView* view = nullptr;
    };
}

TEST_CASE ("The window lays out cleanly at every size", "[ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    for (const auto width : { 960, 1280, 1920 })
    {
        DYNAMIC_SECTION (width << " wide")
        {
            editor->setSize (width, width * 9 / 16);
            REQUIRE (editor->getWidth() == width);

            // The view is scaled, not re-laid-out: its transform matches the window.
            auto* view = editor->getChildComponent (0);
            REQUIRE (view != nullptr);
            CHECK_THAT (view->getTransform().mat00, WithinAbs ((float) width / (float) MainView::width, 0.001));

            checkLayout (*view, "view");
        }
    }
}

TEST_CASE ("The window opens as big as fits above the taskbar, up to 1824 x 1026", "[ui]")
{
    using Editor = ParagonX1AudioProcessorEditor;
    // 1080p with a 40-pixel taskbar: room left for the DAW's title bar and toolbar.
    const auto fullHd = Editor::openingSize ({ 1920, 1040 });
    CHECK (fullHd == juce::Point<int> (1706, 959));
    CHECK (fullHd.y + 80 <= 1040);
    CHECK (Editor::openingSize ({ 2560, 1400 }) == juce::Point<int> (1824, 1026));
    const auto laptop = Editor::openingSize ({ 1366, 728 });
    CHECK (laptop.x <= 1366 - 40);
    CHECK (laptop.y + 80 <= 728);
    CHECK (Editor::openingSize ({ 800, 600 }).x == 960);   // never below the smallest size
}

TEST_CASE ("The window opens on the INSTRUMENT page; FX, PRESETS and SETTINGS cover it; the keyboard and OUTPUT stay", "[ui]")
{
    Window w;
    auto* keyboard = inside<juce::MidiKeyboardComponent> (*w.view);
    REQUIRE (keyboard != nullptr);
    CHECK (keyboard->isVisible());   // shown when the window opens
    CHECK (keyboard->getRangeStart() == 21);
    CHECK (keyboard->getRangeEnd() == 108);
    CHECK (w.view->getPage() == MainView::Page::instrument);
    CHECK (w.view->getInstrumentPage().isVisible());
    checkLayout (*w.view, "instrument");

    // The five tabs (0.15.0: MIXER after INSTRUMENT), side by side in the header, the shown page's lit.
    std::vector<juce::Button*> tabs;
    for (const auto* id : { "instrumentTab", "mixerTab", "fxTab", "presetsTab", "settingsTab" })
    {
        auto* tab = dynamic_cast<juce::Button*> (w.view->findChildWithID (id));
        INFO (id);
        REQUIRE (tab != nullptr);
        CHECK (tab->getBottom() < 64);
        CHECK (tab->getWidth() > 135);   // (five across the group: wide enough for INSTRUMENT with its icon)
        if (! tabs.empty())
            CHECK (tab->getX() >= tabs.back()->getRight());
        tabs.push_back (tab);
    }
    CHECK (tabs[0]->getToggleState());

    const std::pair<MainView::Page, int> pages[] = { { MainView::Page::mixer, 1 }, { MainView::Page::fx, 2 }, { MainView::Page::presets, 3 },
                                                     { MainView::Page::settings, 4 } };
    for (const auto& [page, tab] : pages)
    {
        INFO (tab);
        w.view->showPage (page);
        CHECK_FALSE (w.view->getInstrumentPage().isVisible());
        CHECK (keyboard->isVisible());
        CHECK (tabs[(size_t) tab]->getToggleState());
        checkLayout (*w.view, "page");
    }
    CHECK (w.view->getFxPage().isVisible() == false);   // SETTINGS shown last
    tabs[0]->onClick();   // as clicking INSTRUMENT does
    CHECK (w.view->getInstrumentPage().isVisible());
}

TEST_CASE ("SHOW / HIDE KEYBOARD: the window opens with the keyboard; hidden, it's shorter", "[ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    CHECK (view->isKeyboardShown());
    CHECK (view->getHeight() == MainView::height);
    const auto width = editor->getWidth();
    CHECK (std::abs (editor->getHeight() - width * 900 / MainView::width) <= 1);

    juce::TextButton* button = nullptr;
    std::vector<juce::TextButton*> buttons;
    findAll (*view, buttons);
    for (auto* b : buttons)
        if (b->getButtonText() == "HIDE\nKEYBOARD")
            button = b;
    REQUIRE (button != nullptr);
    CHECK (button->isVisible());
    CHECK (button->getX() > 1480);

    button->onClick();
    CHECK_FALSE (view->isKeyboardShown());
    CHECK_FALSE (view->getKeyboard().isVisible());
    CHECK (button->getButtonText() == "SHOW\nKEYBOARD");
    CHECK (view->getHeight() == MainView::heightFor (false));
    CHECK (MainView::heightFor (false) == 752);
    CHECK (editor->getWidth() == width);
    CHECK (std::abs (editor->getHeight() - width * 752 / MainView::width) <= 1);
    checkLayout (*view, "hidden");
    CHECK (button->getBottom() <= view->getHeight());

    button->onClick();
    CHECK (view->getKeyboard().isVisible());
    CHECK (view->getHeight() == MainView::height);
    checkLayout (*view, "shown");
}

TEST_CASE ("Notes from the keyboard play the instrument; its keys are where it plays", "[ui]")
{
    Window w;
    w.processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    w.processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    w.processor.processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 256) == 0.0f);   // silence: no instrument, no key

    // An instrument on 48 to 72 only.
    auto program = sampler::programFrom (testkit::sine (220.0), 60);
    program->regions[0].loKey = 48;
    program->regions[0].hiKey = 72;
    program->index();
    w.processor.setProgram (std::move (program));
    CHECK (w.processor.getProgram()->plays (60));
    CHECK_FALSE (w.processor.getProgram()->plays (80));

    w.processor.getKeyboardState().noteOn (1, 62, 0.9f);   // as clicking the key does
    for (int block = 0; block < 4; ++block)
        w.processor.processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 256) > 0.05f);
    CHECK (w.processor.getSampler().getActiveVoices() == 1);
    CHECK (w.processor.getSampler().isKeyHeld (62));
    w.processor.getKeyboardState().noteOff (1, 62, 0.0f);
    for (int block = 0; block < 40; ++block)
        w.processor.processBlock (buffer, midi);
    CHECK (w.processor.getSampler().getActiveVoices() == 0);   // released

    w.processor.getKeyboardState().noteOn (1, 80, 0.9f);   // outside its keys: nothing
    for (int block = 0; block < 10; ++block)
        w.processor.processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 256) == 0.0f);
    CHECK (w.processor.getSampler().getActiveVoices() == 0);
}

TEST_CASE ("THE INSTRUMENT page: empty, then what an instrument holds; LOAD and CLEAR", "[ui]")
{
    Window w;
    auto& page = w.view->getInstrumentPage();
    CHECK (page.getInfoText().contains ("SFZ"));
    CHECK_FALSE (page.getClearButton().isEnabled());

    auto program = std::make_shared<sampler::Program>();
    program->name = "Violins";
    for (int layer = 0; layer < 3; ++layer)
        for (int rr = 1; rr <= 2; ++rr)
        {
            sampler::Region r;
            r.sample = testkit::sine (220.0, 0.2);
            r.loKey = 55;
            r.hiKey = 96;
            r.loVel = 1 + layer * 43;
            r.hiVel = layer == 2 ? 127 : 43 + layer * 43;
            r.seqLength = 2;
            r.seqPosition = rr;
            program->regions.push_back (r);
        }
    program->unread.add ("loop_mode=loop_continuous (6)");
    program->numFiles = 6;
    program->index();
    w.processor.setProgram (program, "/libraries/violins.sfz");
    page.refresh();
    const auto info = page.getInfoText();
    INFO (info);
    CHECK (info.contains ("REGIONS   6"));
    CHECK (info.contains ("3 velocity layers, 2 round robins"));
    CHECK (info.contains ("loop_mode=loop_continuous"));
    CHECK (page.getClearButton().isEnabled());
    page.getClearButton().onClick();
    CHECK (w.processor.getProgram() == nullptr);
    CHECK (w.processor.getInstrumentFile().isEmpty());
    CHECK (InstrumentPage::isInstrumentFile (juce::File ("/x/strings.sfz")));
    CHECK (InstrumentPage::isInstrumentFile (juce::File ("/x/cello.WAV")));
    CHECK_FALSE (InstrumentPage::isInstrumentFile (juce::File ("/x/notes.txt")));
}

namespace
{
    bool inFamily (juce::Colour c)
    {
        // Greys, near black and near white (onyx, pearl) or a hue from pink (318) through red to orange (42).
        const auto hue = c.getHue() * 360.0f;
        return c.getSaturation() < 0.25f || c.getBrightness() < 0.15f || (c.getSaturation() < 0.12f && c.getBrightness() > 0.85f)
            || hue >= 318.0f || hue <= 42.0f;
    }
}

TEST_CASE ("COLOURS: the look-and-feel's, the keyboard's, the region map's and every pixel of the INSTRUMENT page are the family's", "[colours][ui]")
{
    ParagonLookAndFeel look;
    for (const int id : { (int) juce::Slider::thumbColourId, (int) juce::Slider::trackColourId, (int) juce::Slider::rotarySliderFillColourId,
                           (int) juce::ToggleButton::tickColourId, (int) juce::TextEditor::highlightColourId, (int) juce::ScrollBar::thumbColourId,
                           (int) juce::ComboBox::arrowColourId, (int) juce::ListBox::backgroundColourId, (int) juce::TreeView::selectedItemBackgroundColourId })
    {
        INFO ("colour id " << juce::String::toHexString (id));
        CHECK (inFamily (look.findColour (id)));
    }
    for (const auto c : { ParagonColours::comp, ParagonColours::modulation, ParagonColours::delay, ParagonColours::reverb,
                          ParagonColours::filterPitch, ParagonColours::glitch, ParagonColours::pitch, ParagonColours::loFi })
        CHECK (inFamily (c));
    for (int v = 1; v <= 127; ++v)   // the region map's boxes, by velocity
        CHECK (inFamily (ParagonColours::velocityColour ((float) v / 127.0f)));

    // The page with an instrument and a chord sounding, drawn: no pixel outside the family. 0.5.0: also with each
    // string instrument's picture (VSCO 2 CE's names) and a section's (its parts lit, notes on their strings).
    Window w;
    w.processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    w.processor.prepareToPlay (48000.0, 256);
    const auto scan = [&w] (std::initializer_list<int> notes, const juce::String& what)
    {
        w.view->getInstrumentPage().refresh();
        for (const auto note : notes)
            w.processor.getKeyboardState().noteOn (1, note, 0.8f);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (int block = 0; block < 10; ++block)
            w.processor.processBlock (buffer, midi);
        juce::Thread::sleep (300);
        juce::Timer::callPendingTimersSynchronously();
        const auto image = w.editor->createComponentSnapshot (w.editor->getLocalBounds());
        auto outside = 0;
        juce::String examples;
        for (int y = 0; y < image.getHeight(); y += 2)
            for (int x = 0; x < image.getWidth(); x += 2)
                if (const auto c = image.getPixelAt (x, y); ! inFamily (c))
                {
                    if (++outside <= 6)
                        examples << " (" << x << "," << y << ") #" << c.toDisplayString (false) << " hue " << juce::roundToInt (c.getHue() * 360.0f)
                                 << " sat " << juce::String (c.getSaturation(), 2) << " bri " << juce::String (c.getBrightness(), 2) << ";";
                }
        INFO (what << ": " << outside << " pixels outside the family's colours:" << examples);
        CHECK (outside == 0);
        w.processor.getKeyboardState().allNotesOff (1);
        for (int block = 0; block < 4; ++block)
            w.processor.processBlock (buffer, midi);
    };
    for (const auto* name : { "a", "ViolinEns-KS", "ViolaEns-KS", "CelloEns-KS", "Contrabass-KS" })
    {
        auto program = sampler::programFrom (testkit::sine (220.0), 60);
        program->name = name;
        program->regions[0].loKey = 28;
        program->regions[0].hiKey = 96;
        program->index();
        w.processor.setProgram (std::move (program), juce::String ("/libraries/") + name + ".sfz");
        scan ({ 48, 55, 64, 76 }, name);
    }
    CHECK (w.view->getInstrumentPage().isShowingPicture());   // (the last: a picture)

    juce::TemporaryFile folder;
    const auto dir = folder.getFile();
    dir.createDirectory();
    testkit::writeWav (dir.getChildFile ("t.wav"), *testkit::sine (220.0, 1.0), 24);
    juce::StringArray files;
    for (const auto& [name, low, high] : { std::tuple { "Contrabass", 28, 60 }, std::tuple { "CelloEns", 36, 77 },
                                           std::tuple { "ViolaEns", 48, 86 }, std::tuple { "ViolinEns", 55, 96 } })
    {
        dir.getChildFile (juce::String (name) + ".sfz").replaceWithText ("<region> sample=t.wav lokey=" + juce::String (low) + " hikey="
                                                                         + juce::String (high) + " pitch_keycenter=60\n");
        files.add (dir.getChildFile (juce::String (name) + ".sfz").getFullPathName());
    }
    w.processor.loadSection (sampler::section::fromFiles (files));
    REQUIRE (w.processor.waitForInstrument());
    scan ({ 30, 40, 50, 70 }, "the section");
    CHECK (w.view->getInstrumentPage().getOrchestraView().getMode() == OrchestraView::Mode::section);
}

TEST_CASE ("THE INSTRUMENT page 0.2.0: an instrument's articulations, named as the keyboard names keys; clicking switches; the map shows the one chosen", "[ui][keyswitch]")
{
    Window w;
    w.processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    w.processor.prepareToPlay (48000.0, 256);
    auto program = std::make_shared<sampler::Program>();
    for (const auto sw : { 36, 37 })
        for (int rr = 1; rr <= (sw == 36 ? 1 : 3); ++rr)
        {
            sampler::Region r;
            r.sample = testkit::sine (220.0, 0.3);
            r.loKey = 55;
            r.hiKey = 70;
            r.swLoLast = r.swHiLast = sw;
            r.seqLength = sw == 36 ? 1 : 3;
            r.seqPosition = rr;
            program->regions.push_back (r);
        }
    program->swLoKey = 36;
    program->swHiKey = 39;
    program->swDefault = 36;
    program->articulations = { { 36, 36, "C2 Sustain Vibrato" }, { 37, 37, "C#2 Spiccato" } };   // labels in SFZ's naming
    sampler::prepareRegions (*program);
    program->index();
    w.processor.setProgram (program, "/libraries/ks.sfz");
    auto& page = w.view->getInstrumentPage();
    page.refresh();
    REQUIRE (page.getNumArticulations() == 2);
    notenames::middleCOctave = 3;   // NOTE NAMES: C3 (Cubase): note 36 is C1
    CHECK (articulationName (program->articulations[0]) == "C1 Sustain Vibrato");
    notenames::middleCOctave = 4;
    CHECK (articulationName (program->articulations[0]) == "C2 Sustain Vibrato");
    notenames::middleCOctave = 3;
    CHECK (articulationName ({ 40, 40, "Legato" }) == "Legato");   // a label without its key's name stays as it is
    // Clicking the second switches (at the next block).
    page.getArticulationButton (1).onClick();
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    w.processor.processBlock (buffer, midi);
    CHECK (w.processor.getKeyswitch() == 37);
    juce::Thread::sleep (300);
    juce::Timer::callPendingTimersSynchronously();
    CHECK (page.getArticulationButton (1).getToggleState());
    CHECK_FALSE (page.getArticulationButton (0).getToggleState());
    CHECK (page.getInfoText().contains ("3 round robins"));
    // Its keyswitch is kept with the project.
    juce::MemoryBlock state;
    w.processor.getStateInformation (state);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    REQUIRE (xml != nullptr);
    const auto* instrument = xml->getChildByName ("Instrument");
    REQUIRE (instrument != nullptr);
    CHECK (instrument->getIntAttribute ("keyswitch") == 37);
}

TEST_CASE ("THE INSTRUMENT page 0.4.0: a section's SPLITS, a row a part; - / + move a split a semitone, each part keeping a key", "[ui][section]")
{
    juce::TemporaryFile folder;
    const auto dir = folder.getFile();
    dir.createDirectory();
    testkit::writeWav (dir.getChildFile ("t.wav"), *testkit::sine (220.0, 0.3), 24);
    dir.getChildFile ("Basses.sfz").replaceWithText ("<region> sample=t.wav lokey=28 hikey=55 pitch_keycenter=40\n");
    dir.getChildFile ("Cellos.sfz").replaceWithText ("<region> sample=t.wav lokey=36 hikey=70 pitch_keycenter=48\n");
    dir.getChildFile ("Violins.sfz").replaceWithText ("<region> sample=t.wav lokey=55 hikey=96 pitch_keycenter=69\n");
    Window w;
    auto& page = w.view->getInstrumentPage();
    CHECK (page.getNumSplitRows() == 0);
    CHECK (page.getNewSectionButton().isVisible());
    CHECK (page.isInterestedInFileDrag ({ dir.getChildFile ("Basses.sfz").getFullPathName(), dir.getChildFile ("Cellos.sfz").getFullPathName() }));
    CHECK_FALSE (page.isInterestedInFileDrag ({ dir.getChildFile ("Basses.sfz").getFullPathName(), dir.getChildFile ("t.wav").getFullPathName() }));

    // Dropped together: a section, its parts low to high.
    page.filesDropped ({ dir.getChildFile ("Violins.sfz").getFullPathName(), dir.getChildFile ("Basses.sfz").getFullPathName(),
                         dir.getChildFile ("Cellos.sfz").getFullPathName() }, 0, 0);
    REQUIRE (w.processor.waitForInstrument());
    page.refresh();
    REQUIRE (page.getNumSplitRows() == 3);
    CHECK (page.getSplitRowText (0) == "BASSES " + notenames::name (28) + " to " + notenames::name (35));
    CHECK (page.getSplitRowText (1) == "CELLOS " + notenames::name (36) + " to " + notenames::name (54));
    CHECK (page.getSplitRowText (2) == "VIOLINS " + notenames::name (55) + " to " + notenames::name (96));

    // The split under the violins up 2 (the cellos play 2 keys more), shown at once, loaded after a moment.
    CHECK (page.moveSplit (2, 1));
    CHECK (page.moveSplit (2, 1));
    CHECK (page.getSplitRowText (1).endsWith (notenames::name (56)));
    CHECK (w.processor.getSection()->parts[1].high == 54);   // not yet
    page.applyPendingSplits();
    REQUIRE (w.processor.waitForInstrument());
    CHECK (w.processor.getSection()->parts[1].high == 56);
    CHECK (w.processor.getSection()->parts[2].low == 57);
    // Each part keeps a key: the basses' split can't go under their lowest, nor the first row move at all.
    CHECK_FALSE (page.moveSplit (0, 1));
    for (int i = 0; i < 7; ++i)
        page.moveSplit (1, -1);
    CHECK_FALSE (page.moveSplit (1, -1));
    CHECK (page.getSplitRowText (0).endsWith (notenames::name (28)));   // the basses down to one key
    page.applyPendingSplits();
    REQUIRE (w.processor.waitForInstrument());
    CHECK (w.processor.getSection()->parts[0].high == 28);

    // The page's name for it: its parts'.
    page.refresh();
    CHECK (page.getInfoText().contains ("REGIONS"));
    CHECK (w.processor.getProgram()->name == "Basses + Cellos + Violins");
}

TEST_CASE ("THE INSTRUMENT page 0.14.0: articulations and splits on the rack's ARTICULATIONS page, plain part names, the panel the page's width", "[ui][section]")
{
    // The owner: "the keyswitch box ... can become a 2nd page in the rack unit ... we can stretch out the tron display of
    // our stage and use all the space"; then "show plain section names".
    CHECK (library::partLabel ("ViolinEns-KS") == "Violins");
    CHECK (library::partLabel ("ViolaEns-KS") == "Violas");
    CHECK (library::partLabel ("CelloEns-KS") == "Cellos");
    CHECK (library::partLabel ("Contrabass-KS") == "Basses");
    CHECK (library::partLabel ("1st Violins KS") == "1st Violins");
    CHECK (library::partLabel ("Mystery Pad-KS") == "Mystery Pad");   // a name the table doesn't know: itself, no "-KS"

    juce::TemporaryFile folder;
    const auto dir = folder.getFile();
    dir.createDirectory();
    testkit::writeWav (dir.getChildFile ("t.wav"), *testkit::sine (220.0, 0.3), 24);
    const juce::String switches = "<group> sw_lokey=96 sw_hikey=97 sw_default=96 sw_last=96 sw_label=c7 Sustain\n<region> sample=t.wav lokey=%lo hikey=%hi\n"
                                  "<group> sw_lokey=96 sw_hikey=97 sw_last=97 sw_label=c#7 Pizzicato\n<region> sample=t.wav lokey=%lo hikey=%hi\n";
    dir.getChildFile ("Contrabass-KS.sfz").replaceWithText (switches.replace ("%lo", "28").replace ("%hi", "55"));
    dir.getChildFile ("ViolinEns-KS.sfz").replaceWithText (switches.replace ("%lo", "55").replace ("%hi", "90"));
    Window w;
    auto& page = w.view->getInstrumentPage();
    auto& rack = page.getRack();
    page.filesDropped ({ dir.getChildFile ("ViolinEns-KS.sfz").getFullPathName(), dir.getChildFile ("Contrabass-KS.sfz").getFullPathName() }, 0, 0);
    REQUIRE (w.processor.waitForInstrument());
    page.refresh();

    // The articulations: keys on the rack, shown on its ARTICULATIONS page only (the SWITCHES key), inside the unit.
    REQUIRE (page.getNumArticulations() == 2);
    REQUIRE (page.getNumSplitRows() == 2);
    auto& sustain = page.getArticulationButton (0);
    CHECK (sustain.getParentComponent() == &rack);
    rack.setPage (InstrumentRack::Page::playing);
    CHECK_FALSE (sustain.isVisible());
    rack.getPageKey (InstrumentRack::Page::articulations).onClick();
    CHECK (rack.getPage() == InstrumentRack::Page::articulations);
    CHECK (sustain.isVisible());
    CHECK (page.getArticulationButton (1).isVisible());
    CHECK (rack.getLocalBounds().contains (sustain.getBounds()));
    CHECK (rack.getLocalBounds().contains (page.getArticulationButton (1).getBounds()));
    CHECK (sustain.getRight() < page.getArticulationButton (1).getX());   // side by side

    // The splits, with plain names (no "-KS", the section's word), on the same page.
    CHECK (page.getSplitRowText (0).startsWith ("BASSES "));
    CHECK (page.getSplitRowText (1).startsWith ("VIOLINS "));
    rack.setPage (InstrumentRack::Page::stage);
    rack.setPage (InstrumentRack::Page::articulations);

    // No left panel: the picture panel (here the stage) runs the page's width; the info text goes with REGIONS.
    page.chooseStage();
    CHECK (page.getStageView().getWidth() > page.getWidth() - 20);
    page.chooseRegions (true);
    CHECK (page.getInfoText().contains ("REGIONS"));
}

TEST_CASE ("THE MIXER tab 0.15.0: a strip per part and the section's; its keys, fader, knobs and effect editor set the processor's strips; family colours", "[ui][mixer]")
{
    juce::TemporaryFile folder;
    const auto dir = folder.getFile();
    dir.createDirectory();
    testkit::writeWav (dir.getChildFile ("t.wav"), *testkit::sine (220.0, 0.3), 24);
    dir.getChildFile ("CelloEns-KS.sfz").replaceWithText ("<region> sample=t.wav lokey=36 hikey=59\n");
    dir.getChildFile ("ViolinEns-KS.sfz").replaceWithText ("<region> sample=t.wav lokey=60 hikey=90\n");
    Window w;
    auto& page = w.view->getMixerPage();
    page.refresh();
    CHECK (page.getNumStrips() == 0);   // nothing loaded: no strips
    w.processor.loadSection (sampler::section::fromFiles ({ dir.getChildFile ("CelloEns-KS.sfz").getFullPathName(),
                                                            dir.getChildFile ("ViolinEns-KS.sfz").getFullPathName() }));
    REQUIRE (w.processor.waitForInstrument());
    juce::Timer::callPendingTimersSynchronously();
    page.refresh();
    juce::Timer::callPendingTimersSynchronously();
    w.view->showPage (MainView::Page::mixer);
    CHECK (page.isVisible());
    REQUIRE (page.getNumStrips() == 3);
    CHECK (page.getStripName (0) == "CELLOS");   // plain names (library::partLabel)
    CHECK (page.getStripName (1) == "VIOLINS");
    CHECK (page.getStripName (2) == "SECTION");
    for (int i = 0; i < 3; ++i)
        CHECK (page.getLocalBounds().contains (page.getStripBounds (i)));

    // The keys: MUTE on the violins' strip, SOLO on the cellos'; the section has no SOLO.
    page.getKey (1, MixerPage::Key::mute).onClick();
    CHECK (w.processor.getStrip ("ViolinEns-KS").mute);
    page.getKey (0, MixerPage::Key::solo).onClick();
    CHECK (w.processor.getStrip ("CelloEns-KS").solo);
    CHECK_FALSE (page.getKey (2, MixerPage::Key::solo).isVisible());
    // The fader and a knob.
    page.moveFader (0, -7.5f);
    CHECK (std::abs (w.processor.getStrip ("CelloEns-KS").levelDb + 7.5f) < 1.0e-4f);
    page.getPanKnob (2).setValue (0.4, juce::sendNotificationSync);
    CHECK (std::abs (w.processor.getSectionStrip().pan - 0.4f) < 1.0e-4f);

    // An effect chosen into a slot (as its list does), its editor, a knob there, ON, REMOVE.
    page.chooseEffect (1, 0, mixerfx::eq);
    CHECK (w.processor.getStrip ("ViolinEns-KS").fx[0].type == mixerfx::eq);
    page.openEditor (1, 0);
    REQUIRE (page.isEditorOpen());
    REQUIRE (page.getEditorKnobCount() == (int) mixerfx::paramsOf (mixerfx::eq).size() + 1);   // its settings and OUTPUT
    page.getEditorKnob (0).setValue (400.0, juce::sendNotificationSync);   // LOW CUT
    CHECK (std::abs (w.processor.getStrip ("ViolinEns-KS").fx[0].values[0] - 400.0f) < 0.5f);
    page.getEffectKey (1, 0).onClick();   // the slot's key: off
    CHECK_FALSE (w.processor.getStrip ("ViolinEns-KS").fx[0].on);

    // Shown again after a change elsewhere (an undo): the strip follows.
    auto cellos = w.processor.getStrip ("CelloEns-KS");
    cellos.mute = true;
    w.processor.setStrip ("CelloEns-KS", cellos);
    juce::Timer::callPendingTimersSynchronously();
    page.refresh();
    CHECK (page.getKey (0, MixerPage::Key::mute).getToggleState());

    // Every pixel of the page (the strips, then the editor over them) in the family's colours.
    for (const auto editorOpen : { false, true })
    {
        if (! editorOpen)
            page.closeEditor();
        else
            page.openEditor (1, 0);
        const auto image = w.editor->createComponentSnapshot (w.editor->getLocalBounds());
        auto outside = 0;
        juce::String examples;
        for (int y = 0; y < image.getHeight(); y += 2)
            for (int x = 0; x < image.getWidth(); x += 2)
                if (const auto c = image.getPixelAt (x, y); ! inFamily (c) && ++outside <= 6)
                    examples << " (" << x << "," << y << ") #" << c.toDisplayString (false) << " hue " << juce::roundToInt (c.getHue() * 360.0f) << ";";
        INFO ((editorOpen ? "editor open" : "strips") << ": " << outside << " pixels outside:" << examples);
        CHECK (outside == 0);
    }
    page.removeEffectForTest (1, 0);
    CHECK_FALSE (w.processor.getStrip ("ViolinEns-KS").fx[0].isLoaded());
    CHECK_FALSE (page.isEditorOpen());
}

TEST_CASE ("THE ORCHESTRA VIEW 0.5.0: which instrument a file is; each note's string, as a player in low position stops it", "[ui][orchestra]")
{
    using orchestra::Kind;
    // VSCO 2 CE's names, and others.
    CHECK (orchestra::kindOf ("ViolinEns-KS") == Kind::violin);
    CHECK (orchestra::kindOf ("SViolinVib") == Kind::violin);
    CHECK (orchestra::kindOf ("ViolaEnsSusVib") == Kind::viola);
    CHECK (orchestra::kindOf ("CelloEns-KS") == Kind::cello);
    CHECK (orchestra::kindOf ("Contrabass-KS") == Kind::bass);
    CHECK (orchestra::kindOf ("Double Bass Pizz") == Kind::bass);
    CHECK (orchestra::kindOf ("BassoonSus") == Kind::bassoon);     // 0.12.0: the woodwinds
    CHECK (orchestra::kindOf ("TromboneSus") == Kind::trombone);   // 0.6.0: the brass
    CHECK (orchestra::kindOf ("GM-StylePerc bassdrum") == Kind::none);
    CHECK (orchestra::kindOf ("FluteSusVib") == Kind::flute);

    // A cello: C2 G2 D3 A3. The snapshot's chord C2 G2 E3 C4: two open strings, E3 two up the D, C4 three up the A.
    auto stops = orchestra::stopsFor (Kind::cello, { 36, 43, 52, 60 });
    std::sort (stops.begin(), stops.end(), [] (const auto& a, const auto& b) { return a.note < b.note; });
    REQUIRE (stops.size() == 4);
    CHECK ((stops[0].string == 0 && stops[0].semitones == 0));
    CHECK ((stops[1].string == 1 && stops[1].semitones == 0));
    CHECK ((stops[2].string == 2 && stops[2].semitones == 2));
    CHECK ((stops[3].string == 3 && stops[3].semitones == 3));
    // Two notes over one string: the lower moves down a string (A3 and B3 on a cello: B3 on the A string, A3 on the D).
    stops = orchestra::stopsFor (Kind::cello, { 57, 59 });
    CHECK ((stops[0].note == 59 && stops[0].string == 3 && stops[0].semitones == 2));
    CHECK ((stops[1].note == 57 && stops[1].string == 2 && stops[1].semitones == 7));
    // Under the lowest string (VSCO's basses reach C1, an extension): the lowest string, open. High up: to the
    // fingerboard's end at most.
    stops = orchestra::stopsFor (Kind::bass, { 24 });
    CHECK ((stops[0].string == 0 && stops[0].semitones == 0));
    stops = orchestra::stopsFor (Kind::violin, { 120 });
    CHECK ((stops[0].string == 3 && stops[0].semitones == 28));
    // Five notes on four strings: one has none.
    stops = orchestra::stopsFor (Kind::violin, { 55, 62, 69, 76, 80 });
    CHECK (std::count_if (stops.begin(), stops.end(), [] (const auto& s) { return s.string < 0; }) == 1);
}

TEST_CASE ("THE INSTRUMENT page 0.5.0: PR7-I's rack holds its knobs, menus and keys; the picture for strings, the region map otherwise", "[ui][orchestra]")
{
    Window w;
    auto& page = w.view->getInstrumentPage();
    auto& rack = page.getRack();
    // The knobs, menus and keys are the rack's, in its cells; PLAYING shows the playing ones, ROUTING the rest.
    CHECK (page.getLoadButton().getParentComponent() == &rack);
    CHECK (page.getNewSectionButton().getParentComponent() == &rack);
    CHECK (page.getClearButton().getParentComponent() == &rack);
    const auto shownOnRack = [&rack]
    {
        juce::StringArray names;
        for (auto* c : rack.getChildren())
            if (auto* control = dynamic_cast<ParamControl*> (c); control != nullptr && control->isVisible())
                names.add (control->getParameterId());
        return names;
    };
    CHECK (rack.getPage() == InstrumentRack::Page::playing);
    CHECK (shownOnRack() == juce::StringArray { ParamIDs::instLevel, ParamIDs::instPan, ParamIDs::instTranspose, ParamIDs::instTune,
                                                ParamIDs::instRelease, ParamIDs::instVoices, ParamIDs::instGlide, ParamIDs::instDynamics });
    rack.getPageKey (InstrumentRack::Page::routing).onClick();
    CHECK (rack.getPage() == InstrumentRack::Page::routing);
    CHECK (shownOnRack() == juce::StringArray { ParamIDs::instSendDelay, ParamIDs::instSendReverb, ParamIDs::instInsert });
    for (auto* c : rack.getChildren())
        if (c->isVisible())
        {
            INFO (c->getName() << " " << c->getBounds().toString());
            CHECK (rack.getLocalBounds().contains (c->getBounds()));
        }
    rack.setPage (InstrumentRack::Page::playing);
    CHECK (rack.getScreen().name == "NO INSTRUMENT");

    // A cello: its picture, the screen naming it; REGIONS shows the map instead.
    const auto make = [] (const juce::String& name, int low, int high)
    {
        auto program = std::make_shared<sampler::Program>();
        program->name = name;
        sampler::Region r;
        r.sample = testkit::sine (220.0, 0.2);
        r.loKey = low;
        r.hiKey = high;
        program->regions.push_back (r);
        program->index();
        return program;
    };
    w.processor.setProgram (make ("CelloEns", 36, 76), "/libraries/CelloEns.sfz");
    page.refresh();
    CHECK (page.getOrchestraView().getMode() == OrchestraView::Mode::single);
    CHECK (page.getOrchestraView().getKind() == orchestra::Kind::cello);
    CHECK (page.isShowingPicture());
    CHECK (rack.getScreen().name == "CELLOENS");
    CHECK (rack.getScreen().line2 == "/libraries/CelloEns.sfz");
    page.chooseRegions (true);
    CHECK_FALSE (page.isShowingPicture());
    page.chooseRegions (false);
    CHECK (page.isShowingPicture());

    // Timpani: no picture (not modelled yet; 0.12.0: the flute has one now): the region map.
    w.processor.setProgram (make ("Timpani-Rolls", 40, 60), "/libraries/Timpani-Rolls.sfz");
    page.refresh();
    CHECK (page.getOrchestraView().getMode() == OrchestraView::Mode::none);
    CHECK_FALSE (page.isShowingPicture());

    // AUTO SUSTAIN's key follows the parameter and sets it.
    auto* autoSustain = w.processor.getParameters().getParameter (ParamIDs::instAutoSustain);
    const auto before = autoSustain->getValue() > 0.5f;
    for (auto* c : rack.getChildren())
        if (auto* key = dynamic_cast<juce::TextButton*> (c); key != nullptr && key->getTooltip().startsWith ("AUTO SUSTAIN"))
        {
            CHECK (key->getToggleState() == before);
            key->onClick();
        }
    CHECK ((autoSustain->getValue() > 0.5f) != before);

    // 0.7.0: LEGATO's key beside it, off by default, sets its parameter; GLIDE a seventh knob, inside the rack.
    auto* legato = w.processor.getParameters().getParameter (ParamIDs::instLegato);
    CHECK (legato->getValue() < 0.5f);
    auto legatoKeys = 0;
    for (auto* c : rack.getChildren())
        if (auto* key = dynamic_cast<juce::TextButton*> (c); key != nullptr && key->getTooltip().startsWith ("LEGATO"))
        {
            ++legatoKeys;
            CHECK (key->isVisible());
            CHECK_FALSE (key->getToggleState());
            CHECK_FALSE (key->getBounds().intersects (rack.pageKeyCell (0)));
            key->onClick();
            CHECK (key->getToggleState());
        }
    CHECK (legatoKeys == 1);
    CHECK (legato->getValue() > 0.5f);
    CHECK (rack.getLocalBounds().contains (rack.knobCell (InstrumentRack::numKnobs - 1)));
    CHECK (w.processor.getParameters().getParameter (ParamIDs::instGlide)->getCurrentValueAsText() == "Off");
}

TEST_CASE ("THE ORCHESTRA VIEW 0.5.0: a section's parts glow while they play, their notes on their strings", "[ui][orchestra][section]")
{
    juce::TemporaryFile folder;
    const auto dir = folder.getFile();
    dir.createDirectory();
    testkit::writeWav (dir.getChildFile ("t.wav"), *testkit::sine (220.0, 2.0), 24);
    dir.getChildFile ("Contrabass.sfz").replaceWithText ("<region> sample=t.wav lokey=24 hikey=60 pitch_keycenter=40\n");
    dir.getChildFile ("CelloEns.sfz").replaceWithText ("<region> sample=t.wav lokey=36 hikey=77 pitch_keycenter=48\n");
    dir.getChildFile ("Horns.sfz").replaceWithText ("<region> sample=t.wav lokey=55 hikey=80 pitch_keycenter=69\n");
    Window w;
    auto& page = w.view->getInstrumentPage();
    w.processor.loadSection (sampler::section::fromFiles ({ dir.getChildFile ("Contrabass.sfz").getFullPathName(),
                                                            dir.getChildFile ("CelloEns.sfz").getFullPathName(),
                                                            dir.getChildFile ("Horns.sfz").getFullPathName() }));
    REQUIRE (w.processor.waitForInstrument());
    page.refresh();
    auto& view = page.getOrchestraView();
    CHECK (view.getMode() == OrchestraView::Mode::section);   // two string parts, one brass: the strings shown, the horns unpictured
    CHECK_FALSE (view.showsBrass());
    CHECK (page.isShowingPicture());
    CHECK (w.processor.getInstrumentFile().contains ("Horns.sfz"));

    // A cello note (in the cellos' split, 36-54), played into the processor: the cello glows, the bass doesn't.
    w.processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    w.processor.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100), 0);
    w.processor.processBlock (buffer, midi);
    juce::Timer::callPendingTimersSynchronously();
    juce::Thread::sleep (60);
    juce::Timer::callPendingTimersSynchronously();
    CHECK (view.getGlow (orchestra::Kind::cello) > 0.9f);
    CHECK (view.getGlow (orchestra::Kind::bass) == 0.0f);
    const auto stops = view.getStops (orchestra::Kind::cello);
    REQUIRE (stops.size() == 1);
    CHECK ((stops[0].note == 45 && stops[0].string == 1 && stops[0].semitones == 2));   // A2: two up the G string
    CHECK (view.getStops (orchestra::Kind::bass).empty());
    // TRANSPOSE moves where it's stopped (+5: D3, the D string open).
    testkit::set (w.processor, ParamIDs::instTranspose, 5.0f);
    juce::Thread::sleep (60);
    juce::Timer::callPendingTimersSynchronously();
    const auto moved = view.getStops (orchestra::Kind::cello);
    REQUIRE (moved.size() == 1);
    CHECK ((moved[0].note == 50 && moved[0].string == 2 && moved[0].semitones == 0));
}

TEST_CASE ("THE ORCHESTRA VIEW 0.5.0: every picture's colours sit inside 322-38 degrees, so any renderer's scaling stays in the family", "[ui][orchestra][colours]")
{
    // Blending colours inside an arc this narrow (76 degrees) gives a colour inside it, however a renderer scales the
    // picture (0.5.0's first Windows build blended a nearly grey lilac pixel into a pink one: 317 degrees).
    std::vector<std::pair<juce::String, juce::Image>> pictures;
    for (const auto k : { orchestra::Kind::violin, orchestra::Kind::viola, orchestra::Kind::cello, orchestra::Kind::bass })
        pictures.push_back ({ orchestra::nameOf (k), OrchestraView::getPicture (k) });
    pictures.push_back ({ "section", OrchestraView::getSectionPicture (false) });
    pictures.push_back ({ "section lit", OrchestraView::getSectionPicture (true) });
    for (const auto k : { orchestra::Kind::trumpet, orchestra::Kind::horn, orchestra::Kind::trombone, orchestra::Kind::tuba })
        pictures.push_back ({ orchestra::nameOf (k), OrchestraView::getPicture (k) });
    pictures.push_back ({ "brass section", OrchestraView::getSectionPicture (false, true) });
    pictures.push_back ({ "brass section lit", OrchestraView::getSectionPicture (true, true) });
    for (const auto k : { orchestra::Kind::flute, orchestra::Kind::piccolo, orchestra::Kind::oboe, orchestra::Kind::englishHorn,
                          orchestra::Kind::clarinet, orchestra::Kind::bassClarinet, orchestra::Kind::bassoon, orchestra::Kind::contrabassoon })
        pictures.push_back ({ orchestra::nameOf (k), OrchestraView::getPicture (k) });
    pictures.push_back ({ "woodwind section", OrchestraView::getSectionPicture (false, orchestra::Family::woodwinds) });
    pictures.push_back ({ "woodwind section lit", OrchestraView::getSectionPicture (true, orchestra::Family::woodwinds) });
    for (const auto& [name, image] : pictures)
    {
        REQUIRE (image.isValid());
        auto outside = 0;
        juce::String example;
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
            {
                const auto c = pixels.getPixelColour (x, y);
                const auto hue = c.getHue() * 360.0f;
                // (8-bit rounding allowed: a hue can only be placed within about 60 / (255 x saturation x brightness)
                // degrees, plus one; pixels darker than 12% can't tint a pixel they're blended with)
                const auto slack = 1.0f + 60.0f / (255.0f * juce::jmax (1.0e-3f, c.getSaturation() * c.getBrightness()));
                if (c.getBrightness() > 0.12f && c.getSaturation() > 0.0f && hue > 38.0f + slack && hue < 322.0f - slack)
                    if (++outside == 1)
                        example << " (" << x << "," << y << ") #" << c.toDisplayString (false) << " hue " << hue;
            }
        INFO (name << ": " << outside << " pixels outside 322-38 degrees" << example);
        CHECK (outside == 0);
    }
}

TEST_CASE ("THE ORCHESTRA VIEW 0.5.1: a stop past the picture's left edge (a cello's or bass's high note) lights at the edge", "[ui][orchestra]")
{
    // An octave up is halfway along; the open string at the nut.
    CHECK (orchestra::alongString (0, 1000.0f, 200.0f, 14.0f) == 0.0f);
    CHECK (std::abs (orchestra::alongString (12, 1000.0f, 200.0f, 14.0f) - 0.5f) < 1.0e-5f);
    // The bridge end off the left edge (the nut at 940, the bridge at -500): two octaves up would be at 940 - 0.75 x
    // 1440 = -140, off the picture: it's drawn at 14 instead. An octave up (220) is where it's stopped.
    const auto at = [] (int semitones) { return 940.0f + (-500.0f - 940.0f) * orchestra::alongString (semitones, 940.0f, -500.0f, 14.0f); };
    CHECK (std::abs (at (24) - 14.0f) < 1.0e-3f);
    CHECK (std::abs (at (12) - 220.0f) < 1.0e-3f);
}

TEST_CASE ("THE ORCHESTRA VIEW 0.6.0: the brass, which a file is, and each note's valves or slide position", "[ui][orchestra][brass]")
{
    using orchestra::Kind;
    CHECK (orchestra::kindOf ("TrumpetSusVib") == Kind::trumpet);
    CHECK (orchestra::kindOf ("FHornStac") == Kind::horn);
    CHECK (orchestra::kindOf ("Tuba-KS") == Kind::tuba);
    CHECK (orchestra::kindOf ("Bass Trombone") == Kind::trombone);   // not a double bass
    CHECK (orchestra::kindOf ("English Horn") == Kind::englishHorn);   // a woodwind (0.12.0), not a horn
    CHECK (orchestra::isBrass (Kind::horn));
    CHECK_FALSE (orchestra::isBrass (Kind::cello));
    CHECK (orchestra::stopsFor (Kind::trumpet, { 60 }).empty());

    const auto valves = [] (Kind k, int note)
    {
        const auto f = orchestra::fingeringFor (k, note);
        return f.playable ? f.valves : -1;
    };
    // B-flat trumpet (sounding pitch: written D4 sounds C4): the usual chart.
    CHECK (valves (Kind::trumpet, 58) == 0b000);   // written C4: open
    CHECK (valves (Kind::trumpet, 60) == 0b101);   // written D4: 1 + 3
    CHECK (valves (Kind::trumpet, 62) == 0b011);   // written E4: 1 + 2
    CHECK (valves (Kind::trumpet, 64) == 0b010);   // written F#4: 2
    CHECK (valves (Kind::trumpet, 65) == 0b000);   // written G4: open
    CHECK (valves (Kind::trumpet, 68) == 0b001);   // written B-flat4: 1
    CHECK (valves (Kind::trumpet, 52) == 0b111);   // written F#3, its lowest: 1 + 2 + 3
    CHECK (valves (Kind::trumpet, 51) == -1);      // under its reach
    // F horn (sounding a fifth under written): written G4 open, B4 on 2, F4 on 1.
    CHECK (valves (Kind::horn, 60) == 0b000);
    CHECK (valves (Kind::horn, 64) == 0b010);
    CHECK (valves (Kind::horn, 58) == 0b001);
    // 4-valve tuba: the 4th valve for five semitones under its second harmonic (F1), 1 + 3 + 4 for C1, all four for B0.
    CHECK (valves (Kind::tuba, 29) == 0b1000);
    CHECK (valves (Kind::tuba, 24) == 0b1101);
    CHECK (valves (Kind::tuba, 23) == 0b1111);
    CHECK (valves (Kind::tuba, 22) == 0b0000);    // its pedal B-flat0
    CHECK (valves (Kind::tuba, 21) == -1);
    CHECK (valves (Kind::tuba, 50) == 0b0000);    // D3: its 5th harmonic
    // Tenor trombone: positions.
    const auto position = [] (int note) { const auto f = orchestra::fingeringFor (Kind::trombone, note); return f.playable ? f.position : -1; };
    CHECK (position (53) == 1);   // F3
    CHECK (position (50) == 4);   // D3
    CHECK (position (48) == 6);   // C3
    CHECK (position (40) == 7);   // E2, its lowest outside the pedals
    CHECK (position (37) == -1);  // no F trigger: out of reach
    CHECK (position (31) == 4);   // a pedal

    // The pictures' geometry: three valves (the horn: three and its thumb valve; the tuba: four), the slide's run.
    CHECK (OrchestraView::getValves (Kind::trumpet, false).size() == 3);
    CHECK (OrchestraView::getValves (Kind::horn, false).size() == 4);
    CHECK (OrchestraView::getValves (Kind::tuba, false).size() == 4);
    CHECK (OrchestraView::hasSlide (Kind::trombone, false));
    CHECK (OrchestraView::hasSlide (Kind::trombone, true));
    CHECK (OrchestraView::getValves (Kind::trumpet, true).size() == 3);
    for (const auto k : { Kind::trumpet, Kind::horn, Kind::tuba })
        for (const auto inSection : { false, true })
            for (const auto& v : OrchestraView::getValves (k, inSection))
            {
                const auto& image = inSection ? OrchestraView::getSectionPicture (false, true) : OrchestraView::getPicture (k);
                CHECK (image.getBounds().toFloat().contains (v));   // in the picture
            }
}

TEST_CASE ("THE ORCHESTRA VIEW 0.6.0: a brass instrument shows its note's fingering; a brass section lights each part", "[ui][orchestra][brass]")
{
    juce::TemporaryFile folder;
    const auto dir = folder.getFile();
    dir.createDirectory();
    testkit::writeWav (dir.getChildFile ("t.wav"), *testkit::sine (220.0, 2.0), 24);
    const auto file = [&dir] (const char* name, int low, int high)
    {
        const auto f = dir.getChildFile (name);
        f.replaceWithText ("<region> sample=t.wav lokey=" + juce::String (low) + " hikey=" + juce::String (high) + " pitch_keycenter=60\n");
        return f.getFullPathName();
    };
    Window w;
    auto& page = w.view->getInstrumentPage();
    auto& view = page.getOrchestraView();
    w.processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    w.processor.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    const auto play = [&] (std::initializer_list<int> notes)
    {
        juce::MidiBuffer midi;
        for (const auto n : notes)
            midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        w.processor.processBlock (buffer, midi);
        juce::Timer::callPendingTimersSynchronously();
        juce::Thread::sleep (60);
        juce::Timer::callPendingTimersSynchronously();
    };

    // One trumpet: its picture; a chord shows its highest note (a trumpet plays one), C4 + D4 -> D4: valves 1 + 2.
    w.processor.loadInstrument (juce::File (file ("TrumpetSus.sfz", 52, 84)));
    REQUIRE (w.processor.waitForInstrument());
    page.refresh();
    CHECK (view.getMode() == OrchestraView::Mode::single);
    CHECK (view.getKind() == orchestra::Kind::trumpet);
    CHECK (view.showsBrass());
    CHECK (page.isShowingPicture());
    CHECK (view.getBrassNote (orchestra::Kind::trumpet) == -1);
    play ({ 60, 62 });
    CHECK (view.getBrassNote (orchestra::Kind::trumpet) == 62);
    // ...and drawn: the view with the note differs from the view without it at valve 1 (pressed), not at valve 3.
    view.setBounds (0, 0, 1100, 450);
    const auto withNote = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    off.addEvent (juce::MidiMessage::noteOff (1, 62), 0);
    w.processor.processBlock (buffer, off);
    juce::Timer::callPendingTimersSynchronously();
    juce::Thread::sleep (60);
    juce::Timer::callPendingTimersSynchronously();
    REQUIRE (view.getBrassNote (orchestra::Kind::trumpet) == -1);
    const auto without = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
    const auto& picture = OrchestraView::getPicture (orchestra::Kind::trumpet);
    const auto scale = juce::jmax (1100.0f / (float) picture.getWidth(), 450.0f / (float) picture.getHeight());
    const auto offset = juce::Point<float> ((1100.0f - (float) picture.getWidth() * scale) * 0.5f, (450.0f - (float) picture.getHeight() * scale) * 0.5f);
    const auto difference = [&] (juce::Point<float> p)
    {
        const auto at = (p * scale + offset).toInt();
        const auto a = withNote.getPixelAt (at.x, at.y), b = without.getPixelAt (at.x, at.y);
        return std::abs (a.getBrightness() - b.getBrightness());
    };
    const auto points = OrchestraView::getValves (orchestra::Kind::trumpet, false);
    REQUIRE (points.size() == 3);
    CHECK (difference (points[0]) > 0.1f);   // (its neon ring is bright already: the dot adds about 0.17)
    CHECK (difference (points[1]) > 0.1f);
    CHECK (difference (points[2]) < 0.05f);

    // A brass section: the brass picture; a trombone note lights the trombone, not the tuba.
    w.processor.loadSection (sampler::section::fromFiles ({ file ("TubaSus.sfz", 29, 62), file ("TromboneSus.sfz", 34, 65),
                                                            file ("FHornSus.sfz", 33, 77), file ("TrumpetSus.sfz", 52, 84) }));
    REQUIRE (w.processor.waitForInstrument());
    page.refresh();
    juce::Timer::callPendingTimersSynchronously();
    CHECK (view.getMode() == OrchestraView::Mode::section);
    CHECK (view.showsBrass());
    play ({});       // (a block first: the audio side takes up the new instrument, fading all voices, then the note)
    play ({ 50 });   // D3: the trombone's split (48-56), 4th position
    CHECK (view.getGlow (orchestra::Kind::trombone) > 0.9f);
    CHECK (view.getGlow (orchestra::Kind::tuba) == 0.0f);
    CHECK (view.getBrassNote (orchestra::Kind::trombone) == 50);
    CHECK (orchestra::fingeringFor (orchestra::Kind::trombone, view.getBrassNote (orchestra::Kind::trombone)).position == 4);
    CHECK (view.getBrassNote (orchestra::Kind::horn) == -1);
}

TEST_CASE ("THE ORCHESTRA VIEW 0.12.0: the woodwinds, which a file is, and each note's keys", "[ui][orchestra][woodwinds]")
{
    using orchestra::Kind;
    // VSCO 2 CE's names and Sonatina's.
    CHECK (orchestra::kindOf ("FluteSusVib") == Kind::flute);
    CHECK (orchestra::kindOf ("PiccoloSus") == Kind::piccolo);
    CHECK (orchestra::kindOf ("OboeSusVib") == Kind::oboe);
    CHECK (orchestra::kindOf ("ClarinetStac") == Kind::clarinet);
    CHECK (orchestra::kindOf ("BassoonSus") == Kind::bassoon);
    CHECK (orchestra::kindOf ("English Horn Sustain") == Kind::englishHorn);
    CHECK (orchestra::kindOf ("Bass Clarinet Sustain") == Kind::bassClarinet);
    CHECK (orchestra::kindOf ("Contrabassoon Sustain") == Kind::contrabassoon);   // not a double bass
    CHECK (orchestra::kindOf ("Alto Flute Solo Sustain") == Kind::altoFlute);   // the flute's picture, its chart a fourth down
    CHECK (orchestra::sectionKindOf (Kind::altoFlute) == Kind::flute);
    CHECK (orchestra::kindOf ("Contrabass") == Kind::bass);
    CHECK (orchestra::isWoodwind (Kind::englishHorn));
    CHECK_FALSE (orchestra::isWoodwind (Kind::horn));
    CHECK (orchestra::sectionKindOf (Kind::contrabassoon) == Kind::bassoon);
    CHECK (orchestra::stopsFor (Kind::flute, { 67 }).empty());

    const auto keys = [] (Kind k, int note)
    {
        const auto f = orchestra::windFingeringFor (k, note);
        return f.charted ? f.keys : -1;
    };
    // (bits: 0 thumb, 1-3 left hand, 4-6 right hand, 7 the octave / register / whisper / E-flat key)
    constexpr int sixFingers = 0b01111110;
    CHECK (keys (Kind::flute, 67) == 0b10001111);   // G4: thumb, the left hand, E-flat
    CHECK (keys (Kind::flute, 74) == 0b01111101);   // D5: the left forefinger up (and the little finger)
    CHECK (keys (Kind::flute, 72) == 0b10000010);   // C5: the left forefinger (and E-flat) only
    CHECK (keys (Kind::flute, 94) == -1);           // B-flat6: not in our chart
    CHECK (keys (Kind::oboe, 67) == 0b00001110);    // G4: the left hand
    CHECK (keys (Kind::oboe, 79) == 0b10001110);    // G5: the same with the octave key
    CHECK (keys (Kind::clarinet, 60) == 0b00000111);   // concert C4 = written D4: thumb, L1, L2
    CHECK (keys (Kind::clarinet, 53) == 0b01111111);   // concert F3 = written G3: every finger down
    CHECK (keys (Kind::bassoon, 43) == 0b10111110);    // G2: the right hand's third up, the whisper key
    // The transposing ones read the same chart at their written pitch.
    for (int n = 40; n < 110; ++n)
    {
        CHECK (keys (Kind::piccolo, n + 12) == keys (Kind::flute, n));
        CHECK (keys (Kind::altoFlute, n - 5) == keys (Kind::flute, n));
        CHECK (keys (Kind::englishHorn, n - 7) == keys (Kind::oboe, n));
        CHECK (keys (Kind::bassClarinet, n - 12) == keys (Kind::clarinet, n));
        CHECK (keys (Kind::contrabassoon, n - 12) == keys (Kind::bassoon, n));
    }
    // The clarinet's upper register (written B4-C6) is its low one (written E3-F4) a twelfth down, plus the register key.
    for (int written = 71; written <= 84; ++written)
        CHECK (keys (Kind::clarinet, written - 2) == (keys (Kind::clarinet, written - 2 - 19) | 0b10000000));
    // Every chart's lowest note has all six fingers down; nothing under it is charted (the clarinets' extension apart).
    CHECK ((keys (Kind::flute, 60) & sixFingers) == sixFingers);
    CHECK ((keys (Kind::oboe, 58) & sixFingers) == sixFingers);
    CHECK ((keys (Kind::bassoon, 34) & sixFingers) == sixFingers);
    CHECK (keys (Kind::flute, 59) == -1);
    CHECK (keys (Kind::oboe, 57) == -1);
    CHECK (keys (Kind::bassoon, 33) == -1);

    // The pictures' keys: eight each, inside the picture, the left hand's three left of the right hand's (the mouthpiece
    // at the left; the bassoons' left hand on the wing joint, right of the boot's right hand).
    for (const auto k : { Kind::flute, Kind::piccolo, Kind::oboe, Kind::englishHorn, Kind::clarinet, Kind::bassClarinet, Kind::bassoon, Kind::contrabassoon })
        for (const auto inSection : { false, true })
        {
            INFO (orchestra::nameOf (k) << (inSection ? " in the section" : ""));
            const auto points = OrchestraView::getKeys (k, inSection);
            REQUIRE (points.size() == 8);
            const auto& image = inSection ? OrchestraView::getSectionPicture (false, orchestra::Family::woodwinds) : OrchestraView::getPicture (k);
            for (const auto& p : points)
                CHECK (image.getBounds().toFloat().contains (p));
            const auto bassoons = orchestra::sectionKindOf (k) == Kind::bassoon;
            CHECK ((points[1].x < points[2].x) != bassoons);
            CHECK ((points[3].x < points[4].x) != bassoons);
        }
}

TEST_CASE ("THE ORCHESTRA VIEW 0.12.0: a woodwind shows its note's keys; a woodwind section lights each part", "[ui][orchestra][woodwinds]")
{
    juce::TemporaryFile folder;
    const auto dir = folder.getFile();
    dir.createDirectory();
    testkit::writeWav (dir.getChildFile ("t.wav"), *testkit::sine (220.0, 2.0), 24);
    const auto file = [&dir] (const char* name, int low, int high)
    {
        const auto f = dir.getChildFile (name);
        f.replaceWithText ("<region> sample=t.wav lokey=" + juce::String (low) + " hikey=" + juce::String (high) + " pitch_keycenter=60\n");
        return f.getFullPathName();
    };
    Window w;
    auto& page = w.view->getInstrumentPage();
    auto& view = page.getOrchestraView();
    w.processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    w.processor.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    const auto play = [&] (juce::MidiBuffer midi)
    {
        w.processor.processBlock (buffer, midi);
        juce::Timer::callPendingTimersSynchronously();
        juce::Thread::sleep (60);
        juce::Timer::callPendingTimersSynchronously();
    };

    // One flute: its picture; G4 held lights its thumb key, not its right hand's first.
    w.processor.loadInstrument (juce::File (file ("FluteSusVib.sfz", 60, 96)));
    REQUIRE (w.processor.waitForInstrument());
    page.refresh();
    CHECK (view.getMode() == OrchestraView::Mode::single);
    CHECK (view.getKind() == orchestra::Kind::flute);
    CHECK (view.showsWoodwinds());
    CHECK (page.isShowingPicture());
    view.setBounds (0, 0, 1100, 450);
    const auto without = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
    play (on);
    const auto withNote = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
    const auto& picture = OrchestraView::getPicture (orchestra::Kind::flute);
    const auto scale = juce::jmax (1100.0f / (float) picture.getWidth(), 450.0f / (float) picture.getHeight());
    const auto offset = juce::Point<float> ((1100.0f - (float) picture.getWidth() * scale) * 0.5f, (450.0f - (float) picture.getHeight() * scale) * 0.5f);
    const auto difference = [&] (juce::Point<float> p)
    {
        const auto at = (p * scale + offset).toInt();
        return std::abs (withNote.getPixelAt (at.x, at.y).getBrightness() - without.getPixelAt (at.x, at.y).getBrightness());
    };
    const auto points = OrchestraView::getKeys (orchestra::Kind::flute, false);
    REQUIRE (points.size() == 8);
    CHECK (difference (points[0]) > 0.1f);    // the thumb key: closed
    CHECK (difference (points[3]) > 0.1f);    // L3: closed
    CHECK (difference (points[4]) < 0.02f);   // R1: open
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::noteOff (1, 67), 0);
    play (off);

    // A section of flute, oboe, English horn, clarinet and bassoon (and a cello part): the woodwinds shown (four
    // pictured parts to one), the English horn left unpictured (the oboe has its own part); a clarinet note lights the
    // clarinet.
    w.processor.loadSection (sampler::section::fromFiles ({ file ("BassoonSus.sfz", 34, 60), file ("CelloEns.sfz", 36, 60),
                                                            file ("ClarinetSus.sfz", 50, 70), file ("English Horn Sustain.sfz", 52, 72),
                                                            file ("OboeSusVib.sfz", 58, 80), file ("FluteSusVib.sfz", 60, 96) }));
    REQUIRE (w.processor.waitForInstrument());
    page.refresh();
    CHECK (view.getMode() == OrchestraView::Mode::section);
    CHECK (view.showsWoodwinds());
    CHECK (view.getSectionFamily() == orchestra::Family::woodwinds);
    const auto section = w.processor.getSection();
    REQUIRE (section.has_value());
    auto clarinetKey = -1;
    for (const auto& part : section->parts)
        if (sampler::section::nameOf (part).containsIgnoreCase ("clarinet"))
            clarinetKey = (part.low + part.high) / 2;
    REQUIRE (clarinetKey >= 0);
    juce::MidiBuffer note;
    note.addEvent (juce::MidiMessage::noteOn (1, clarinetKey, (juce::uint8) 100), 0);
    play (note);
    CHECK (view.getGlow (orchestra::Kind::clarinet) > 0.9f);
    CHECK (view.getGlow (orchestra::Kind::flute) == 0.0f);
    CHECK (view.getGlow (orchestra::Kind::englishHorn) == 0.0f);
    dir.deleteRecursively();
}
