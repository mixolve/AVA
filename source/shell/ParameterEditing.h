#pragma once

#include "Controls.h"

class AccessoryButton;

class ParameterField : public juce::Component
{
public:
    virtual juce::String getTitleText() const = 0;
    void setBottomAccessory(AccessoryButton* accessory);
    void setBottomComponent(juce::Component& component);
    struct BottomAction { juce::String label; std::function<void()> action; std::function<bool()> isActive; int slot = -1; };
    void setBottomActions(std::initializer_list<BottomAction> actions);
    void setBottomActionEnabled(const juce::String& label, bool enabled);
    void setBottomIndicatorsVisible(bool hostSlot, bool bypass)
    {
        bottomIndicatorsVisible = { hostSlot, bypass };
        layoutBottomAccessory(getLocalBounds());
        repaint();
    }
    bool isBottomIndicatorVisible(int index) const noexcept
    {
        return juce::isPositiveAndBelow(index, 2) && bottomIndicatorsVisible[static_cast<size_t>(index)];
    }
    void setRangeIndicatorsVisible(bool visible) { rangeIndicatorsVisible = visible; repaint(); }
    bool areRangeIndicatorsVisible() const noexcept { return rangeIndicatorsVisible; }
    void setBottomIndicatorSize(int size)
    {
        bottomIndicatorSize = juce::jmax(1, size);
        layoutBottomAccessory(getLocalBounds());
        repaint();
    }
    int getBottomIndicatorSize() const noexcept;
    int getBottomComponentCount() const noexcept;
    bool hasTopInternalControls() const noexcept;
    bool hasBottomInternalControls() const noexcept;
    juce::Rectangle<int> getBottomSideZone(juce::Rectangle<int> bounds, int side) const;
    // Layout for the filled bottom-row text indicators (one or several).
    // Adjacent zones share the standard gap, including the slot/bypass markers.
    juce::Rectangle<int> getBottomTextZone(juce::Rectangle<int> bounds, int index = 0, int count = 1) const;
    int getLayoutCellSpan() const noexcept { return int(getProperties().getWithDefault("layoutCellSpan", 1)); }

    // Draw embedded-action hover with the existing indicator routine at the
    // field level: the 1px outline extends beyond the button's own bounds.
    void paintOverChildren(juce::Graphics& graphics) override;
    void repaintBottomHover(const juce::Component& button);

    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override
    {
        repaint();
        juce::Component::modifierKeysChanged(modifiers);
    }

protected:
    void layoutBottomAccessory(juce::Rectangle<int> bounds);

private:
    bool rangeIndicatorsVisible = true;
    int bottomIndicatorSize = 8;
    AccessoryButton* bottomAccessory = nullptr;
    juce::Component::SafePointer<juce::Component> bottomComponent;
    std::vector<std::unique_ptr<juce::Button>> bottomActions;
    std::array<bool, 2> bottomIndicatorsVisible { true, true };
};

namespace parameter_control_support
{
void configureHiddenSlider(juce::Slider& slider);
inline void applySliderValue(juce::Component& owner,
                             juce::Slider& slider,
                             ValueBoxComponent* valueBox,
                             const double value,
                             const bool sendNotification)
{
    slider.setValue(value, sendNotification ? juce::sendNotificationSync
                                            : juce::dontSendNotification);

    if (valueBox != nullptr)
        valueBox->repaint();

    owner.repaint();

    if (auto* parent = owner.getParentComponent())
        parent->repaint();
}
ValueBoxComponent::BottomIndicator makeParameterToggleIndicator(juce::RangedAudioParameter* parameter);
void bindBypassIndicator(ValueBoxComponent& value, std::function<juce::String()> parameterId,
                         juce::AudioProcessorValueTreeState& state);
void bindParameterIndicators(ValueBoxComponent& value, juce::Component& source,
    std::function<juce::String()> parameterId,
    std::function<juce::RangedAudioParameter*()> parameter,
    juce::AudioProcessorValueTreeState& state);
}
