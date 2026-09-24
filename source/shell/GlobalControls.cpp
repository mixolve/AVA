#include "Editor.h"
#include "ChoiceControl.h"
#include "LocalParameterControl.h"
#include "ParameterControl.h"

#include <algorithm>
#include <array>

namespace
{
juce::String getListInternalNameForHostTarget(AvaAudioProcessor& processor,
                                              const std::vector<OscParameterInfo>& visibleParameters,
                                              const juce::String& parameterId)
{
    const auto findInternalName = [&visibleParameters] (const juce::String& internalName)
    {
        const auto match = std::find_if(visibleParameters.begin(),
                                        visibleParameters.end(),
                                        [&internalName] (const auto& parameter)
                                        {
                                            return parameter.internalName == internalName;
                                        });

        return match != visibleParameters.end() ? match->internalName : juce::String {};
    };

    const auto trimmedParameterId = parameterId.trim();
    if (const auto exactName = findInternalName(trimmedParameterId); exactName.isNotEmpty())
        return exactName;

    if ((processor.getActiveModule() == AvaAudioProcessor::ActiveModule::eql
         || processor.getActiveModule() == AvaAudioProcessor::ActiveModule::fft)
        && ! trimmedParameterId.startsWith("band-"))
    {
        const auto bandParameterId = "band-"
            + juce::String(static_cast<int>(processor.getSelectedCrossoverRange() + 1))
            + "_" + trimmedParameterId;
        return findInternalName(bandParameterId);
    }

    return {};
}
}

void AvaAudioProcessorEditor::setupShellControls()
{
    routingButton = std::make_unique<BoxTextButton>(uiAccent);
    routingButton->setButtonText({});
    routingButton->setTablerIcon("load-balancer");
    routingButton->setClickingTogglesState(true);
    routingButton->setToggleAccentVisible(true);
    routingButton->onClick = [this]
    {
        if (returnToRoutingAction != nullptr)
            returnToRoutingAction();
        else
            toggleRoutingSection();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*routingButton);

    oscButton = std::make_unique<BoxTextButton>(uiAccent);
    oscButton->setButtonText({});
    oscButton->setTablerIcon("affiliate");
    oscButton->setClickingTogglesState(true);
    oscButton->setToggleAccentVisible(true);
    oscButton->onClick = [this]
    {
        toggleOscSection();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*oscButton);

    globalBypassButton = std::make_unique<BoxTextButton>(uiAccent);
    globalBypassButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::paramGlobalBypassId);
    globalBypassButton->setButtonText("BP");
    globalBypassButton->setTextJustification(juce::Justification::centred);
    globalBypassButton->setClickingTogglesState(true);
    globalBypassAttachment = std::make_unique<ButtonAttachment>(valueTreeState,
                                                                 AvaAudioProcessor::paramGlobalBypassId,
                                                                 *globalBypassButton);
    globalBypassButton->setLongPressPromptActions({}, [this]
    {
        if (auto* parameter = valueTreeState.getParameter(AvaAudioProcessor::paramGlobalBypassId))
            handleHostSlotAssignRequest(AvaAudioProcessor::paramGlobalBypassId, "BP", parameter->getValue());
    });
    globalBypassButton->onClick = [this]
    {
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*globalBypassButton);

    if (clipButton != nullptr)
        clipButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::oscGlobalClipId);

    undoButton = std::make_unique<BoxTextButton>(uiGrey500);
    undoButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::oscGlobalUndoId);
    undoButton->setButtonText("U");
    undoButton->setTextJustification(juce::Justification::centred);
    undoButton->setTablerIcon("arrow-back-up");
    undoButton->onClick = [this]
    {
        performUndo();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*undoButton);

    redoButton = std::make_unique<BoxTextButton>(uiGrey500);
    redoButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::oscGlobalRedoId);
    redoButton->setButtonText("R");
    redoButton->setTextJustification(juce::Justification::centred);
    redoButton->setTablerIcon("arrow-forward-up");
    redoButton->onClick = [this]
    {
        performRedo();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*redoButton);

    abSlotAButton = std::make_unique<BoxTextButton>(uiAccent);
    abSlotAButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::oscGlobalAbSlotAId);
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
    abSwitchButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::oscGlobalAbSwitchId);
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
    abSlotBButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::oscGlobalAbSlotBId);
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

void AvaAudioProcessorEditor::toggleOscSection()
{
    oscExpanded = ! oscExpanded;
    if (oscExpanded)
    {
        routingExpanded = false;
        hostParametersExpanded = false;
    }
    updateSectionStates();
    resized();
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
    const auto visibleParameters = audioProcessor.getVisibleOscParameters();

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
            auto parameterName = getListInternalNameForHostTarget(audioProcessor,
                                                                  visibleParameters,
                                                                  assignment.parameterId);

            if (parameterName.isEmpty())
                parameterName = assignment.parameterId;

            slotButton->setButtonText(parameterName);
        }
    }
}

bool AvaAudioProcessorEditor::handleHostSlotAssignRequest(const juce::String& parameterId,
                                                         const juce::String& parameterName,
                                                         const float normalizedValue)
{
    juce::ignoreUnused(parameterName);
    const auto trimmedParameterId = parameterId.trim();

    if (trimmedParameterId.isEmpty()
        || trimmedParameterId.startsWith(AvaAudioProcessor::paramHostSlotPrefix))
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
    const auto visibleParameters = audioProcessor.getVisibleOscParameters();
    assignment.parameterName = getListInternalNameForHostTarget(audioProcessor,
                                                                visibleParameters,
                                                                trimmedParameterId);

    if (assignment.parameterName.isEmpty())
        assignment.parameterName = trimmedParameterId;

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

bool AvaAudioProcessorEditor::handleOscGlobalAction(const juce::String& actionId, const float value)
{
    if (value < 0.5f)
        return true;

    if (actionId == AvaAudioProcessor::oscGlobalAbSlotAId)
    {
        if (audioProcessor.getABCompareActiveSlot() != 0)
            switchABState();
        return true;
    }

    if (actionId == AvaAudioProcessor::oscGlobalAbSwitchId)
    {
        switchABState();
        return true;
    }

    if (actionId == AvaAudioProcessor::oscGlobalAbSlotBId)
    {
        if (audioProcessor.getABCompareActiveSlot() != 1)
            switchABState();
        return true;
    }

    if (actionId == AvaAudioProcessor::oscGlobalUndoId)
    {
        performUndo();
        return true;
    }

    if (actionId == AvaAudioProcessor::oscGlobalRedoId)
    {
        performRedo();
        return true;
    }

    if (actionId == AvaAudioProcessor::oscAddModuleId)
    {
        static constexpr std::array moduleOrder {
            AvaAudioProcessor::ActiveModule::tls,
            AvaAudioProcessor::ActiveModule::eql,
            AvaAudioProcessor::ActiveModule::fft,
            AvaAudioProcessor::ActiveModule::dyn,
            AvaAudioProcessor::ActiveModule::trs
        };
        const auto selectedIndex = juce::roundToInt(value) - 1;

        if (audioProcessor.getActiveModule() == AvaAudioProcessor::ActiveModule::none
            && juce::isPositiveAndBelow(selectedIndex, static_cast<int>(moduleOrder.size())))
            loadModule(moduleOrder[static_cast<size_t>(selectedIndex)]);

        return true;
    }

    if (actionId == AvaAudioProcessor::oscCloseModuleId)
    {
        closeActiveModule();
        return true;
    }

    return false;
}
