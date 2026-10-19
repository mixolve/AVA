#include <juce_opengl/juce_opengl.h>
#import <Cocoa/Cocoa.h>
#include "SvgExport.h"

namespace svg_export
{
void* installKeyboardShortcut(juce::Component& component)
{
    juce::Component::SafePointer<juce::Component> target(&component);
    id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
        handler:^NSEvent*(NSEvent* event)
        {
            // Use the physical E key: Option changes the character on macOS.
            const auto flags = event.modifierFlags;
            if (event.keyCode != 14
                || (flags & NSEventModifierFlagControl) == 0
                || (flags & NSEventModifierFlagCommand) != 0
                || (flags & NSEventModifierFlagOption) == 0
                || (flags & NSEventModifierFlagShift) == 0
                || target == nullptr || !target->isShowing())
                return event;
            for (auto* parent = target->getParentComponent(); parent != nullptr; parent = parent->getParentComponent())
                if (parent->getName() == "editor")
                    return event;
            auto* view = static_cast<NSView*>(target->getWindowHandle());
            if (view == nil || event.window != view.window)
                return event;
            // In a docked host window, choose the editor under the mouse or with focus.
            const auto bounds = target->getScreenBounds();
            if (bounds.getWidth() < view.window.contentView.bounds.size.width * 0.65
                && !target->hasKeyboardFocus(true)
                && !bounds.contains(juce::Desktop::getMousePosition()))
                return event;
            if (!event.isARepeat)
                juce::MessageManager::callAsync([target]
                {
                    if (target != nullptr && target->isShowing())
                        chooseFile(*target);
                });
            return nil;
        }];
    return [monitor retain];
}

void removeKeyboardShortcut(void* monitor)
{
    if (monitor == nullptr)
        return;
    [NSEvent removeMonitor:static_cast<id>(monitor)];
    [static_cast<id>(monitor) release];
}
}
