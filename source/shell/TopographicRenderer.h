#pragma once
#include <JuceHeader.h>
#include <memory>

// Native port of the Radiant Canvas 2D renderer (MIT, Paul Bakaus).
class TopographicRenderer
{
public:
    TopographicRenderer();
    ~TopographicRenderer();
    void paint(juce::Graphics&, int canvasWidth, int canvasHeight, double time,
               float scale, int contours, int colour, float thickness = 1.0f);
    void paintPreview(juce::Graphics&, int canvasWidth, int canvasHeight, double time,
                      float scale, int contours, int colour, juce::Rectangle<int> bounds,
                      float displayScale, float thickness = 1.0f);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
