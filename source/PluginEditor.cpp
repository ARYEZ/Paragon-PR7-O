#include "PluginEditor.h"

ParagonX1AudioProcessorEditor::ParagonX1AudioProcessorEditor (ParagonX1AudioProcessor& processorToEdit)
    : AudioProcessorEditor (processorToEdit),
      view (processorToEdit)
{
    setLookAndFeel (&look->lookAndFeel);

    addAndMakeVisible (view);   // the view already has its height (shorter with the MIDI track hidden)

    // Keep the view's shape at any size the user drags to.
    setResizable (true, true);
    setResizeLimits (960, 540, 1920, 1080);   // sets up the constrainer; fitShape gives it the view's shape
    fitShape();
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto size = openingSize (display != nullptr ? display->userArea : juce::Rectangle<int> (defaultWidth, defaultHeight + 80));
    chosenWidth = size.x;
    setSize (size.x, size.x * view.getHeight() / MainView::width);

    // SHOW / HIDE KEYBOARD (PR7-D 0.61.0's SHOW / HIDE MIDI TRACK): the window gets taller or shorter with the view.
    view.onHeightChanged = [this] { fitToView(); };
}

void ParagonX1AudioProcessorEditor::fitShape()
{
    // 16:9 with the track, wider without it; 960 to 1920 wide either way. Set straight on the constrainer, which
    // doesn't resize anything (the editor's setResizeLimits would re-fit the window to the old shape).
    const auto heightAt = [this] (int width) { return width * view.getHeight() / MainView::width; };
    auto* constrainer = getConstrainer();
    constrainer->setSizeLimits (960, heightAt (960), 1920, heightAt (1920));
    constrainer->setFixedAspectRatio ((double) MainView::width / (double) view.getHeight());
}

void ParagonX1AudioProcessorEditor::fitToView()
{
    // The new shape and limits go in first: asking for the new size, the DAW checks it against them straight away
    // (Cubase does), and with the old ones it "corrects" the size to something else.
    fitShape();

    // The width the person chose (dragging, or the opening size), unless the taller window wouldn't fit on the screen
    // below where it is now (the DAW keeps the window's top left corner, so it grows downwards): then narrower, just
    // enough to fit (never under 960).
    auto width = juce::jlimit (960, 1920, chosenWidth);
    if (getPeer() != nullptr)   // on screen (not in the tests)
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
            width = widthToFit (chosenWidth, view.getHeight(), display->userArea, getScreenY());

    const juce::ScopedValueSetter<bool> fitting (fittingToView, true);
    setSize (width, juce::roundToInt ((float) width * (float) view.getHeight() / (float) MainView::width));
}

int ParagonX1AudioProcessorEditor::widthToFit (int chosenWidth, int viewHeight, juce::Rectangle<int> screen, int windowTop)
{
    // Room below the window's top, less a small margin, at the view's shape.
    const auto room = screen.getBottom() - windowTop - 16;
    return juce::jlimit (960, 1920, juce::jmin (chosenWidth, room * MainView::width / viewHeight));
}

juce::Point<int> ParagonX1AudioProcessorEditor::openingSize (juce::Rectangle<int> screen)
{
    // `screen` is the part above the taskbar (or below the menu bar). Leave
    // room for the DAW's window frame, title bar and plugin toolbar, which
    // sit above the plugin itself.
    const auto fitWidth = juce::jmin (screen.getWidth() - 40, (screen.getHeight() - 80) * MainView::width / MainView::height);
    const auto width = juce::jlimit (960, defaultWidth, fitWidth);
    return { width, width * MainView::height / MainView::width };
}

ParagonX1AudioProcessorEditor::~ParagonX1AudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void ParagonX1AudioProcessorEditor::resized()
{
    // Remember the width the person chose, to go back to after a SHOW KEYBOARD that had to be narrower to fit.
    if (! fittingToView)
        chosenWidth = getWidth();

    // Scale the whole view rather than re-laying it out, so it looks the
    // same at every size. JUCE draws through the transform, so text and
    // shapes stay sharp.
    view.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) MainView::width));
}
