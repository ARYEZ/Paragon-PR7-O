// The window: its layout, the pages and their tabs, and the on-screen keyboard.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PluginTestUtils.h"

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

    // The page with an instrument and a chord sounding, drawn: no pixel outside the family.
    Window w;
    w.processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    w.processor.prepareToPlay (48000.0, 256);
    auto program = sampler::programFrom (testkit::sine (220.0), 60);
    program->regions[0].loKey = 40;
    program->regions[0].hiKey = 80;
    program->index();
    w.processor.setProgram (std::move (program), "/libraries/a.sfz");
    w.view->getInstrumentPage().refresh();
    for (const auto note : { 48, 55, 64 })
        w.processor.getKeyboardState().noteOn (1, note, 0.8f);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < 10; ++block)
        w.processor.processBlock (buffer, midi);
    juce::Thread::sleep (300);
    juce::Timer::callPendingTimersSynchronously();
    const auto image = w.editor->createComponentSnapshot (w.editor->getLocalBounds());
    auto outside = 0;
    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
            if (! inFamily (image.getPixelAt (x, y)))
                ++outside;
    INFO (outside << " pixels outside the family's colours");
    CHECK (outside == 0);
}
