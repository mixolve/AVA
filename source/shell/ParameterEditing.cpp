#include "ParameterEditing.h"
#include <algorithm>
#include "ParameterControl.h"
#include "ValueEditor.h"
#include "Editor.h"
#include "../modules/shared/BlockParameters.h"
#include "../modules/dyn/Parameters.h"
#include "../modules/eql/ParameterIds.h"
#include "ModuleCopyTargets.h"

void parameter_control_support::configureHiddenSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setInterceptsMouseClicks(false, false);
    slider.setScrollWheelEnabled(false);
    slider.setAlpha(0.0f);
    slider.setWantsKeyboardFocus(false);
    slider.setMouseClickGrabsKeyboardFocus(false);
}

namespace
{
// The child APVTS of a copied module uses the same local parameter IDs as
// its original. Qualify the host target using the control's actual ancestor,
// not whichever module happens to be selected in the editor.
juce::String hostTargetForControl(juce::Component& source, juce::String local)
{
    for (auto* component = &source; component != nullptr; component = component->getParentComponent())
    {
        const auto uidValue = component->getProperties().getWithDefault("moduleCopyUid", -1);
        const auto uid = static_cast<int>(uidValue);
        if (uid < ava::module_stack::originalCount) continue;
        if (local.startsWith("filter-"))
        {
            const auto band = static_cast<int>(component->getProperties().getWithDefault("moduleCopyBand", -1));
            if (!juce::isPositiveAndBelow(band, 6)) return {};
            local = "band-" + juce::String(band + 1) + "_" + local;
        }
        return ava::module_copy_targets::make(uid, local);
    }
    return local;
}

juce::RangedAudioParameter* bypassFor(juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    if (state.state.hasType("eql_state"))
    {
        for (int filter = 0; filter < EqlModuleProcessor::maxFilterCount; ++filter)
        {
            const auto bypassId = EqlModuleProcessor::getFilterBypassParamId(filter);
            bool matches = false;
            forEachFilterParameterId(filter, [&] (const juce::String& candidate) { matches = matches || id == candidate; });
            if (matches) return state.getParameter(bypassId);
        }
        return nullptr;
    }
    const auto prefix = id.upToFirstOccurrenceOf("_", true, false);
    const auto suffix = id.fromFirstOccurrenceOf("_", false, false);
    if (state.state.hasType("chain_state"))
        for (const auto& spec : ava::modules::chain::parameters::parameterSpecs)
            if (suffix == spec.suffix
                || (spec.control.modeSuffix[0] != '\0' && suffix == spec.control.modeSuffix)
                || (spec.control.noteSuffix[0] != '\0' && suffix == spec.control.noteSuffix))
                return spec.bypassSuffix[0] != '\0' ? state.getParameter(prefix + spec.bypassSuffix) : nullptr;
    if (state.state.hasType("dyn_state"))
        for (const auto& spec : dyn::parameters::parameterSpecs)
            if (dyn::parameters::hasBypass(spec)
                && (suffix == spec.suffix
                    || (spec.control.modeSuffix[0] != '\0' && suffix == spec.control.modeSuffix)
                    || (spec.control.noteSuffix[0] != '\0' && suffix == spec.control.noteSuffix)))
                return state.getParameter(prefix + dyn::parameters::bypassSuffix(spec));
    return nullptr;
}
}

void parameter_control_support::bindParameterIndicators(ValueBoxComponent& value,
    juce::Component& source, std::function<juce::String()> parameterId,
    std::function<juce::RangedAudioParameter*()> parameter, juce::AudioProcessorValueTreeState& state)
{
    value.bottomIndicators[0] = {
        [&source, parameterId, parameter] () -> int
        {
            if (parameter() == nullptr) return -1;
            if (auto* owner = source.findParentComponentOfClass<AvaAudioProcessorEditor>())
                return owner->isHostSlotAssigned(hostTargetForControl(source, parameterId())) ? 1 : 0;
            return -1;
        },
        [&source, parameterId, parameter] (bool)
        {
            if (parameter() != nullptr)
                if (auto* owner = source.findParentComponentOfClass<AvaAudioProcessorEditor>())
                    owner->handleHostSlotAssignRequest(hostTargetForControl(source, parameterId()));
        }
    };
    bindBypassIndicator(value, std::move(parameterId), state);
}

ValueBoxComponent::BottomIndicator parameter_control_support::makeParameterToggleIndicator(juce::RangedAudioParameter* parameter)
{
    return {
        [parameter] () -> int { return parameter != nullptr ? (parameter->getValue() >= 0.5f ? 1 : 0) : -1; },
        [parameter] (bool enabled)
        {
            if (parameter != nullptr)
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
                parameter->endChangeGesture();
            }
        }
    };
}

void parameter_control_support::bindBypassIndicator(ValueBoxComponent& value,
    std::function<juce::String()> parameterId, juce::AudioProcessorValueTreeState& state)
{
    value.bottomIndicators[1] = makeParameterToggleIndicator(bypassFor(state, parameterId()));
}

void ParameterField::paintOverChildren(juce::Graphics& graphics)
{
    const auto drawHover = [this, &graphics] (const juce::Button* button)
    {
        if (button == nullptr || !button->isVisible() || !button->isEnabled()) return;
        const auto fieldBounds = getLocalArea(button, button->getLocalBounds());
        RangeIndicator::paintHover(graphics, *button, *this, fieldBounds, true);
    };
    drawHover(bottomAccessory);
    for (const auto& button : bottomActions) drawHover(button.get());
}

void ParameterField::repaintBottomHover(const juce::Component& button)
{
    repaint(getLocalArea(&button, button.getLocalBounds()).expanded(3));
}

void ParameterField::setBottomAccessory(AccessoryButton* accessory)
{
    bottomAccessory = accessory;
    if (accessory != nullptr) addAndMakeVisible(*accessory);
    resized();
}

void ParameterField::setBottomComponent(juce::Component& component)
{
    if (bottomComponent == &component) return;
    if (bottomComponent != nullptr) removeChildComponent(bottomComponent.getComponent());
    bottomComponent = &component;
    addAndMakeVisible(component);
    resized();
    repaint();
}

bool ParameterField::hasTopInternalControls() const noexcept
{
    for (auto* child : getChildren())
        if (const auto* value = dynamic_cast<const ValueBoxComponent*>(child))
            return value->hasTopInternalControls();
    return false;
}

bool ParameterField::hasBottomInternalControls() const noexcept
{
    if (bottomAccessory != nullptr || bottomComponent != nullptr || !bottomActions.empty()) return true;
    for (auto* child : getChildren())
        if (const auto* value = dynamic_cast<const ValueBoxComponent*>(child))
            return value->hasBottomInternalControls();
    return bottomIndicatorsVisible[0] || bottomIndicatorsVisible[1];
}

int ParameterField::getBottomIndicatorSize() const noexcept
{
    return RangeIndicator::bottomControlSize(*this, getLocalBounds(), bottomIndicatorSize);
}

int ParameterField::getBottomComponentCount() const noexcept
{
    return int(bottomActions.size()) + int(bottomAccessory != nullptr) + int(bottomComponent != nullptr);
}

juce::Rectangle<int> ParameterField::getBottomTextZone(juce::Rectangle<int> bounds, int index, int count) const
{
    constexpr int edgeInset = RangeIndicator::edgeInset;
    auto strip = bounds.reduced(edgeInset);
    const auto textBottom = getControlTextBounds(*this, bounds).getBottom();
    strip.setTop(juce::jlimit(strip.getY(), strip.getBottom(), textBottom + controlInnerGap));

    // The actual indicator can be hidden by a choice control even when its
    // field-level visibility flag remains set. Reserve spacing only for
    // markers that are really painted.
    auto indicatorShown = [this] (int side)
    {
        if (!isBottomIndicatorVisible(side)) return false;
        for (auto* child : getChildren())
            if (const auto* box = dynamic_cast<const ValueBoxComponent*>(child))
                return box->isBottomIndicatorVisible(side);
        return true;
    };
    const auto leftCount = indicatorShown(0) ? 1 : 0;
    const auto rightCount = indicatorShown(1) ? 1 : 0;
    const auto columns = juce::jmax(1, count + leftCount + rightCount);
    const auto slot = juce::jlimit(0, columns - 1, index + leftCount);
    return controlStripCell(strip, slot, columns);
}

juce::Rectangle<int> ParameterField::getBottomSideZone(juce::Rectangle<int> bounds, int side) const
{
    const auto count = getBottomComponentCount();
    auto left = isBottomIndicatorVisible(0), right = isBottomIndicatorVisible(1);
    for (auto* child : getChildren())
        if (const auto* box = dynamic_cast<const ValueBoxComponent*>(child))
        { left = box->isBottomIndicatorVisible(0); right = box->isBottomIndicatorVisible(1); break; }
    const auto columns = juce::jmax(1, count + int(left) + int(right));
    auto strip = bounds.reduced(RangeIndicator::edgeInset);
    strip.setTop(getControlTextBounds(*this, bounds).getBottom() + controlInnerGap);
    const auto slot = side == 0 ? 0 : columns - 1;
    return controlStripCell(strip, slot, columns);
}

void ParameterField::layoutBottomAccessory(juce::Rectangle<int> bounds)
{
    if (bottomAccessory != nullptr) bottomAccessory->placeIn(bounds);
    const auto actionCount = static_cast<int>(bottomActions.size());
    const auto count = getBottomComponentCount();
    const auto firstAction = int(bottomAccessory != nullptr);
    for (int index = 0; index < actionCount; ++index)
    {
        const auto zone = getBottomTextZone(bounds, firstAction + index, count);
        bottomActions[static_cast<size_t>(index)]->setBounds(zone);
        bottomActions[static_cast<size_t>(index)]->toFront(false);
    }
    if (bottomComponent != nullptr)
    {
        bottomComponent->setBounds(getBottomTextZone(bounds, count - 1, count));
        bottomComponent->toFront(false);
    }
}

namespace
{
class TextIndicatorButton final : public juce::Button
{
public:
    explicit TextIndicatorButton(const juce::String& text) : juce::Button(text)
    {
        setButtonText(text);
        getProperties().set("embeddedControlButton", true);
        onStateChange = [this] { repaintValueOverlay(*this); };
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }
    std::function<bool()> isActive;
    bool marqueePending = false;
    void mouseMove(const juce::MouseEvent& event) override
    {
        juce::Button::mouseMove(event);
        if (auto* field = findParentComponentOfClass<ParameterField>())
            field->repaintBottomHover(*this);
    }
    void paintButton(juce::Graphics& graphics, bool hover, bool pressed) override
    {
        const auto zone = getLocalBounds();
        const bool active = isEnabled() && isActive && isActive();
        graphics.setColour(!isEnabled() ? uiIndicatorDisabled : (active || pressed) ? uiWhite : uiIndicatorInactive);
        graphics.fillRect(zone);
        const auto inverted = isEnabled() && !pressed && isHoverInversionReady(*this, hover && isEnabled(), "indicator");
        if (inverted) { graphics.setColour(juce::Colour(0xff666666)); graphics.fillRect(zone); }
        if (drawEmbeddedButtonText(graphics, getButtonText(), zone,
            inverted ? uiWhite : juce::Colour(0xff222222)))
            scheduleMarqueeRepaint(*this, marqueePending);
        // The parent field paints the same RangeIndicator hover outline
        // after children, avoiding clipping by this taller click target.
        juce::ignoreUnused(hover, pressed);
    }
};
}

void ParameterField::setBottomActions(std::initializer_list<BottomAction> actions)
{
    if (bottomActions.size() == actions.size()
        && std::equal(actions.begin(), actions.end(), bottomActions.begin(),
            [](const auto& action, const auto& button) { return action.label == button->getButtonText(); }))
    {
        size_t index = 0;
        for (const auto& action : actions)
        {
            auto& button = static_cast<TextIndicatorButton&>(*bottomActions[index]);
            button.getProperties().set("actionSlot", action.slot >= 0 ? action.slot : int(index));
            ++index;
            button.onClick = action.action;
            button.isActive = action.isActive;
            button.repaint();
        }
        resized();
        return;
    }
    bottomActions.clear();
    for (const auto& action : actions)
    {
        auto button = std::make_unique<TextIndicatorButton>(action.label);
        button->setComponentID("sort-" + action.label);
        button->getProperties().set("actionSlot", action.slot >= 0 ? action.slot : int(bottomActions.size()));
        button->onClick = action.action;
        button->isActive = action.isActive;
        addAndMakeVisible(*button);
        bottomActions.push_back(std::move(button));
    }
    resized();
}

void ParameterField::setBottomActionEnabled(const juce::String& label, bool enabled)
{
    for (auto& button : bottomActions)
        if (button->getButtonText() == label) button->setEnabled(enabled);
}
