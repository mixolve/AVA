#include "WindowGeometry.h"

#if JUCE_WINDOWS
#include <windows.h>

namespace window_geometry
{
namespace
{

HWND getWindow(juce::Component& component)
{
    auto* view = static_cast<HWND>(component.getWindowHandle());
    return view != nullptr ? GetAncestor(view, GA_ROOT) : nullptr;
}
}

bool canMoveNativeWindow(juce::Component& component)
{
    if (dynamic_cast<juce::AudioProcessorEditor*>(&component) == nullptr) return true;
    auto* window = getWindow(component);
    RECT content {};
    if (window == nullptr || IsZoomed(window) || !GetClientRect(window, &content)) return false;
    const auto logical = juce::Desktop::getInstance().getDisplays().physicalToLogical(
        juce::Rectangle<int>(content.left, content.top, content.right-content.left, content.bottom-content.top));
    const auto editor = component.getScreenBounds();
    return editor.getWidth() >= logical.getWidth() * 0.65 && editor.getHeight() >= logical.getHeight() * 0.65;
}

juce::Rectangle<int> getBounds(juce::Component& component)
{
    RECT bounds {};
    if (auto* window = getWindow(component); window != nullptr && GetWindowRect(window, &bounds))
        return juce::Desktop::getInstance().getDisplays().physicalToLogical(
            juce::Rectangle<int>(bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top));
    return component.getTopLevelComponent()->getScreenBounds();
}

void setPosition(juce::Component& component, juce::Point<int> position)
{
    if (auto* window = getWindow(component))
    {
        const auto physical = juce::Desktop::getInstance().getDisplays().logicalToPhysical(position);
        SetWindowPos(window, nullptr, physical.x, physical.y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    }
}

}
#elif ! JUCE_MAC
namespace window_geometry
{
bool canMoveNativeWindow(juce::Component& component) { return component.getTopLevelComponent()->isOnDesktop(); }

juce::Rectangle<int> getBounds(juce::Component& component)
{
    return component.getTopLevelComponent()->getScreenBounds();
}
void setPosition(juce::Component& component, juce::Point<int> position)
{
    auto* top = component.getTopLevelComponent();
    if (top->isOnDesktop())
        top->setTopLeftPosition(position);
}

}
#endif
