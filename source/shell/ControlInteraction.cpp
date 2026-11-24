#include "Style.h"
#include "Editor.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"
#include "ValueEditor.h"

#include <typeinfo>

bool isMouseHovering(const juce::Component& component) noexcept
{
    return isMouseHovering(component,component.getLocalBounds());
}

bool isMouseHovering(const juce::Component& component,juce::Rectangle<int> region) noexcept
{
    const auto& desktop = juce::Desktop::getInstance();

    // A mouse-down does not mean the cursor has left the control. Cancelling
    // hover on every press causes the 50 ms reveal timer to flash on release.
    for (const auto& source : desktop.getMouseSources())
    {
        if (! source.isMouse()) continue;
        // A drag captures its original component. Do not hover unrelated
        // controls just because the virtual pointer crosses their bounds.
        if (source.isDragging() && source.hasMovedSignificantlySincePressed())
        {
            const auto* captured = source.getComponentUnderMouse();
            if (captured != &component && !component.isParentOf(captured)) continue;
        }
        // JUCE's cached target is updated by a later synthetic mouse event after
        // replacing/hiding a control. Hit-test the current tree for the first paint.
        const auto point=component.getLocalPoint(nullptr,source.getScreenPosition());
        if (region.toFloat().contains(point) && const_cast<juce::Component&>(component).reallyContains(point,true))
            return true;
    }

    return false;
}

bool isEmptyEditorBackground(const juce::Component* component)
{
    if(component==nullptr) return false;
    if(typeid(*component)==typeid(juce::Component) || dynamic_cast<const juce::Viewport*>(component)!=nullptr
        || dynamic_cast<const BackgroundClickComponent*>(component)!=nullptr
        || dynamic_cast<const AvaAudioProcessorEditor*>(component)!=nullptr
        || dynamic_cast<const ParameterControl*>(component)!=nullptr
        || dynamic_cast<const ChoiceControl*>(component)!=nullptr
        || dynamic_cast<const LocalChoiceControl*>(component)!=nullptr)
        return true;
    if(const auto* viewport=dynamic_cast<const juce::Viewport*>(component->getParentComponent()))
        if(viewport->getViewedComponent()!=nullptr && component==viewport->getViewedComponent()->getParentComponent())
            return true;
    return static_cast<bool>(component->getProperties().getWithDefault("emptyEditorBackground",false));
}

void clearKeyboardFocus(juce::Component& component)
{
    if (auto* focusedComponent = juce::Component::getCurrentlyFocusedComponent())
        focusedComponent->giveAwayKeyboardFocus();

    if (auto* topLevel = component.getTopLevelComponent())
        topLevel->unfocusAllComponents();

    if (auto* editor = component.findParentComponentOfClass<AvaAudioProcessorEditor>())
        editor->grabKeyboardFocus();
    else
        component.grabKeyboardFocus();
}
