// The window: its layout, the pages and their tabs, and the on-screen keyboard.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PluginTestUtils.h"
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

    // The four tabs, side by side in the header, the shown page's lit.
    std::vector<juce::Button*> tabs;
    for (const auto* id : { "instrumentTab", "fxTab", "presetsTab", "settingsTab" })
    {
        auto* tab = dynamic_cast<juce::Button*> (w.view->findChildWithID (id));
        INFO (id);
        REQUIRE (tab != nullptr);
        CHECK (tab->getBottom() < 64);
        CHECK (tab->getWidth() > 150);
        if (! tabs.empty())
            CHECK (tab->getX() >= tabs.back()->getRight());
        tabs.push_back (tab);
    }
    CHECK (tabs[0]->getToggleState());

    const std::pair<MainView::Page, int> pages[] = { { MainView::Page::fx, 1 }, { MainView::Page::presets, 2 }, { MainView::Page::settings, 3 } };
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
    CHECK (orchestra::kindOf ("BassoonSus") == Kind::none);
    CHECK (orchestra::kindOf ("TromboneSus") == Kind::none);
    CHECK (orchestra::kindOf ("GM-StylePerc bassdrum") == Kind::none);
    CHECK (orchestra::kindOf ("FluteSusVib") == Kind::none);

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
                                                ParamIDs::instRelease, ParamIDs::instVoices, ParamIDs::instDynamics });
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

    // A flute: no picture (not modelled yet): the region map.
    w.processor.setProgram (make ("FluteSusVib", 60, 96), "/libraries/FluteSusVib.sfz");
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
    CHECK (view.getMode() == OrchestraView::Mode::section);   // the horns aren't modelled: they play, unpictured
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
