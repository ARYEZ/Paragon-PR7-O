#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// THE STAGE VIEW (PR7-O 0.8.0; the owner: "Let's do the stage"): the picture panel's STAGE view. Our own neon stage
// (resources/orchestra/stage3d.py) seen from above and behind the conductor; the orchestra's usual places drawn
// faintly on it as a guide; each part loaded a lit zone at its seat (stage::Seat), brighter while it plays. Drag a
// part to move its seat (kept by the part's name: the processor's setSeat). The mics' distance is written at the
// front (the rack's STAGE page: MIC).
class StageView final : public juce::Component, private juce::Timer
{
public:
    explicit StageView (ParagonX1AudioProcessor&);

    void refresh();                                       // the parts from the processor now
    const std::vector<ParagonX1AudioProcessor::StagePart>& getParts() const noexcept { return parts; }
    // Stage metres <-> the view's pixels (through the picture's geometry and where it's drawn).
    juce::Point<float> toView (juce::Point<float> metres) const;
    juce::Point<float> toStage (juce::Point<float> viewPoint) const;
    static const juce::Image& getPicture();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> pictureArea() const;
    juce::Point<float> toPicture (juce::Point<float> metres) const;   // the picture's own pixels
    juce::Path zone (const stage::Seat&) const;
    int partAt (juce::Point<float>) const;
    float lit (int part) const;

    ParagonX1AudioProcessor& processor;
    std::vector<ParagonX1AudioProcessor::StagePart> parts;
    std::vector<std::pair<int, int>> partKeys;   // each part's keys (a section's split; else all)
    std::vector<float> glow;
    int dragging = -1, hovering = -1;
    juce::Point<float> grabOffset;
    juce::String shownSource;
    int shownSeats = -1;
};
