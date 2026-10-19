#pragma once
#include <JuceHeader.h>

inline juce::Path makeSidePetal(const juce::Rectangle<int> bounds, const bool rightEdge = true,
                                const bool closed = true, const float bevel = 4.0f,
                                const float bevelWidth = 8.0f, const float backInset = 1.0f)
{
    constexpr float depth = 8.0f;
    const auto inner = bounds.toFloat().reduced(1.0f);
    juce::Path path;
    if (inner.getWidth() < depth || inner.getHeight() < 2.0f * bevel) return path;
    const auto edge = rightEdge ? float(bounds.getRight())-backInset : float(bounds.getX())+backInset;
    const auto tip = edge + (rightEdge ? -depth : depth);
    path.startNewSubPath(edge, inner.getY());
    const auto bevelStart=tip + (rightEdge?bevelWidth:-bevelWidth);
    if(bevelWidth<depth) path.lineTo(bevelStart,inner.getY());
    path.lineTo(tip, inner.getY() + bevel);
    path.lineTo(tip, inner.getBottom() - bevel);
    if(bevelWidth<depth) path.lineTo(bevelStart,inner.getBottom());
    path.lineTo(edge, inner.getBottom());
    if (closed) path.closeSubPath();
    return path;
}

inline juce::Path makeEndPetal(const juce::Rectangle<int> bounds, const bool bottom, const float bevelWidth = 4.0f)
{
    const auto area = bounds.toFloat();
    const auto bevel = juce::jmin(bevelWidth, area.getWidth() / 2.0f);
    const auto base = bottom ? area.getBottom() : area.getY();
    const auto tip = bottom ? area.getY() : area.getBottom();
    juce::Path path;
    path.startNewSubPath(area.getX(), base);
    path.lineTo(area.getRight(), base);
    path.lineTo(area.getRight() - bevel, tip);
    path.lineTo(area.getX() + bevel, tip);
    path.closeSubPath();
    return path;
}
