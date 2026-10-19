#include "WindowGeometry.h"

#if JUCE_WINDOWS
#include <windows.h>
#include <vector>

namespace window_geometry
{
namespace
{
int dragCursorHideCalls = 0;

HWND getWindow(juce::Component& component)
{
    auto* view = static_cast<HWND>(component.getWindowHandle());
    return view != nullptr ? GetAncestor(view, GA_ROOT) : nullptr;
}
}

bool canResizeNativeWindow(juce::Component& component)
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
std::vector<SnapTarget> getSnapTargets(juce::Component& component)
{
    std::vector<SnapTarget> targets;
    for (const auto& display : juce::Desktop::getInstance().getDisplays().displays)
    {
        const auto area = display.userArea;
        targets.push_back({ { area.getX(), area.getY(), area.getWidth(), area.getHeight() }, true });
    }
    struct Context { HWND own; std::vector<SnapTarget>& targets; } context { getWindow(component), targets };
    EnumWindows([](HWND window, LPARAM data) -> BOOL
    {
        auto& state = *reinterpret_cast<Context*>(data);
        if (window == state.own || !IsWindowVisible(window) || IsIconic(window)) return TRUE;
        RECT frame {};
        if (GetWindowRect(window, &frame) && frame.right > frame.left && frame.bottom > frame.top)
        {
            const auto area = juce::Desktop::getInstance().getDisplays().physicalToLogical(
                juce::Rectangle<int>(frame.left, frame.top, frame.right - frame.left, frame.bottom - frame.top));
            state.targets.push_back({ { area.getX(), area.getY(), area.getWidth(), area.getHeight() }, false });
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return targets;
}

void hideWindowDragCursor()
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    source.hideCursor();
}
void setWindowDragActive(juce::Component&, const bool active)
{
    if (active)
    {
        if (dragCursorHideCalls == 0)
        {
            int visibility;
            do { visibility = ShowCursor(FALSE); ++dragCursorHideCalls; } while (visibility >= 0);
        }
        hideWindowDragCursor();
    }
    else
    {
        while (dragCursorHideCalls > 0) { ShowCursor(TRUE); --dragCursorHideCalls; }
        juce::Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate();
    }
}

}
#elif ! JUCE_MAC
namespace window_geometry
{
bool canResizeNativeWindow(juce::Component& component) { return component.getTopLevelComponent()->isOnDesktop(); }

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
std::vector<SnapTarget> getSnapTargets(juce::Component&)
{
    std::vector<SnapTarget> targets;
    for (const auto& display : juce::Desktop::getInstance().getDisplays().displays)
    {
        const auto area = display.userArea;
        targets.push_back({ { area.getX(), area.getY(), area.getWidth(), area.getHeight() }, true });
    }
    return targets;
}
void hideWindowDragCursor()
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    source.hideCursor();
}
void setWindowDragActive(juce::Component&, const bool active)
{
    if (active) hideWindowDragCursor();
    else juce::Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate();
}

}
#endif
