#include "Controls.h"
#include "ValueEditor.h"
#include "Prompt.h"
#include "Editor.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"

void repaintValueOverlay(juce::Component& component)
{
    // The editor owns a buffered image. Repainting its outer zoom/host window
    // reuses that image, so invalidate the editor that paints the indicators.
    component.repaint();
    auto* field = component.findParentComponentOfClass<ParameterField>();
    // Manual Ctrl+selection is stored on ParameterField, but both its value
    // frame and its range marker live in ValueBoxComponent. Repaint both
    // even when the click was not on an embedded button.
    if (auto* owner = dynamic_cast<ParameterField*>(&component); owner != nullptr)
    {
        for (auto* child : owner->getChildren())
            if (auto* value = dynamic_cast<ValueBoxComponent*>(child))
                value->repaint();
    }
    else if (field != nullptr && bool(component.getProperties().getWithDefault("embeddedControlButton", false)))
    {
        for (auto* child : field->getChildren())
            if (auto* value = dynamic_cast<ValueBoxComponent*>(child))
                value->repaint();
    }
    if (auto* editor = component.findParentComponentOfClass<AvaAudioProcessorEditor>())
    {
        auto* repaintSource = field != nullptr && bool(component.getProperties().getWithDefault("embeddedControlButton", false))
            ? static_cast<juce::Component*>(field) : &component;
        editor->repaint(editor->getLocalArea(repaintSource, repaintSource->getLocalBounds().expanded(4)));
    }
}

