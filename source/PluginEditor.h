#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/MainView.h"
#include "ui/ParagonLookAndFeel.h"

// The plugin window. All the content lives in MainView, laid out on a fixed
// 1600-wide canvas (900 tall with the keyboard, 752 without); the editor
// just scales it to whatever size the window is, keeping the view's shape.
class ParagonX1AudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ParagonX1AudioProcessorEditor (ParagonX1AudioProcessor&);
    ~ParagonX1AudioProcessorEditor() override;

    void resized() override;

    // Opens at up to 1824 x 1026: the largest 16:9 size that fits the
    // screen above the taskbar, with room for the DAW's own window frame
    // (about 1706 x 959 on a 1080p screen); drag to resize.
    static constexpr int defaultWidth = 1824;
    static constexpr int defaultHeight = 1026;

    // The size to open at, given the screen's area above the taskbar (logical pixels).
    static juce::Point<int> openingSize (juce::Rectangle<int> screen);
    // 0.61.0 (PR7-G's): the width that fits a window of the view's shape below `windowTop`, at most the chosen one.
    static int widthToFit (int chosenWidth, int viewHeight, juce::Rectangle<int> screen, int windowTop);

private:
    juce::SharedResourcePointer<SharedParagonLookAndFeel> look;   // declared first: outlives the view
    MainView view;
    juce::TooltipWindow tooltips { this };

    void fitShape();     // resize limits and aspect ratio for the view's current height
    void fitToView();    // after SHOW / HIDE KEYBOARD: the new shape, then the new size
    int chosenWidth = defaultWidth;   // the width the person chose (or opened at)
    bool fittingToView = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParagonX1AudioProcessorEditor)
};
