#import <AppKit/AppKit.h>

#include "OscListWindowAttachment.h"

namespace osc_list_window
{
void attachToOwner(juce::Component& list, juce::Component& owner)
{
    auto* listView = static_cast<NSView*>(list.getWindowHandle());
    auto* ownerView = static_cast<NSView*>(owner.getWindowHandle());
    if (listView == nil || ownerView == nil)
        return;

    auto* listWindow = [listView window];
    auto* ownerWindow = [ownerView window];
    if (listWindow == nil || ownerWindow == nil || listWindow == ownerWindow)
        return;

    [listWindow setAcceptsMouseMovedEvents:YES];
    [ownerWindow addChildWindow:listWindow ordered:NSWindowAbove];
}

void detachFromOwner(juce::Component& list)
{
    auto* listView = static_cast<NSView*>(list.getWindowHandle());
    if (listView == nil)
        return;

    auto* listWindow = [listView window];
    if (auto* ownerWindow = [listWindow parentWindow])
        [ownerWindow removeChildWindow:listWindow];
}
}
