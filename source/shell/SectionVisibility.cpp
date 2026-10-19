#include "ChoiceControl.h"
#include "Editor.h"
#include "OscPanel.h"
#include "FilterSection.h"
#include "PresetSection.h"
#include "../crossover/Editor.h"
#include "../modules/eql/Processor.h"
#include "../modules/eql/FilterSupport.h"
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

    const auto progress = presetsSection->actionsReveal.value();
    const auto showActions = shouldShow && (presetsSection->actionsToggleButton->getToggleState() || progress > 0.0);
    const auto showPresets = shouldShow && (!presetsSection->actionsToggleButton->getToggleState() || progress < 1.0);
    setComponentVisible(presetsSection->actionsToggleButton.get(), shouldShow);
    presetsSection->actionsClip.setVisible(showActions);
    presetsSection->presetClip.setVisible(showPresets);
    presetsSection->actionsClip.setAlpha(1.0f);
    presetsSection->presetClip.setAlpha(1.0f);
    presetsSection->actionsClip.setInterceptsMouseClicks(false, !presetsSection->actionsReveal.isRunning());
    presetsSection->presetClip.setInterceptsMouseClicks(false, !presetsSection->actionsReveal.isRunning());
    setComponentVisible(&presetsSection->presetCombo, showPresets);
    setComponentVisible(presetsSection->addButton.get(), showPresets);
    setComponentVisible(addFilterButton.get(), shouldShow);
    setComponentVisible(sortFiltersButton.get(), shouldShow);
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

        section->body.setVisible(shouldShow && (section->expanded || section->disclosure.value() > 0.0));
        section->accentFrame.setVisible(shouldShow && (section->expanded || section->disclosure.value() > 0.0));
        section->header->setVisible(shouldShow);
        section->typeControl->setVisible(shouldShow);
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

void AvaAudioProcessorEditor::updateEqlSectionStates(const int activeFilterCount)
{
    updateUndoRedoButtons();
    const auto canSortFilters = activeFilterCount > 1;

    if (sortFiltersButton != nullptr)
    {
        sortFiltersButton->setVisible(eqlModuleLoaded && presetsSection != nullptr);
        sortFiltersButton->setEnabled(canSortFilters);
        sortFiltersButton->setAlpha(1.0f);
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
        const auto targetExpanded = isActive && section->expanded;
        if (! section->disclosureInitialised || ! isActive)
        {
            section->disclosure.snap(targetExpanded);
            section->disclosureInitialised = true;
        }
        else section->disclosure.setExpanded(targetExpanded);
        const auto sectionExpanded = isActive && (targetExpanded || section->disclosure.value() > 0.0);
        section->body.setVisible(sectionExpanded);
        section->body.setAlpha(static_cast<float>(section->disclosure.value()));
        section->body.setInterceptsMouseClicks(! section->disclosure.isRunning(), ! section->disclosure.isRunning());
        section->accentFrame.setAlpha(static_cast<float>(section->disclosure.value()));
        const auto filterType = section->getFilterType();
        const auto availability = getFilterControlAvailability(filterType, section->orderControl->getSelectedChoiceIndex());
        const auto isVolume = ! availability.frequency;
        const auto phaseEnabled = section->isPhaseEnabled();
        const auto orderInactive = ! availability.order;
        const auto gainInactive = ! availability.gain;
        const auto filterOrderOff = filterType == EqlModuleProcessor::FilterType::bell
            && section->orderControl->getSelectedChoiceIndex() == 0;

        section->updatePhaseForType(false);
        if (auto* eqlProcessor = getActiveEqlProcessor())
            section->header->setButtonText(eqlProcessor->getFilterHeaderText(filterIndex));
        else
            section->header->setButtonText({});

        section->accentFrame.setVisible(sectionExpanded);
        section->header->setVisible(isActive);
        section->header->setToggleState(targetExpanded, juce::dontSendNotification);
        section->header->setActiveFill(targetExpanded);
        section->typeControl->setVisible(sectionExpanded);
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
        section->bandwidthControl->setInteractionEnabled(availability.bandwidth);
        if (availability.bandwidth)
            section->bandwidthControl->clearOverrideText();
        else
            section->bandwidthControl->setOverrideText("OFF");

        section->gainControl->setVisible(sectionExpanded);
        section->setGainDisplaysDegrees(phaseEnabled && ! gainInactive);
        section->gainControl->setInteractionEnabled(! gainInactive);
        if (gainInactive)
            section->gainControl->setOverrideText("OFF");
        else
            section->gainControl->clearOverrideText();

        section->bypassButton->setVisible(isActive);
    }

    if (addFilterButton != nullptr)
    {
        addFilterButton->setVisible(eqlModuleLoaded && presetsSection != nullptr);
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
        case AvaAudioProcessor::ActiveModule::dyn: moduleLabel = "DYN"; break;
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
        clipButton->setButtonText({});
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

    ensureModuleTitle();
    updateInstanceHeadings();

    const auto hostParametersVisible = hostParametersExpanded;
    if (! hostParametersVisible && hostSlotMoveSourceIndex >= 0) cancelHostSlotMove();
    const auto modulePageVisible = ! routingExpanded && ! oscExpanded && ! hostParametersExpanded;
    setComponentVisible(crossoverEditor.get(), modulePageVisible);
    const auto moduleContentVisible = modulePageVisible && crossoverEditor != nullptr
        && ! dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get())->isCrossoverSettingsSelected();

    if (moduleTitle != nullptr)
    {
        moduleTitle->setVisible(modulePageVisible && audioProcessor.getActiveModule() != AvaAudioProcessor::ActiveModule::none);
    }

    hostParametersViewport.setVisible(hostParametersVisible);

    if (moduleAddButton != nullptr)
    {
        const auto noModuleLoaded = audioProcessor.getActiveModule() == AvaAudioProcessor::ActiveModule::none;
        moduleAddButton->setVisible(modulePageVisible && noModuleLoaded);
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

    if (! moduleContentVisible)
    {
        setEqlControlsVisible(false);
        return;
    }

    if (! eqlModuleLoaded && ! tlsModuleLoaded && ! dynModuleLoaded)
    {
        setEqlControlsVisible(false);
        setComponentVisible(tlsModuleEditor.get(), false);
        setComponentVisible(dynModuleEditor.get(), false);
        return;
    }

    if (tlsModuleLoaded || dynModuleLoaded)
    {
        setEqlControlsVisible(false);
        setComponentVisible(tlsModuleEditor.get(), tlsModuleLoaded);
        setComponentVisible(dynModuleEditor.get(), dynModuleLoaded);
        return;
    }

    updateEqlSectionStates(activeFilterCount);
}
