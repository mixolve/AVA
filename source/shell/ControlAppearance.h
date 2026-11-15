#pragma once
#include "Style.h"

void paintControlFill(juce::Graphics&, juce::Rectangle<int>, juce::Colour);

// Shared control body and frame. Text buttons paint their interaction states separately.
struct ControlAppearance
{
    bool frameVisible = true;
    juce::Colour fillColour = juce::Colour(0xff444444);
    juce::Colour frameColour = juce::Colour(0xffbbbbbb);

    static float pixelThickness(juce::Graphics& g, float thickness)
    {
        if (g.getInternalContext().isVectorDevice()) return thickness;
        const auto scale = juce::jmax(0.01f, g.getInternalContext().getPhysicalPixelScaleFactor());
        return static_cast<float>(juce::jmax(1, juce::roundToInt(thickness * scale))) / scale;
    }

    void paintBackground(juce::Graphics& g,juce::Rectangle<int> bounds) const
    {
        paintControlFill(g, bounds, fillColour);
    }
    void paintBackground(juce::Graphics& g,const juce::Path& shape) const
    {
        const juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(shape);
        paintControlFill(g, shape.getBounds().getSmallestIntegerContainer(), fillColour);
    }
    void paintFrame(juce::Graphics& g,juce::Rectangle<int> bounds) const
    {
        if (frameVisible)
            paintUiFrame(g, bounds.toFloat(), frameColour, pixelThickness(g, 1.0f));
    }
    void paintFrame(juce::Graphics& g,const juce::Path& contour) const
    {
        const juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(contour);
        if (frameVisible) {
            g.setColour(frameColour);
            g.strokePath(contour,juce::PathStrokeType(2.0f * pixelThickness(g, 1.0f)));
        }

    }
    void paint(juce::Graphics& g,juce::Rectangle<int> bounds) const
    { paintBackground(g,bounds);paintFrame(g,bounds); }
    void paint(juce::Graphics& g,const juce::Path& shape) const
    { paintBackground(g,shape);paintFrame(g,shape); }
};
