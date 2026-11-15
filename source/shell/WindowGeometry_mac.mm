#include <juce_gui_basics/juce_gui_basics.h>
#import <AppKit/AppKit.h>

#include "WindowGeometry.h"

namespace window_geometry
{
namespace
{
NSWindow* getWindow(juce::Component& component)
{
    auto* view = static_cast<NSView*>(component.getWindowHandle());
    return view != nil ? [view window] : nil;
}

CGFloat desktopTop()
{
    return NSMaxY([[[NSScreen screens] firstObject] frame]);
}
}

bool canMoveNativeWindow(juce::Component& component)
{
    auto* window = getWindow(component);
    if (window == nil || (window.styleMask & NSWindowStyleMaskFullScreen) != 0) return false;
    if (dynamic_cast<juce::AudioProcessorEditor*>(&component) == nullptr) return true;
    const auto editor = component.getScreenBounds();
    const auto content = window.contentView.bounds;
    return editor.getWidth() >= content.size.width * 0.65
        && editor.getHeight() >= content.size.height * 0.65;
}

juce::Rectangle<int> getBounds(juce::Component& component)
{
    if (auto* window = getWindow(component))
    {
        const auto frame = [window frame];
        return { juce::roundToInt(frame.origin.x), juce::roundToInt(desktopTop() - NSMaxY(frame)),
                 juce::roundToInt(frame.size.width), juce::roundToInt(frame.size.height) };
    }
    return component.getTopLevelComponent()->getScreenBounds();
}

void setPosition(juce::Component& component, juce::Point<int> position)
{
    if (auto* window = getWindow(component))
        [window setFrameTopLeftPoint:NSMakePoint(position.x, desktopTop() - position.y)];
}

}
