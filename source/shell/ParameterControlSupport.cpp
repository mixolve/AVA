#include "ParameterControlSupport.h"
#include "Editor.h"


namespace
{
juce::Component::SafePointer<AvaAudioProcessorEditor> focusedParameterOwner;
BoxTextButton* focusedParameterTitleButton = nullptr;
juce::Slider* focusedParameterValueSlider = nullptr;

AvaAudioProcessorEditor* findFocusOwner(juce::Component* component) noexcept
{
    if (component == nullptr)
        return nullptr;

    if (auto* editor = dynamic_cast<AvaAudioProcessorEditor*>(component))
        return editor;

    return component->findParentComponentOfClass<AvaAudioProcessorEditor>();
}

bool focusBelongsTo(juce::Component& owner) noexcept
{
    return focusedParameterOwner.getComponent() != nullptr
        && focusedParameterOwner.getComponent() == findFocusOwner(&owner);
}

}

void parameter_control_support::focusTitleButton(BoxTextButton* button, juce::Slider* valueSlider)
{
    auto* owner = findFocusOwner(button);

    if (button == nullptr || valueSlider == nullptr)
    {
        shell_parameter_focus::clearFocus();
        return;
    }

    if (owner == nullptr)
    {
        shell_parameter_focus::clearFocus();
        return;
    }

    if (focusedParameterTitleButton != nullptr && focusedParameterTitleButton != button)
        focusedParameterTitleButton->setAlwaysAccentOutline(false);

    focusedParameterOwner = owner;
    focusedParameterTitleButton = button;
    focusedParameterValueSlider = valueSlider;
    focusedParameterTitleButton->setAlwaysAccentOutline(true);
}

bool parameter_control_support::isTitleButtonFocused(const BoxTextButton* button,
                                                     const juce::Slider* valueSlider) noexcept
{
    return button != nullptr
        && valueSlider != nullptr
        && focusedParameterTitleButton == button
        && focusedParameterValueSlider == valueSlider;
}

void parameter_control_support::clearFocusedTitleButton(BoxTextButton* button, juce::Slider* valueSlider)
{
    if (valueSlider != nullptr && focusedParameterValueSlider == valueSlider)
        focusedParameterValueSlider = nullptr;

    if (button == nullptr || focusedParameterTitleButton != button)
        return;

    focusedParameterTitleButton->setAlwaysAccentOutline(false);
    focusedParameterTitleButton = nullptr;
    focusedParameterOwner = nullptr;
    focusedParameterValueSlider = nullptr;
}

bool parameter_control_support::canUseFocusedPotentiometer(
    juce::RangedAudioParameter* parameter,
    const std::function<void()>& valueClickAction) noexcept
{
    return parameter != nullptr
        && valueClickAction == nullptr
        && dynamic_cast<juce::AudioParameterChoice*>(parameter) == nullptr;
}

bool parameter_control_support::assignTitleToHostSlot(juce::Component& source,
                                                      BoxTextButton* titleButton,
                                                      const juce::String& parameterId,
                                                      juce::RangedAudioParameter* parameter)
{
    if (parameter == nullptr)
        return false;

    if (auto* owner = source.findParentComponentOfClass<AvaAudioProcessorEditor>())
    {
        return owner->handleHostSlotAssignRequest(parameterId,
                                                  titleButton != nullptr ? titleButton->getButtonText() : parameterId,
                                                  parameter->getValue());
    }

    return false;
}

juce::Slider* shell_parameter_focus::getFocusedValueSlider(juce::Component& owner) noexcept
{
    return focusBelongsTo(owner) ? focusedParameterValueSlider : nullptr;
}

void shell_parameter_focus::clearFocus() noexcept
{
    if (focusedParameterTitleButton != nullptr)
        focusedParameterTitleButton->setAlwaysAccentOutline(false);

    focusedParameterTitleButton = nullptr;
    focusedParameterOwner = nullptr;
    focusedParameterValueSlider = nullptr;
}

void shell_parameter_focus::clearFocus(juce::Component& owner) noexcept
{
    if (focusBelongsTo(owner))
        clearFocus();
}

void shell_parameter_focus::clearFocusIfNotShowing(juce::Component& owner) noexcept
{
    if (! focusBelongsTo(owner))
        return;

    if (focusedParameterTitleButton == nullptr && focusedParameterValueSlider == nullptr)
        return;

    // Numeric controls use an internal non-visible slider; visibility is driven by the title button.
    if (focusedParameterTitleButton != nullptr)
    {
        if (focusedParameterTitleButton->isShowing())
            return;

        shell_parameter_focus::clearFocus();
        return;
    }

    if (focusedParameterValueSlider != nullptr && focusedParameterValueSlider->isShowing())
        return;

    shell_parameter_focus::clearFocus();
}
