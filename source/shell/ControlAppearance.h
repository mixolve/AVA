#pragma once
#include <JuceHeader.h>

// Hover changes outline opacity only; active state owns fill and content colours.
struct ControlAppearance
{
    ControlAppearance(bool activeIn=false,float hoverIn=0.0f,bool editingIn=false)
        : active(activeIn),hover(hoverIn),editing(editingIn) {}

    bool active=false;
    float hover=0.0f;
    bool editing=false;
    bool fillVisible=true, normalFrameVisible=true;
    juce::Colour normalFill=juce::Colour(0xff444444),activeFill=juce::Colour(0xffbbbbbb);
    juce::Colour normalContent=juce::Colour(0xffffffff),normalBorder=juce::Colour(0xffbbbbbb);
    juce::Colour activeBorder=juce::Colour(0xff444444);

    juce::Colour backgroundColour() const { return active?activeFill:normalFill; }
    juce::Colour contentColour() const { return active?juce::Colour(0xff000000):normalContent; }

    void paintBackground(juce::Graphics& g,juce::Rectangle<int> bounds) const
    {
        if(!fillVisible && !active) return;
        g.setColour(backgroundColour());g.fillRect(bounds);
    }
    void paintBackground(juce::Graphics& g,const juce::Path& shape) const
    {
        if(!fillVisible && !active) return;
        g.setColour(backgroundColour());g.fillPath(shape);
    }
    void paintFrame(juce::Graphics& g,juce::Rectangle<int> bounds) const
    {
        juce::Path contour;contour.addRectangle(bounds.toFloat());paintFrame(g,contour);
    }
    void paintFrame(juce::Graphics& g,const juce::Path& contour) const
    {
        const juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(contour);
        if(normalFrameVisible) {
            g.setColour(active?activeBorder:normalBorder);
            g.strokePath(contour,juce::PathStrokeType(2.0f)); // 1px inside.
        }
        if(editing) {
            g.setColour(juce::Colour(0xffffffff));
            g.strokePath(contour,juce::PathStrokeType(2.0f));
        }
        if(hover>0.0f) {
            g.setColour(juce::Colour(0xffffffff).withMultipliedAlpha(hover));
            g.strokePath(contour,juce::PathStrokeType(4.0f));
        }
    }
    void paint(juce::Graphics& g,juce::Rectangle<int> bounds) const
    { paintBackground(g,bounds);paintFrame(g,bounds); }
    void paint(juce::Graphics& g,const juce::Path& shape) const
    { paintBackground(g,shape);paintFrame(g,shape); }
};

