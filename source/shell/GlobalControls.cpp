#include "Editor.h"
#include "WindowDragHandle.h"
#include "ChoiceControl.h"
#include "LocalParameterControl.h"
#include "ParameterControl.h"

#include <algorithm>
#include <array>

namespace
{
juce::String getListNameForHostTarget(AvaAudioProcessor& processor,
                                     const std::vector<OscParameterInfo>& visibleParameters,
                                     const juce::String& parameterId)
{
    const auto findName = [&visibleParameters] (const juce::String& sourceId)
    {
        const auto match = std::find_if(visibleParameters.begin(),
                                        visibleParameters.end(),
                                        [&sourceId] (const auto& parameter)
                                        {
                                            return parameter.sourceId == sourceId;
                                        });

        return match != visibleParameters.end() ? match->name : juce::String {};
    };

    const auto trimmedParameterId = parameterId.trim();
    if (const auto exactName = findName(trimmedParameterId); exactName.isNotEmpty())
        return exactName;

    if ((processor.getActiveModule() == AvaAudioProcessor::ActiveModule::eql)
        && ! trimmedParameterId.startsWith("band-"))
    {
        const auto bandParameterId = "band-"
            + juce::String(static_cast<int>(processor.getSelectedCrossoverRange() + 1))
            + "_" + trimmedParameterId;
        if (const auto name = findName(bandParameterId); name.isNotEmpty()) return name;
        return getOscDisplayName(bandParameterId);
    }

    return getOscDisplayName(trimmedParameterId);
}
}

void AvaAudioProcessorEditor::setupShellControls()
{
    topLines = std::make_unique<BurningFilmArtwork>();
    topLines->setName("film-top");
    topLines->setGpuRenderingEnabled(true);
    topLines->setPreviewEnabled(true);
    topLines->setAnimationSettings(animationSettings);
    topLines->onSettingsRequested = [this] { showAnimationSettings(); };
    addAndMakeVisible(*topLines);

    bottomLines = std::make_unique<BurningFilmArtwork>();
    bottomLines->setName("film-bottom");
    bottomLines->setGpuRenderingEnabled(true);
    bottomLines->setPreviewEnabled(true);
    bottomLines->setBottomAnimation(true);
    bottomLines->setSynchronizationSource(*topLines);
    bottomLines->setAnimationSettings(animationSettings);
    bottomLines->onSettingsRequested = [this] { showAnimationSettings(); };
    addAndMakeVisible(*bottomLines);

    routingButton = std::make_unique<BoxTextButton>(uiAccent);
    routingButton->setName("routing");
    routingButton->setButtonText({});
    routingButton->setTablerIcon("load-balancer");
    routingButton->setClickingTogglesState(true);
    routingButton->setToggleActiveFillEnabled(true);
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
    oscButton->setName("osc");
    oscButton->setButtonText({});
    oscButton->setTablerIcon("affiliate");
    oscButton->setClickingTogglesState(true);
    oscButton->setToggleActiveFillEnabled(true);
    oscButton->onClick = [this]
    {
        toggleOscSection();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*oscButton);

    globalSpacer = std::make_unique<WindowDragHandle>(*this);
    globalSpacer->setName("drag-handle");
    globalSpacer->setColour(juce::Label::backgroundColourId, uiGreyDark);
    globalSpacer->setColour(juce::Label::outlineColourId, uiGreyLight);
    globalSpacer->setInterceptsMouseClicks(true, false);
    addAndMakeVisible(*globalSpacer);

    globalBypassButton = std::make_unique<BoxTextButton>(uiAccent);
    globalBypassButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::paramGlobalBypassId);
    globalBypassButton->setButtonText({});
    globalBypassButton->setTablerIcon("plug-off");
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

    undoButton = std::make_unique<BoxTextButton>(uiGreyLight);
    undoButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::oscGlobalUndoId);
    undoButton->setButtonText("U");
    undoButton->setTextJustification(juce::Justification::centred);
    undoButton->setTablerIcon("arrow-back-up");
    undoButton->onClick = [this]
    {
        performHistoryStep(HistoryAction::undo);
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*undoButton);

    redoButton = std::make_unique<BoxTextButton>(uiGreyLight);
    redoButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::oscGlobalRedoId);
    redoButton->setButtonText("R");
    redoButton->setTextJustification(juce::Justification::centred);
    redoButton->setTablerIcon("arrow-forward-up");
    redoButton->onClick = [this]
    {
        performHistoryStep(HistoryAction::redo);
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*redoButton);

    abSlotAButton = std::make_unique<BoxTextButton>(uiAccent);
    abSlotAButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::oscGlobalAbSlotAId);
    abSlotAButton->setButtonText("A");
    abSlotAButton->setTextJustification(juce::Justification::centred);
    abSlotAButton->setClickingTogglesState(false);
    abSlotAButton->setToggleActiveFillEnabled(true);
    abSlotAButton->onClick = [this]
    {
        if (audioProcessor.getABCompareActiveSlot() != 0)
            switchABState();

        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*abSlotAButton);

    abSwitchButton = std::make_unique<BoxTextButton>(uiGreyLight);
    abSwitchButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::oscGlobalAbSwitchId);
    abSwitchButton->setButtonText({});
    abSwitchButton->setTextJustification(juce::Justification::centred);
    abSwitchButton->setClickingTogglesState(false);
    abSwitchButton->setTablerIcon("layers-difference");
    abSwitchButton->setLongPressPromptActions([this]
    {
        copyCurrentABStateToOtherSlot();
    });
    abSwitchButton->setLongPressPrimaryPromptIcon("copy");
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
    abSlotBButton->setToggleActiveFillEnabled(true);
    abSlotBButton->onClick = [this]
    {
        if (audioProcessor.getABCompareActiveSlot() != 1)
            switchABState();

        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*abSlotBButton);

    hostParametersContent.onBackgroundClick = [this] { cancelHostSlotMove(); };

    for (int slotIndex = 0; slotIndex < static_cast<int>(hostSlotButtons.size()); ++slotIndex)
    {
        auto slotNameField = std::make_unique<BoxTextButton>(uiGreyLight);
        slotNameField->setButtonText(juce::String::formatted("%02d-", slotIndex + 1)
                                     + AvaAudioProcessor::getHostSlotLetterLabel(slotIndex));
        slotNameField->setTextJustification(juce::Justification::centred);
        slotNameField->setFillVisible(false);
        slotNameField->setInteractionOutlineEnabled(false);
        slotNameField->onClick = [this, slotIndex] { applyHostSlotMove(slotIndex); };

        auto slotButton = std::make_unique<BoxTextButton>(uiGreyLight);
        slotButton->setTextJustification(juce::Justification::centredLeft);
        slotButton->setButtonText({});
        slotButton->onClick = [this, slotIndex] { applyHostSlotMove(slotIndex); };
        slotButton->onMoveArmed = [this, slotIndex] { armHostSlotMove(slotIndex); };
        for (auto* button : { slotNameField.get(), slotButton.get() })
        {
            button->onControlDragStarted = [this, slotIndex] { armHostSlotMove(slotIndex, true); };
            button->onControlDrag = [this] (const juce::MouseEvent& event, const bool released)
            {
                handleHostSlotControlDrag(event, released);
            };
        }

        hostParametersContent.addAndMakeVisible(*slotNameField);
        hostParametersContent.addAndMakeVisible(*slotButton);
        hostSlotNameFields[static_cast<size_t>(slotIndex)] = std::move(slotNameField);
        hostSlotButtons[static_cast<size_t>(slotIndex)] = std::move(slotButton);
    }

    sortFiltersButton = std::make_unique<BoxTextButton>(uiGreyLight);
    sortFiltersButton->setButtonText("SORT");
    sortFiltersButton->setTextJustification(juce::Justification::centred);
    sortFiltersButton->onClick = [this]
    {
        sortFilterSections();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*sortFiltersButton);

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

    if (hostSlotMoveSourceIndex == slotIndex) cancelHostSlotMove();
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
    storeEditorStateToValueTree();
    sourceParameter->beginChangeGesture();
    sourceParameter->setValueNotifyingHost(destinationValue);
    sourceParameter->endChangeGesture();
    destinationParameter->beginChangeGesture();
    destinationParameter->setValueNotifyingHost(sourceValue);
    destinationParameter->endChangeGesture();

    refreshHostSlotButtons();
    updateSectionStates();
    scheduleHistorySnapshot();
}

void AvaAudioProcessorEditor::armHostSlotMove(const int slotIndex, const bool controlDrag)
{
    if (! juce::isPositiveAndBelow(slotIndex, static_cast<int>(hostSlotAssignments.size()))
        || hostSlotAssignments[static_cast<size_t>(slotIndex)].parameterId.isEmpty())
    {
        cancelHostSlotMove();
        return;
    }
    hostSlotMoveSourceIndex = slotIndex;
    hostSlotControlDrag = controlDrag;
    hostSlotControlDragCancelled = false;
    grabKeyboardFocus();
    updateHostSlotMoveTargets();
}

void AvaAudioProcessorEditor::updateHostSlotMoveTargets()
{
    for (int index = 0; index < static_cast<int>(hostSlotButtons.size()); ++index)
    {
        const auto target = hostSlotMoveSourceIndex >= 0 && index != hostSlotMoveSourceIndex;
        const auto assigned = hostSlotAssignments[static_cast<size_t>(index)].parameterId.isNotEmpty();
        if (auto* field = hostSlotNameFields[static_cast<size_t>(index)].get())
        {
            field->setDragTargetHighlighted(target);
            field->setMoveTargetAvailable(target);
            field->setInteractionOutlineEnabled(target);
            field->setState(juce::Button::buttonNormal);
        }
        if (auto* button = hostSlotButtons[static_cast<size_t>(index)].get())
        {
            button->setEnabled(assigned || target);
            button->setDragTargetHighlighted(target);
            button->setMoveTargetAvailable(target);
            button->setState(juce::Button::buttonNormal);
        }
    }
}

void AvaAudioProcessorEditor::cancelHostSlotMove()
{
    hostSlotMoveSourceIndex = -1;
    hostSlotControlDrag = false;
    hostSlotControlDragCancelled = true;
    updateHostSlotMoveTargets();
}

void AvaAudioProcessorEditor::applyHostSlotMove(const int destinationIndex)
{
    const auto sourceIndex = hostSlotMoveSourceIndex;
    if (sourceIndex < 0) return;
    cancelHostSlotMove();
    if (sourceIndex != destinationIndex)
        moveHostSlotAssignment(sourceIndex, destinationIndex - sourceIndex);
    clearKeyboardFocus(*this);
}

void AvaAudioProcessorEditor::handleHostSlotControlDrag(const juce::MouseEvent& event, const bool released)
{
    if (hostSlotControlDragCancelled || hostSlotMoveSourceIndex < 0) return;
    if (! event.mods.isCtrlDown())
    {
        cancelHostSlotMove();
        return;
    }
    const auto position = hostParametersContent.getLocalPoint(event.eventComponent, event.position);
    const auto inViewport = hostParametersViewport.getLocalBounds().toFloat().contains(
        hostParametersViewport.getLocalPoint(event.eventComponent, event.position));
    auto destination = -1;
    for (int index = 0; index < static_cast<int>(hostSlotButtons.size()); ++index)
    {
        auto* field = hostSlotNameFields[static_cast<size_t>(index)].get();
        auto* button = hostSlotButtons[static_cast<size_t>(index)].get();
        if (field == nullptr || button == nullptr) continue;
        const auto hovered = inViewport && index != hostSlotMoveSourceIndex
            && field->getBounds().getUnion(button->getBounds()).toFloat().contains(position);
        field->setState(hovered ? juce::Button::buttonOver : juce::Button::buttonNormal);
        button->setState(hovered ? juce::Button::buttonOver : juce::Button::buttonNormal);
        if (hovered) destination = index;
    }
    if (released)
    {
        if (destination >= 0) applyHostSlotMove(destination);
        else cancelHostSlotMove();
    }
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

        if (isAssigned)
        {
            slotButton->setLongPressPromptActions([this, slotIndex]
            {
                clearHostSlot(slotIndex);
                clearKeyboardFocus(*this);
            });
            slotButton->setLongPressPrimaryPromptIcon("trash");
        }
        else
        {
            slotButton->setLongPressPromptActions({});
        }
        slotButton->setLongPressMovePromptEnabled(isAssigned);
        slotButton->setAlpha(1.0f);

        if (assignment.parameterId.isEmpty())
            slotButton->setButtonText({});
        else
        {
            auto parameterName = getListNameForHostTarget(audioProcessor,
                                                          visibleParameters,
                                                          assignment.parameterId);

            if (parameterName.isEmpty())
                parameterName = assignment.parameterId;

            slotButton->setButtonText(parameterName.toUpperCase());
        }
    }
    updateHostSlotMoveTargets();
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
    assignment.parameterName = getListNameForHostTarget(audioProcessor,
                                                       visibleParameters,
                                                       trimmedParameterId);

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
        performHistoryStep(HistoryAction::undo);
        return true;
    }

    if (actionId == AvaAudioProcessor::oscGlobalRedoId)
    {
        performHistoryStep(HistoryAction::redo);
        return true;
    }

    if (actionId == AvaAudioProcessor::oscAddModuleId)
    {
        const auto& modules = AvaAudioProcessor::availableModules;
        const auto selectedIndex = getOscChoiceIndex(value, static_cast<int>(modules.size()));

        if (selectedIndex < 0)
            return false;

        if (audioProcessor.getActiveModule() == AvaAudioProcessor::ActiveModule::none
            && juce::isPositiveAndBelow(selectedIndex, static_cast<int>(modules.size())))
            loadModule(modules[static_cast<size_t>(selectedIndex)]);

        return true;
    }

    if (actionId == AvaAudioProcessor::oscCloseModuleId)
    {
        closeActiveModule();
        return true;
    }

    return false;
}
