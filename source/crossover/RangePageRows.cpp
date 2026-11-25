#include "../modules/shared/TimeUnits.h"
#include "ModuleBindings.h"
#include "RangePage.h"
#include "UiSupport.h"
#include "UiStateSchema.h"

#include "../shell/EditorSetup.h"
#include "../modules/shared/BlockParameters.h"
#include "../modules/dyn/Parameters.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace crossover_ui;

int CrossoverRangePage::RowBase::getTopGap() const noexcept
{
    return hasTopGap ? getUiRowGap(*this) : 0;
}

void CrossoverRangePage::RowBase::mouseDown(const juce::MouseEvent& event)
{
    if (auto* page = findParentComponentOfClass<CrossoverRangePage>())
        page->mouseDown(event.getEventRelativeTo(page));
    else clearKeyboardFocus(*this);
}

CrossoverRangePage::ParameterRow::ParameterRow(CrossoverRangePage& pageIn,
                                                CrossoverModuleComponent& ownerIn,
                                                juce::String parameterId,
                                                juce::String auxiliaryToggleParameterId,
                                                const CrossoverControlSpec& spec)
    : page(pageIn),
      owner(ownerIn),
      controlSpec(spec)
{
    control = std::make_unique<ParameterControl>(
        ownerIn.valueTreeState,
        parameterId,
        spec.label,
        spec.decimals);
    const auto policy = owner.config.getControlPolicy(page.rangeIndex, spec.suffix);
    hasTopGap = spec.hasTopGap;
    addAndMakeVisible(*control);

    refreshInteractionState();

    bindAuxiliary(*control, owner.valueTreeState, auxiliaryToggleParameterId, policy, control.get());
    if (auxiliaryIndicator != nullptr) auxiliaryIndicator->onValueChanged = [this] { refreshInteractionState(); };
    refreshInteractionState();
}

int CrossoverRangePage::ParameterRow::getPreferredHeight() const
{
    return getSquareControlSize(*this);
}

void CrossoverRangePage::ParameterRow::resized()
{
    if (control != nullptr) control->setBounds(getLocalBounds());
}

void CrossoverRangePage::ParameterRow::refreshExternalState()
{
    refreshInteractionState();
}

void CrossoverRangePage::ParameterRow::refreshInteractionState()
{
    const auto policy = owner.config.getControlPolicy(page.rangeIndex, controlSpec.suffix);
    if (control != nullptr) {
        control->setInteractionEnabled(policy.enabled);
        control->setValuePresentation(policy.presentation);
        control->setTitleText(policy.title != nullptr ? policy.title : controlSpec.label);
    }
    if (auxiliaryIndicator != nullptr) auxiliaryIndicator->setEnabled(policy.enabled);
}

CrossoverRangePage::ChoiceRow::ChoiceRow(CrossoverModuleComponent& ownerIn, const size_t rangeIndexIn,
                                         const juce::String& parameterId,
                                         const juce::String& auxiliaryToggleId,
                                         const CrossoverControlSpec& spec)
    : owner(ownerIn), rangeIndex(rangeIndexIn), suffix(spec.suffix)
{
    control = std::make_unique<ChoiceControl>(ownerIn.valueTreeState, parameterId, spec.label);
    const auto policy = owner.config.getControlPolicy(rangeIndexIn, spec.suffix);
    control->setBottomIndicatorsVisible(policy.metadata.hostSlot, true);
    control->getProperties().set("layoutCellSpan", policy.cellSpan);
    if (!policy.choices.isEmpty()) control->setChoices(policy.choices);
    addAndMakeVisible(*control);
    bindAuxiliary(*control, owner.valueTreeState, auxiliaryToggleId, policy);
    refreshExternalState();
}

void CrossoverRangePage::ChoiceRow::refreshExternalState()
{
    const auto policy = owner.config.getControlPolicy(rangeIndex, suffix);
    if (policy.title != nullptr) control->setTitleText(policy.title);
    // MODE uses the same control for TIME and SPECTRAL: keep its hover
    // value choices in sync when DOMAIN changes, without rebuilding it.
    if (!policy.choices.isEmpty()) control->setChoices(policy.choices);
    control->setInteractionEnabled(policy.enabled);
    if (auxiliaryIndicator != nullptr) auxiliaryIndicator->setEnabled(policy.enabled);
    if (!owner.config.isRangeChoiceAvailable) return;
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(
            owner.valueTreeState.getParameter(control->getParameterId())))
        for (int choice = 0; choice < parameter->choices.size(); ++choice)
            control->setChoiceEnabled(choice, owner.config.isRangeChoiceAvailable(suffix, choice));
}

int CrossoverRangePage::ChoiceRow::getPreferredHeight() const
{
    return getSquareControlSize(*this);
}

void CrossoverRangePage::ChoiceRow::resized()
{
    if (control != nullptr) control->setBounds(getLocalBounds());
}

CrossoverRangePage::ToggleRow::ToggleRow(CrossoverRangePage& pageIn,
                                         CrossoverModuleComponent& ownerIn,
                                         juce::String parameterId,
                                         const CrossoverControlSpec& spec)
    : page(pageIn), owner(ownerIn), parameterIdToToggle(std::move(parameterId)), suffix(spec.suffix)
{
    const auto policy = owner.config.getControlPolicy(page.rangeIndex, spec.suffix);
    control = std::make_unique<ParameterControl>(owner.valueTreeState, parameterIdToToggle, spec.label, 0);
    hasTopGap = spec.hasTopGap;
    addAndMakeVisible(*control);
    const auto auxiliaryId = policy.metadata.auxiliarySuffix[0] != '\0'
        ? owner.config.makeRangeParameterId(page.rangeIndex, policy.metadata.auxiliarySuffix) : juce::String {};
    bindAuxiliary(*control, owner.valueTreeState, auxiliaryId, policy);
    if (suffix == "true-peak")
    {
        control->setBottomIndicatorsVisible(false, false);
        const auto rateId = owner.config.makeRangeParameterId(page.rangeIndex, "true-peak-rate");
        if (auto* rate = owner.valueTreeState.getParameter(rateId))
        {
            const auto select = [this, rate](int index)
            {
                rate->beginChangeGesture();
                rate->setValueNotifyingHost(rate->convertTo0to1(float(index)));
                rate->endChangeGesture();
                control->repaint();
            };
            const auto active = [rate](int index) { return juce::roundToInt(rate->convertFrom0to1(rate->getValue())) == index; };
            control->setBottomActions({
                {"04", [select] { select(0); }, [active] { return active(0); }},
                {"08", [select] { select(1); }, [active] { return active(1); }},
                {"16", [select] { select(2); }, [active] { return active(2); }},
            });
            page.listenToParameter(rateId);
        }
    }
    refreshExternalState();
}

int CrossoverRangePage::ToggleRow::getPreferredHeight() const
{
    return getSquareControlSize(*this);
}

void CrossoverRangePage::ToggleRow::refreshExternalState()
{
    const auto enabled = owner.config.getControlPolicy(page.rangeIndex, suffix).enabled;
    control->setInteractionEnabled(enabled);
    if (suffix == "true-peak")
    {
        for (const auto* label : {"04", "08", "16"}) control->setBottomActionEnabled(label, enabled);
        for (auto* child : control->getChildren()) child->repaint();
    }
    if (auxiliaryIndicator != nullptr) auxiliaryIndicator->setEnabled(enabled);
}

void CrossoverRangePage::ToggleRow::resized()
{
    control->setBounds(getLocalBounds());
}

CrossoverRangePage::ReadoutRow::ReadoutRow(CrossoverModuleComponent& ownerIn,
                                           juce::String degreeParameterId,
                                           juce::String flipParameterId,
                                           const CrossoverControlSpec& spec)
    : owner(ownerIn),
      degreeParameterIdToRead(std::move(degreeParameterId)),
      flipParameterIdToRead(std::move(flipParameterId))
{
    value.getProperties().set(juce::Identifier("oscParameterId"), degreeParameterIdToRead);
    value.getProperties().set(juce::Identifier("oscParameterId2"), flipParameterIdToRead);
    value.setFont(makeUiFont());
    value.setColour(juce::Label::textColourId, uiWhite);
    value.setColour(juce::Label::backgroundColourId, uiGreyDark);
    value.setColour(juce::Label::outlineColourId, uiGreyLight);
    value.setJustificationType(juce::Justification::centred);
    value.setBorderSize(juce::BorderSize<int> { 1 });
    addAndMakeVisible(value);

    hasTopGap = spec.hasTopGap;
    updateText();
}

int CrossoverRangePage::ReadoutRow::getPreferredHeight() const
{
    return getSquareControlSize(*this);
}

void CrossoverRangePage::ReadoutRow::refreshExternalState()
{
    updateText();
}

void CrossoverRangePage::ReadoutRow::resized()
{
    value.setBounds(getLocalBounds());
}

void CrossoverRangePage::ReadoutRow::updateText()
{
    const auto degree = readRawParameter(owner.valueTreeState, degreeParameterIdToRead, 0.0f);
    const auto flipRight = readRawParameter(owner.valueTreeState, flipParameterIdToRead, 0.0f) >= 0.5f
        && readRawParameter(owner.valueTreeState, flipParameterIdToRead + ".bypass", 0.0f) < 0.5f;
    value.setText(getOrthogonalPositionDescription(degree, flipRight), juce::dontSendNotification);
}

CrossoverRangePage::TimeRow::TimeRow(CrossoverModuleComponent& ownerIn, const size_t rangeIndexIn,
                                     juce::String valueParameterId,
                                     juce::String modeParameterId,
                                     juce::String syncParameterId,
                                     const CrossoverControlSpec& spec)
    : controlSpec(spec), rangeIndex(rangeIndexIn), owner(ownerIn),
      valueParameterIdToEdit(std::move(valueParameterId)),
      modeParameterIdToEdit(std::move(modeParameterId)),
      noteParameterIdToEdit(std::move(syncParameterId))
{
    control = std::make_unique<ParameterControl>(
        owner.valueTreeState,
        valueParameterIdToEdit,
        spec.label,
        spec.decimals);
    hasTopGap = spec.hasTopGap;
    addAndMakeVisible(*control);

    noteControl = std::make_unique<ChoiceControl>(owner.valueTreeState, noteParameterIdToEdit, spec.label);
    noteControl->displayFormatter = [this](int index, const juce::String& note)
    {
        const auto bpm = owner.config.getTempoBpm != nullptr ? owner.config.getTempoBpm() : 120.0;
        const auto type = getTimeModeIndex();
        auto milliseconds = ava::modules::parameters::noteDurationMilliseconds(index, type, bpm);
        if (auto* parameter = owner.valueTreeState.getParameter(valueParameterIdToEdit)) {
            const auto& range = parameter->getNormalisableRange();
            milliseconds = juce::jlimit(double(range.start), double(range.end), milliseconds);
        }
        return note + "\n" + juce::String(milliseconds, 2);
    };
    addChildComponent(*noteControl);
    modeIndicator = std::make_unique<AccessoryButton>("MD");
    modeIndicator->bindParameter(owner.valueTreeState, modeParameterIdToEdit, true);
    modeIndicator->onValueChanged = [this] { updateModeControl(); };
    control->setBottomAccessory(modeIndicator.get());
    updateModeControl();
}

int CrossoverRangePage::TimeRow::getPreferredHeight() const
{
    return getSquareControlSize(*this);
}

void CrossoverRangePage::TimeRow::refreshExternalState()
{
    updateModeControl();
}

void CrossoverRangePage::TimeRow::resized()
{
    if (control != nullptr) control->setBounds(getLocalBounds());
    if (noteControl != nullptr) noteControl->setBounds(getLocalBounds());
}

bool CrossoverRangePage::TimeRow::isNoteMode() const noexcept
{
    return getTimeModeIndex() > 0;
}

int CrossoverRangePage::TimeRow::getTimeModeIndex() const noexcept
{
    return juce::jmax(0,
                      static_cast<int>(std::round(readRawParameter(owner.valueTreeState,
                                                                    modeParameterIdToEdit,
                                                                    0.0f))));
}

juce::StringArray CrossoverRangePage::TimeRow::getNoteChoices() const
{
    auto choices = owner.config.getNoteChoices != nullptr
        ? owner.config.getNoteChoices()
        : juce::StringArray {};
    const auto typeIndex = getTimeModeIndex();

    if (typeIndex == 2)
        for (auto& choice : choices)
            choice << "T";
    else if (typeIndex == 3)
        for (auto& choice : choices)
            choice << ".";

    return choices;
}

void CrossoverRangePage::TimeRow::updateModeControl()
{
    if (control == nullptr)
        return;

    const auto noteMode = isNoteMode();
    control->setVisible(!noteMode);
    noteControl->setVisible(noteMode);
    noteControl->setChoices(getNoteChoices());
    if (modeIndicator != nullptr)
    {
        auto* target = noteMode ? static_cast<juce::Component*>(noteControl.get()) : control.get();
        if (modeIndicator->getParentComponent() != target)
        {
            control->setBottomAccessory(nullptr);
            noteControl->setBottomAccessory(nullptr);
            if (noteMode) noteControl->setBottomAccessory(modeIndicator.get());
            else control->setBottomAccessory(modeIndicator.get());
        }
    }
    {
        const auto enabled = owner.config.getControlPolicy(rangeIndex, controlSpec.suffix).enabled;
        control->setInteractionEnabled(enabled);
        noteControl->setInteractionEnabled(enabled);
        if (modeIndicator != nullptr) modeIndicator->setEnabled(enabled);
    }
}
