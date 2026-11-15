#include "Editor.h"
#include "ChoiceControl.h"
#include "PresetSection.h"
#include "../modules/eql/RangeBank.h"

void AvaAudioProcessorEditor::setupPresetControls()
{
    presetsSection = std::make_unique<PresetsSection>();
    presetsSection->onPresetSelected = [this]
    {
        if (presetsSection == nullptr)
            return;

        const auto presetName = presetsSection->getSelectedPresetName();

        if (presetName.isEmpty())
        {
            refreshFilterPresetList();
            return;
        }

        // EQL presets replace a module ValueTree in one operation. Record
        // complete host snapshots around the operation: individual parameter
        // listeners cannot be relied upon to see a replaceState() transaction.
        auto& owner = audioProcessor.getRoutingOwner();
        auto* historyEditor = owner.getContentEditor();
        if (historyEditor != nullptr)
            historyEditor->commitPendingHistorySnapshot(true);

        juce::MemoryBlock before;
        owner.getStateInformationForABCompareSnapshot(before);

        // Capture the actual EQL bank BEFORE applying the preset. Unlike an
        // outer-plugin snapshot, these bytes can be restored directly into the
        // same instance and cannot be lost in routing-state reconstruction.
        const auto instanceId = audioProcessor.getRoutingInstanceId();
        juce::MemoryBlock eqlBefore;
        if (auto* bank = audioProcessor.getEqlProcessorBank())
            bank->getStateInformation(eqlBefore);

        bool loaded = false;
        {
            const juce::ScopedValueSetter<bool> suppressHandlers(suppressFilterSectionValueChangeHandlers, true);
            if (auto* eqlProcessor = getActiveEqlProcessor(); eqlProcessor != nullptr)
                loaded = eqlProcessor->loadFilterPreset(presetName);
            if (loaded)
                reloadFilterPresetFromProcessor();
        }
        refreshFilterPresetList();

        if (loaded)
        {
            juce::MemoryBlock after;
            owner.getStateInformationForABCompareSnapshot(after);
            juce::MemoryBlock eqlAfter;
            if (auto* bank = audioProcessor.getEqlProcessorBank())
                bank->getStateInformation(eqlAfter);
            if (historyEditor != nullptr)
            {
                if (audioProcessor.getSelectedModuleCopy() >= ava::module_stack::originalCount)
                    // Undo for a duplicate must restore the whole module stack:
                    // the EQL-only history path targets the ORIGINAL bank.
                    historyEditor->scheduleHistorySnapshot();
                else
                    historyEditor->recordPresetHistoryChange(before, after, instanceId, eqlBefore, eqlAfter);
            }
            // Preset data is stored in a nested EQL processor, not in the
            // outer APVTS. Explicitly mark the complete host state dirty.
            audioProcessor.notifyHostOfStateChange();
            if (historyEditor != nullptr)
                historyEditor->observedStateRevision = owner.getStateRevision();
        }
    };
    presetsSection->addButton->onTrigger = [this]
    {
        addFilterPreset();
        clearKeyboardFocus(*this);
    };
    presetsSection->saveButton->onTrigger = [this]
    {
        saveFilterPreset();
        clearKeyboardFocus(*this);
    };
    presetsSection->renameButton->onTrigger = [this]
    {
        if (presetsSection != nullptr)
            presetsSection->beginRename();
    };
    presetsSection->defaultButton->onTrigger = [this]
    {
        setDefaultFilterPreset();
        clearKeyboardFocus(*this);
    };
    presetsSection->copyFiltersButton->onTrigger = [this] { copyAllEqlFilters(); };
    presetsSection->pasteFilterButton->onTrigger = [this] { pasteAllEqlFilters(); };
    presetsSection->deleteButton->onTrigger = [this]
    {
        deleteSelectedFilterPreset();
        clearKeyboardFocus(*this);
    };
    eqlContent.addAndMakeVisible(presetsSection->presetChoice);
    for (auto* button : { presetsSection->addButton.get(), presetsSection->saveButton.get(),
            presetsSection->renameButton.get(), presetsSection->defaultButton.get(), presetsSection->deleteButton.get(),
            presetsSection->copyFiltersButton.get(), presetsSection->pasteFilterButton.get() })
        eqlContent.addAndMakeVisible(*button);
    presetsSection->onRenameRequested = [this] (const juce::String& currentName)
    {
        const auto promptBounds = getLocalArea(&presetsSection->presetChoice, presetsSection->presetChoice.getLocalBounds());

        showTextPrompt(currentName,
                       [this, currentName] (const juce::String& newName)
                       {
                           if (! renameFilterPreset(currentName, newName))
                               return false;

                           clearKeyboardFocus(*this);
                           return true;
                       },
                       promptBounds, {}, {}, true, nullptr, {}, {}, {}, &presetsSection->presetChoice);
    };
    refreshFilterPresetList();
}
