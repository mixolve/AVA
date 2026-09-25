#include "Editor.h"
#include "OscPanel.h"
#include "FilterSection.h"
#include "PresetSections.h"
#include "../crossover/Component.h"
#include "../modules/eql/Processor.h"
#include "../routing/Panel.h"
#include "../routing/State.h"

namespace
{
template <typename ComponentType>
void setComponentVisible(ComponentType* component, const bool shouldShow)
{
    if (component != nullptr)
        component->setVisible(shouldShow);
}
}

void AvaAudioProcessorEditor::setPresetsVisible(const bool shouldShow)
{
    if (presetsSection == nullptr)
        return;

    setComponentVisible(&presetsSection->presetCombo,
                        shouldShow && ! presetsSection->filterActionsExpanded && ! presetsSection->actionsExpanded);
    setComponentVisible(presetsSection->filterActionsToggleButton.get(), shouldShow);
    setComponentVisible(presetsSection->actionsToggleButton.get(), shouldShow);
    const auto showFilterActions = shouldShow && presetsSection->filterActionsExpanded;
    setComponentVisible(addFilterButton.get(), showFilterActions);
    setComponentVisible(sortPlaceButton.get(), showFilterActions);
    setComponentVisible(sortFreqButton.get(), showFilterActions);
    setComponentVisible(sortDuoButton.get(), showFilterActions);
    const auto showActions = shouldShow && presetsSection->actionsExpanded;
    setComponentVisible(presetsSection->addButton.get(), showActions);
    setComponentVisible(presetsSection->saveButton.get(), showActions);
    setComponentVisible(presetsSection->renameButton.get(), showActions);
    setComponentVisible(presetsSection->defaultButton.get(), showActions);
    setComponentVisible(presetsSection->deleteButton.get(), showActions);
}

void AvaAudioProcessorEditor::setEqlFilterSectionsVisible(const bool shouldShow)
{
    for (auto& section : filterSections)
    {
        if (section == nullptr)
            continue;

        section->header->setVisible(shouldShow);
        section->typeControl->setVisible(shouldShow);
        section->placeControl->setVisible(shouldShow);
        section->orderControl->setVisible(shouldShow);
        section->frequencyControl->setVisible(shouldShow);
        section->bandwidthControl->setVisible(shouldShow);
        section->gainControl->setVisible(shouldShow);
        section->bypassButton->setVisible(shouldShow);
    }
}

void AvaAudioProcessorEditor::setEqlControlsVisible(const bool shouldShow)
{
    filterViewport.setVisible(shouldShow);
    setPresetsVisible(shouldShow);
    setEqlFilterSectionsVisible(shouldShow);
}

void AvaAudioProcessorEditor::setFftControlsVisible(const bool shouldShow)
{
    const auto correlationMode = fftDynamicModeControl != nullptr
        && fftDynamicModeControl->getSelectedChoiceIndex() == 1;

    setComponentVisible(fftAnalyserComponent.get(), shouldShow);
    setComponentVisible(fftAnalyserHighControl.get(), shouldShow);
    setComponentVisible(fftAnalyserLowControl.get(), shouldShow);
    setComponentVisible(fftAttackControl.get(), shouldShow);
    setComponentVisible(fftReleaseControl.get(), shouldShow);
    setComponentVisible(fftKneeControl.get(), shouldShow);
    setComponentVisible(fftRatioControl.get(), shouldShow);
    setComponentVisible(fftFloorControl.get(), shouldShow && correlationMode);
    setComponentVisible(fftGeneralProcessorHeader.get(), shouldShow);
    setComponentVisible(fftDspFftSizeControl.get(), shouldShow);
    setComponentVisible(fftDspOverlapControl.get(), shouldShow);
    setComponentVisible(fftDspSlopeControl.get(), shouldShow);
    setComponentVisible(fftCorrelationImpactControl.get(), shouldShow && correlationMode);
    setComponentVisible(fftDeltaButton.get(), shouldShow);
    setComponentVisible(fftDualMonoLeftThresholdControl.get(), shouldShow);
    setComponentVisible(fftDualMonoLeftAdaptiveControl.get(), shouldShow);
    setComponentVisible(fftDualMonoRightThresholdControl.get(), shouldShow && ! correlationMode);
    setComponentVisible(fftDualMonoRightAdaptiveControl.get(), shouldShow && ! correlationMode);
    setComponentVisible(fftDynamicProcessorHeader.get(), shouldShow);
    setComponentVisible(fftDynamicModeControl.get(), shouldShow);
    setComponentVisible(fftCorrelationTypeControl.get(), shouldShow && correlationMode);
    setComponentVisible(fftDynamicDirectionControl.get(), shouldShow);
    setComponentVisible(fftCorrelationSmoothingControl.get(), shouldShow && correlationMode);
    setComponentVisible(fftDualMonoLinkButton.get(), shouldShow && ! correlationMode);
    setComponentVisible(fftAdaptiveSettingsHeader.get(), shouldShow);
    setComponentVisible(fftAdaptiveOffsetControl.get(), shouldShow);
    setComponentVisible(fftAdaptiveAttackControl.get(), shouldShow);
    setComponentVisible(fftAdaptiveHoldControl.get(), shouldShow);
    setComponentVisible(fftAdaptiveReleaseControl.get(), shouldShow);
    setComponentVisible(fftDetectorRangeHeader.get(), shouldShow);
    setComponentVisible(fftDetectorLowCutControl.get(), shouldShow);
    setComponentVisible(fftDetectorHighCutControl.get(), shouldShow);
    setComponentVisible(fftAnalyserTimeControl.get(), shouldShow);

    if (fftDualMonoLeftThresholdControl != nullptr)
        fftDualMonoLeftThresholdControl->setTitleText(correlationMode ? "THRESHOLD" : "L-THRESHOLD");
    if (fftDualMonoLeftAdaptiveControl != nullptr)
        fftDualMonoLeftAdaptiveControl->setTitleText(correlationMode ? "ADAPTIVE" : "L-ADAPTIVE");
    if (fftDualMonoRightThresholdControl != nullptr)
        fftDualMonoRightThresholdControl->setTitleText("R-THRESHOLD");
    if (fftDualMonoRightAdaptiveControl != nullptr)
        fftDualMonoRightAdaptiveControl->setTitleText("R-ADAPTIVE");
    if (fftAdaptiveOffsetControl != nullptr)
        fftAdaptiveOffsetControl->setTitleText("OFFSET");

    if (fftDualMonoLinkButton != nullptr)
    {
        fftDualMonoLinkButton->setButtonText("LINK-LR (STEREO)");
        fftDualMonoLinkButton->setEnabled(true);
    }
}

void AvaAudioProcessorEditor::updateEqlSectionStates(const int activeFilterCount)
{
    updateUndoRedoButtons();
    const auto canSortFilters = activeFilterCount > 1;

    for (auto* sortButton : { sortPlaceButton.get(), sortFreqButton.get(), sortDuoButton.get() })
    {
        if (sortButton == nullptr)
            continue;

        sortButton->setVisible(eqlModuleLoaded && presetsSection != nullptr && presetsSection->filterActionsExpanded);
        sortButton->setEnabled(canSortFilters);
        sortButton->setAlpha(1.0f);
    }

    filterViewport.setVisible(eqlModuleLoaded);
    setPresetsVisible(eqlModuleLoaded);

    for (int orderPosition = 0; orderPosition < static_cast<int>(filterOrderLabels.size()); ++orderPosition)
        if (auto* label = filterOrderLabels[static_cast<size_t>(orderPosition)].get())
            label->setVisible(eqlModuleLoaded && orderPosition < activeFilterCount);

    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
    {
        auto* section = filterSections[static_cast<size_t>(filterIndex)].get();

        if (section == nullptr)
            continue;

        const auto orderPosition = getFilterOrderPositionForIndex(filterIndex);
        const auto isActive = eqlModuleLoaded && orderPosition >= 0;
        const auto sectionExpanded = isActive && section->expanded;
        const auto filterType = section->getFilterType();
        const auto isVolume = filterType == EqlModuleProcessor::FilterType::volume;
        const auto isBell = filterType == EqlModuleProcessor::FilterType::bell;
        const auto isPhasePlace = section->getPlace() >= 5 && section->getPlace() <= 7;
        const auto bandwidthInactive = section->isBandwidthInactiveAtCurrentOrder();
        const auto orderInactive = section->isOrderInactive();
        const auto gainInactive = section->isGainInactive();
        const auto filterOrderOff = filterType == EqlModuleProcessor::FilterType::bell
            && section->orderControl->getSelectedChoiceIndex() == 0;

        section->updatePlaceChoicesForType(false);
        if (auto* eqlProcessor = getActiveEqlProcessor())
            section->header->setButtonText(eqlProcessor->getFilterHeaderText(filterIndex, orderPosition));
        else
            section->header->setButtonText({});

        section->header->setVisible(isActive);
        section->header->setToggleState(sectionExpanded, juce::dontSendNotification);
        section->typeControl->setVisible(sectionExpanded);
        section->placeControl->setVisible(sectionExpanded);
        section->orderControl->setVisible(sectionExpanded);
        section->orderControl->setInteractionEnabled(! orderInactive);
        if (orderInactive || filterOrderOff)
            section->orderControl->setOverrideText("OFF");
        else
            section->orderControl->clearOverrideText();

        section->frequencyControl->setVisible(sectionExpanded);
        section->updateFrequencyRangeForType();
        section->frequencyControl->setInteractionEnabled(! isVolume);
        if (isVolume)
            section->frequencyControl->setOverrideText("OFF");
        else
            section->frequencyControl->clearOverrideText();

        section->bandwidthControl->setVisible(sectionExpanded);
        section->bandwidthControl->setInteractionEnabled(isBell && ! bandwidthInactive);
        if (isBell && ! bandwidthInactive)
            section->bandwidthControl->clearOverrideText();
        else
            section->bandwidthControl->setOverrideText("OFF");

        section->gainControl->setVisible(sectionExpanded);
        section->setGainDisplaysDegrees(isPhasePlace && ! gainInactive);
        section->gainControl->setInteractionEnabled(! gainInactive);
        if (gainInactive)
            section->gainControl->setOverrideText("OFF");
        else
            section->gainControl->clearOverrideText();

        section->bypassButton->setVisible(isActive);
    }

    if (addFilterButton != nullptr)
    {
        addFilterButton->setVisible(eqlModuleLoaded && presetsSection != nullptr && presetsSection->filterActionsExpanded);
        addFilterButton->setEnabled(true);
        addFilterButton->setAlpha(1.0f);
    }
}

juce::String AvaAudioProcessorEditor::getInstanceDisplayName() const
{
    const auto& owner = audioProcessor.getOscOwner();
    const auto routing = ava::routing::readState(owner.getValueTreeState().state);
    return ava::routing::getDisplayName(routing,
                                         routingInstanceId != 0 ? routingInstanceId : routing.rootInstanceId);
}

void AvaAudioProcessorEditor::updateInstanceHeadings()
{
    const auto name = getInstanceDisplayName();

    juce::String moduleLabel;
    switch (audioProcessor.getActiveModule())
    {
        case AvaAudioProcessor::ActiveModule::tls: moduleLabel = "TLS"; break;
        case AvaAudioProcessor::ActiveModule::eql: moduleLabel = "EQL"; break;
        case AvaAudioProcessor::ActiveModule::fft: moduleLabel = "FFT"; break;
        case AvaAudioProcessor::ActiveModule::dyn: moduleLabel = "DYN"; break;
        case AvaAudioProcessor::ActiveModule::trs: moduleLabel = "TRS"; break;
        case AvaAudioProcessor::ActiveModule::none: break;
    }

    if (moduleTitle != nullptr)
    {
        const auto title = moduleLabel;
        if (moduleTitle->getButtonText() != title)
            moduleTitle->setButtonText(title);
    }

    if (auto* crossover = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get()))
        crossover->setInstanceName(name);
}

void AvaAudioProcessorEditor::updateSectionStates()
{
    constexpr auto globalControlsVisible = true;
    const auto activeFilterCount = getActiveFilterCount();

    if (clipButton != nullptr)
    {
        clipButton->setVisible(globalControlsVisible);
        clipButton->setButtonText("C");
        clipButton->setToggleState(false, juce::dontSendNotification);
    }

    if (footerTab != nullptr)
        footerTab->setVisible(true);

    if (hostButton != nullptr)
    {
        hostButton->setVisible(globalControlsVisible);
        hostButton->setToggleState(hostParametersExpanded, juce::dontSendNotification);
    }

    if (routingButton != nullptr)
    {
        routingButton->setVisible(globalControlsVisible);
        routingButton->setToggleState(routingExpanded, juce::dontSendNotification);
    }
    if (routingPanel != nullptr)
        routingPanel->setVisible(routingExpanded);
    if (oscButton != nullptr)
        oscButton->setToggleState(oscExpanded, juce::dontSendNotification);
    if (oscPanel != nullptr)
        oscPanel->setVisible(oscExpanded);
    if (focusedParameterControl != nullptr)
        focusedParameterControl->setVisible(! routingExpanded && ! oscExpanded);

    ensureModuleTitle();
    updateInstanceHeadings();

    const auto hostParametersVisible = hostParametersExpanded;
    const auto moduleContentVisible = crossoverEditor != nullptr
        && ! dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get())->isCrossoverSettingsSelected();

    if (moduleTitle != nullptr)
    {
        moduleTitle->setVisible(audioProcessor.getActiveModule() != AvaAudioProcessor::ActiveModule::none);
    }

    hostParametersViewport.setVisible(hostParametersVisible);

    if (moduleAddButton != nullptr)
    {
        const auto noModuleLoaded = audioProcessor.getActiveModule() == AvaAudioProcessor::ActiveModule::none;
        moduleAddButton->setVisible(noModuleLoaded);
        moduleAddButton->setEnabled(noModuleLoaded);
    }

    setComponentVisible(globalBypassButton.get(), globalControlsVisible);
    setComponentVisible(undoButton.get(), globalControlsVisible);
    setComponentVisible(redoButton.get(), globalControlsVisible);
    setComponentVisible(abSlotAButton.get(), globalControlsVisible);
    setComponentVisible(abSwitchButton.get(), globalControlsVisible);
    setComponentVisible(abSlotBButton.get(), globalControlsVisible);

    for (auto& hostSlotNameField : hostSlotNameFields)
        setComponentVisible(hostSlotNameField.get(), hostParametersVisible);
    for (auto& hostSlotButton : hostSlotButtons)
        setComponentVisible(hostSlotButton.get(), hostParametersVisible);

    setComponentVisible(tlsModuleEditor.get(), tlsModuleLoaded && moduleContentVisible);
    setComponentVisible(dynModuleEditor.get(), dynModuleLoaded && moduleContentVisible);
    setComponentVisible(trsModuleEditor.get(), trsModuleLoaded && moduleContentVisible);

    if (! moduleContentVisible)
    {
        setEqlControlsVisible(false);
        setFftControlsVisible(false);
        return;
    }

    if (! eqlModuleLoaded && ! fftModuleLoaded && ! tlsModuleLoaded && ! dynModuleLoaded && ! trsModuleLoaded)
    {
        setEqlControlsVisible(false);
        setFftControlsVisible(false);
        setComponentVisible(tlsModuleEditor.get(), false);
        setComponentVisible(dynModuleEditor.get(), false);
        setComponentVisible(trsModuleEditor.get(), false);
        return;
    }

    if (tlsModuleLoaded || dynModuleLoaded || trsModuleLoaded)
    {
        setEqlControlsVisible(false);
        setFftControlsVisible(false);
        setComponentVisible(tlsModuleEditor.get(), tlsModuleLoaded);
        setComponentVisible(dynModuleEditor.get(), dynModuleLoaded);
        setComponentVisible(trsModuleEditor.get(), trsModuleLoaded);
        return;
    }

    if (fftModuleLoaded)
    {
        setPresetsVisible(false);
        setFftControlsVisible(true);
        filterViewport.setVisible(true);
        setComponentVisible(addFilterButton.get(), false);
        setComponentVisible(sortPlaceButton.get(), false);
        setComponentVisible(sortFreqButton.get(), false);
        setComponentVisible(sortDuoButton.get(), false);
        setEqlFilterSectionsVisible(false);
        return;
    }

    setFftControlsVisible(false);
    updateEqlSectionStates(activeFilterCount);
}
