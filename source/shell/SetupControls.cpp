#include "ChoiceControl.h"
#include "LocalParameterControl.h"
#include "ParameterControl.h"

void AvaAudioProcessorEditor::setupShellControls()
{
    globalBypassButton = std::make_unique<BoxTextButton>(uiAccent);
    globalBypassButton->setButtonText("B");
    globalBypassButton->setTextJustification(juce::Justification::centred);
    globalBypassButton->setClickingTogglesState(true);
    globalBypassAttachment = std::make_unique<ButtonAttachment>(valueTreeState,
                                                                 AvaAudioProcessor::paramGlobalBypassId,
                                                                 *globalBypassButton);
    globalBypassButton->setLongPressPromptActions({}, [this]
    {
        if (auto* parameter = valueTreeState.getParameter(AvaAudioProcessor::paramGlobalBypassId))
            handleHostSlotAssignRequest(AvaAudioProcessor::paramGlobalBypassId, "B", parameter->getValue());
    });
    globalBypassButton->onClick = [this]
    {
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*globalBypassButton);

    undoButton = std::make_unique<BoxTextButton>(uiGrey500);
    undoButton->setButtonText("U");
    undoButton->setTextJustification(juce::Justification::centred);
    undoButton->setSystemSymbol("arrow.uturn.backward");
    undoButton->onClick = [this]
    {
        performUndo();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*undoButton);

    redoButton = std::make_unique<BoxTextButton>(uiGrey500);
    redoButton->setButtonText("R");
    redoButton->setTextJustification(juce::Justification::centred);
    redoButton->setSystemSymbol("arrow.uturn.forward");
    redoButton->onClick = [this]
    {
        performRedo();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*redoButton);

    abSlotAButton = std::make_unique<BoxTextButton>(uiAccent);
    abSlotAButton->setButtonText("A");
    abSlotAButton->setTextJustification(juce::Justification::centred);
    abSlotAButton->setClickingTogglesState(false);
    abSlotAButton->setToggleAccentVisible(true);
    abSlotAButton->onClick = [this]
    {
        if (audioProcessor.getABCompareActiveSlot() != 0)
            switchABState();

        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*abSlotAButton);

    abSwitchButton = std::make_unique<BoxTextButton>(uiGrey500);
    abSwitchButton->setButtonText({});
    abSwitchButton->setTextJustification(juce::Justification::centred);
    abSwitchButton->setClickingTogglesState(false);
    abSwitchButton->setHorizontalBidirectionalArrowVisible(true);
    abSwitchButton->setLongPressAction([this]
    {
        copyCurrentABStateToOtherSlot();
    }, 500, "C?");
    abSwitchButton->onClick = [this]
    {
        switchABState();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*abSwitchButton);

    abSlotBButton = std::make_unique<BoxTextButton>(uiAccent);
    abSlotBButton->setButtonText("B");
    abSlotBButton->setTextJustification(juce::Justification::centred);
    abSlotBButton->setClickingTogglesState(false);
    abSlotBButton->setToggleAccentVisible(true);
    abSlotBButton->onClick = [this]
    {
        if (audioProcessor.getABCompareActiveSlot() != 1)
            switchABState();

        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*abSlotBButton);

    for (int slotIndex = 0; slotIndex < static_cast<int>(hostSlotButtons.size()); ++slotIndex)
    {
        auto slotNameField = std::make_unique<BoxTextButton>(uiGrey500);
        slotNameField->setButtonText(juce::String::formatted("%02d-", slotIndex + 1)
                                     + AvaAudioProcessor::getHostSlotLetterLabel(slotIndex));
        slotNameField->setTextJustification(juce::Justification::centred);
        slotNameField->setClearsParameterFocusOnMouseDown(false);
        slotNameField->setFillVisible(false);
        slotNameField->setPressFillEnabled(false);
        slotNameField->onClick = [this, slotIndex]
        {
            if (! juce::isPositiveAndBelow(hostSlotMoveSourceIndex,
                                           static_cast<int>(hostSlotAssignments.size())))
            {
                return;
            }

            const auto sourceIndex = hostSlotMoveSourceIndex;
            hostSlotMoveSourceIndex = -1;

            for (const auto& field : hostSlotNameFields)
                if (field != nullptr)
                    field->setDragTargetOutlineVisible(false);

            moveHostSlotAssignment(sourceIndex, slotIndex - sourceIndex);

            if (sourceIndex != slotIndex)
                if (auto* field = hostSlotNameFields[static_cast<size_t>(slotIndex)].get())
                    field->flashConfirmationOutline();

            clearKeyboardFocus(*this);
        };

        auto slotButton = std::make_unique<BoxTextButton>(uiGrey500);
        slotButton->setTextJustification(juce::Justification::centredLeft);
        slotButton->setButtonText({});
        slotButton->setClearsParameterFocusOnMouseDown(false);
        slotButton->onMoveArmed = [this, slotIndex]
        {
            hostSlotMoveSourceIndex = slotIndex;

            for (int index = 0; index < static_cast<int>(hostSlotNameFields.size()); ++index)
                if (auto* field = hostSlotNameFields[static_cast<size_t>(index)].get())
                    field->setDragTargetOutlineVisible(index == slotIndex);
        };

        hostParametersContent.addAndMakeVisible(*slotNameField);
        hostParametersContent.addAndMakeVisible(*slotButton);
        hostSlotNameFields[static_cast<size_t>(slotIndex)] = std::move(slotNameField);
        hostSlotButtons[static_cast<size_t>(slotIndex)] = std::move(slotButton);
    }

    sortPlaceButton = std::make_unique<BoxTextButton>(uiGrey500);
    sortPlaceButton->setButtonText("SP");
    sortPlaceButton->setTextJustification(juce::Justification::centred);
    sortPlaceButton->onClick = [this]
    {
        sortFilterSectionsByPlace();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*sortPlaceButton);

    sortFreqButton = std::make_unique<BoxTextButton>(uiGrey500);
    sortFreqButton->setButtonText("SF");
    sortFreqButton->setTextJustification(juce::Justification::centred);
    sortFreqButton->onClick = [this]
    {
        sortFilterSectionsByFrequency();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*sortFreqButton);

    sortDuoButton = std::make_unique<BoxTextButton>(uiGrey500);
    sortDuoButton->setButtonText("SD");
    sortDuoButton->setTextJustification(juce::Justification::centred);
    sortDuoButton->onClick = [this]
    {
        sortFilterSectionsByDuo();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*sortDuoButton);

}


void AvaAudioProcessorEditor::clearHostSlot(const int slotIndex)
{
    if (! juce::isPositiveAndBelow(slotIndex, static_cast<int>(hostSlotAssignments.size())))
        return;

    auto& assignment = hostSlotAssignments[static_cast<size_t>(slotIndex)];
    assignment.parameterId.clear();
    assignment.parameterName.clear();
    storeEditorStateToValueTree();

    if (auto* slotParameter = valueTreeState.getParameter(AvaAudioProcessor::getHostSlotParameterId(slotIndex));
        slotParameter != nullptr)
    {
        slotParameter->beginChangeGesture();
        slotParameter->setValueNotifyingHost(0.0f);
        slotParameter->endChangeGesture();
    }

    refreshHostSlotButtons();
    storeEditorStateToValueTree();
}

void AvaAudioProcessorEditor::moveHostSlotAssignment(const int slotIndex, const int offset)
{
    const auto slotCount = static_cast<int>(hostSlotAssignments.size());

    if (! juce::isPositiveAndBelow(slotIndex, slotCount) || offset == 0)
        return;

    if (hostSlotAssignments[static_cast<size_t>(slotIndex)].parameterId.isEmpty())
        return;

    const auto destinationIndex = slotIndex + offset;

    if (! juce::isPositiveAndBelow(destinationIndex, slotCount))
        return;

    auto* sourceParameter = valueTreeState.getParameter(AvaAudioProcessor::getHostSlotParameterId(slotIndex));
    auto* destinationParameter = valueTreeState.getParameter(AvaAudioProcessor::getHostSlotParameterId(destinationIndex));

    if (sourceParameter == nullptr || destinationParameter == nullptr)
        return;

    const auto sourceValue = sourceParameter->getValue();
    const auto destinationValue = destinationParameter->getValue();
    std::swap(hostSlotAssignments[static_cast<size_t>(slotIndex)],
              hostSlotAssignments[static_cast<size_t>(destinationIndex)]);

    const juce::ScopedValueSetter<bool> syncGuard(suppressHostSlotAutomationSync, true);
    sourceParameter->beginChangeGesture();
    sourceParameter->setValueNotifyingHost(destinationValue);
    sourceParameter->endChangeGesture();
    destinationParameter->beginChangeGesture();
    destinationParameter->setValueNotifyingHost(sourceValue);
    destinationParameter->endChangeGesture();

    refreshHostSlotButtons();
    storeEditorStateToValueTree();
    updateSectionStates();
    scheduleHistorySnapshot();
}

void AvaAudioProcessorEditor::refreshHostSlotButtons()
{
    for (int slotIndex = 0; slotIndex < static_cast<int>(hostSlotAssignments.size()); ++slotIndex)
    {
        auto* slotNameField = hostSlotNameFields[static_cast<size_t>(slotIndex)].get();
        auto* slotButton = hostSlotButtons[static_cast<size_t>(slotIndex)].get();

        if (slotNameField == nullptr || slotButton == nullptr)
            continue;

        const auto& assignment = hostSlotAssignments[static_cast<size_t>(slotIndex)];
        const auto isAssigned = assignment.parameterId.isNotEmpty();
        slotNameField->setLongPressAction({});

        if (isAssigned)
        {
            slotButton->setLongPressPromptActions([this, slotIndex]
            {
                clearHostSlot(slotIndex);
                clearKeyboardFocus(*this);
            }, {}, "D?");
        }
        else
        {
            slotButton->setLongPressPromptActions({});
        }
        slotButton->setEnabled(isAssigned);
        slotButton->setAlpha(1.0f);

        if (assignment.parameterId.isEmpty())
            slotButton->setButtonText({});
        else
        {
            auto parameterName = assignment.parameterName.isNotEmpty() ? assignment.parameterName
                                                                        : assignment.parameterId;

            if (auto* parameter = findHostAssignableParameter(assignment.parameterId))
            {
                const auto currentName = parameter->getName(256).trim();

                if (currentName.isNotEmpty())
                    parameterName = currentName;
            }

            slotButton->setButtonText(parameterName.toUpperCase());
        }
    }
}

bool AvaAudioProcessorEditor::handleHostSlotAssignRequest(const juce::String& parameterId,
                                                         const juce::String& parameterName,
                                                         const float normalizedValue)
{
    const auto trimmedParameterId = parameterId.trim();

    if (trimmedParameterId.isEmpty())
        return false;

    auto targetSlot = -1;

    for (int slotIndex = 0; slotIndex < static_cast<int>(hostSlotAssignments.size()); ++slotIndex)
    {
        if (hostSlotAssignments[static_cast<size_t>(slotIndex)].parameterId == trimmedParameterId)
        {
            targetSlot = slotIndex;
            break;
        }
    }

    if (targetSlot < 0)
    {
        for (int slotIndex = 0; slotIndex < static_cast<int>(hostSlotAssignments.size()); ++slotIndex)
        {
            if (hostSlotAssignments[static_cast<size_t>(slotIndex)].parameterId.isEmpty())
            {
                targetSlot = slotIndex;
                break;
            }
        }
    }

    if (targetSlot < 0)
        return false;

    auto& assignment = hostSlotAssignments[static_cast<size_t>(targetSlot)];
    assignment.parameterId = trimmedParameterId;
    assignment.parameterName = parameterName.trim().toUpperCase();

    if (auto* parameter = findHostAssignableParameter(trimmedParameterId))
    {
        const auto currentName = parameter->getName(256).trim();

        if (currentName.isNotEmpty())
            assignment.parameterName = currentName.toUpperCase();
    }

    if (assignment.parameterName.isEmpty())
        assignment.parameterName = trimmedParameterId.toUpperCase();

    if (auto* slotParameter = valueTreeState.getParameter(AvaAudioProcessor::getHostSlotParameterId(targetSlot));
        slotParameter != nullptr)
    {
        const auto clampedValue = juce::jlimit(0.0f, 1.0f, normalizedValue);
        slotParameter->beginChangeGesture();
        slotParameter->setValueNotifyingHost(clampedValue);
        slotParameter->endChangeGesture();

        syncHostSlotAssignmentValue(targetSlot, clampedValue);
    }

    refreshHostSlotButtons();
    storeEditorStateToValueTree();
    if (hostParametersExpanded)
    {
        updateSectionStates();
        resized();
    }
    return true;
}
