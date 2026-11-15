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

    setComponentVisible(&presetsSection->presetChoice, shouldShow);
    for (auto* button : { presetsSection->addButton.get(), presetsSection->saveButton.get(),
            presetsSection->renameButton.get(), presetsSection->defaultButton.get(), presetsSection->deleteButton.get(),
            presetsSection->copyFiltersButton.get(), presetsSection->pasteFilterButton.get(),
            addFilterButton.get(), sortFiltersButton.get() })
        setComponentVisible(button, shouldShow);
}

void AvaAudioProcessorEditor::updateEqlSectionStates(const int activeFilterCount)
{
    enforceSingleExpandedFilterSection();
    updateUndoRedoButtons();
    const auto canSortFilters = activeFilterCount > 1;

    if (sortFiltersButton != nullptr)
    {
        sortFiltersButton->setVisible((getActiveEqlProcessor() != nullptr) && presetsSection != nullptr);
        sortFiltersButton->setInteractionEnabled(canSortFilters);
        sortFiltersButton->setAlpha(1.0f);
    }

    filterViewport.setVisible((getActiveEqlProcessor() != nullptr));
    setPresetsVisible((getActiveEqlProcessor() != nullptr));

    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
    {
        auto* section = filterSections[static_cast<size_t>(filterIndex)].get();

        if (section == nullptr)
            continue;

        const auto orderPosition = getFilterOrderPositionForIndex(filterIndex);
        const auto isActive = (getActiveEqlProcessor() != nullptr) && orderPosition >= 0;
        const auto sectionExpanded = isActive && section->expanded;
        section->body.setVisible(sectionExpanded);
        section->body.setInterceptsMouseClicks(false, true);
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
            section->header->setTitleText(juce::String(orderPosition + 1) + " " + eqlProcessor->getFilterHeaderText(filterIndex));
        else
            section->header->setTitleText({});

        section->header->setVisible(isActive);
        section->header->getProperties().set("textButtonBaseColour", juce::int64(sectionExpanded ? 0xffffffff : 0xffbbbbbb));
        section->header->setSelectionHighlighted(sectionExpanded);
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

    }

    if (addFilterButton != nullptr)
    {
        addFilterButton->setVisible((getActiveEqlProcessor() != nullptr) && presetsSection != nullptr);
        addFilterButton->setInteractionEnabled(true);
        addFilterButton->setAlpha(1.0f);
    }
    if (deleteFilterButton != nullptr)
    {
        deleteFilterButton->setVisible(getActiveEqlProcessor() != nullptr);
        deleteFilterButton->setInteractionEnabled(activeFilterCount > 1);
    }
    for (auto* control : {copyFilterButton.get(), pasteFilterButton.get()})
        if (control != nullptr)
        {
            control->setVisible(getActiveEqlProcessor() != nullptr);
            control->setInteractionEnabled(activeFilterCount > 0);
        }
    if (filterDragSourceIndex >= activeFilterCount) cancelFilterDrag();
}

juce::String AvaAudioProcessorEditor::getInstanceDisplayName() const
{
    const auto& owner = audioProcessor.getRoutingOwner();
    const auto routing = ava::routing::readState(owner.getValueTreeState().state);
    return ava::routing::getDisplayName(routing,
                                         routingInstanceId != 0 ? routingInstanceId : routing.rootInstanceId);
}

void AvaAudioProcessorEditor::updateInstanceHeadings()
{
    auto name = getInstanceDisplayName();
    // Default routing names are INS-1, INS-2, etc. Show them as INS 1.
    if (name.startsWithIgnoreCase("INS-"))
        name = "INS " + name.substring(4);
    const auto caption = "INSTANCE (" + name.toUpperCase() + ")";
    for (auto& frame : instanceContent.controlBlockFrames)
        if (frame.title == "INSTANCE" && frame.displayTitle != caption)
        {
            frame.displayTitle = caption;
            instanceContent.repaint();
        }
}

void AvaAudioProcessorEditor::updateSectionStates()
{
    const juce::ScopeGuard promptVisibility {[this] { applyPromptVisibility(); applyDockVisibility(); }};
    const auto activeFilterCount = getActiveFilterCount();
    if (aboutExpanded) cancelFilterDrag();

    // UNIT MAIN owns ABOUT, with visibility controlled by its
    // collapse state rather than a removed global sidebar.
    if (aboutControl != nullptr)
    {
        aboutControl->setSelectedChoiceIndex(aboutExpanded ? 1 : 0, false);
        aboutControl->setSelectionHighlighted(aboutExpanded);
    }
    if (aboutPanel != nullptr) aboutPanel->setVisible(aboutExpanded);

    if (routingPanel != nullptr)
        routingPanel->setVisible(!unitContent.isBlockCollapsed("UNIT")
            && !unitRoutingContent.isBlockCollapsed("ROUTING") && !aboutExpanded);

    updateInstanceHeadings();

    const auto modulePageVisible = !aboutExpanded;
    if (!modulePageVisible && hostSlotMoveSourceIndex >= 0) cancelHostSlotMove();
    setComponentVisible(crossoverEditor.get(), modulePageVisible);
    const auto moduleContentVisible = modulePageVisible && crossoverEditor != nullptr;

    // UNIT MAIN owns UNDO, REDO, A/B and BYPASS; the nested block manages
    // their visibility so collapsed controls never reappear in the sidebar.
    if (!moduleContentVisible)
    {
        filterViewport.setVisible(false);
        moduleContent.setVisible(false);
        return;
    }
    moduleContent.setVisible(true);
    filterViewport.setVisible(true);
    updateEqlSectionStates(activeFilterCount);
}

void AvaAudioProcessorEditor::showAllSection()
{
    dismissTextPrompt();
    if (audioProcessor.getSelectedModuleCopy() >= ava::module_stack::originalCount) {
        audioProcessor.selectModuleCopy(-1);
        rebindActiveModuleEditors();
    }
    aboutExpanded = false;
    storeEditorStateToValueTree();
    updateSectionStates();
    resized();
    filterViewport.setViewPosition(0, 0);
}

