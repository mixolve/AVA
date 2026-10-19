#include "Editor.h"
#include "PresetSection.h"

void AvaAudioProcessorEditor::setupPresetControls()
{
    presetsSection = std::make_unique<PresetsSection>();
    presetsSection->onPresetSelected = [this]
    {
        if (presetsSection == nullptr)
            return;

        const auto presetName = presetsSection->getSelectedPresetName();

        if (presetName.isEmpty())
            return;

        const juce::ScopedValueSetter<bool> suppressHandlers(suppressFilterSectionValueChangeHandlers, true);

        if (auto* eqlProcessor = getActiveEqlProcessor(); eqlProcessor != nullptr && eqlProcessor->loadFilterPreset(presetName))
        {
            reloadFilterPresetFromProcessor();
            refreshFilterPresetList(presetName);
        }
    };
    presetsSection->actionsReveal.onUpdate = [safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)]
    {
        if (safeEditor != nullptr)
        {
            safeEditor->updateSectionStates();
            safeEditor->resized();
        }
    };
    presetsSection->actionsToggleButton->onClick = [this]
    {
        presetsSection->actionsToggleButton->setTablerIcon(
            presetsSection->actionsToggleButton->getToggleState() ? "arrows-up" : "arrows-down");
        presetsSection->actionsReveal.setExpanded(presetsSection->actionsToggleButton->getToggleState());
        updateSectionStates();
        resized();
        clearKeyboardFocus(*this);
    };
    presetsSection->addButton->onClick = [this]
    {
        addFilterPreset();
        clearKeyboardFocus(*this);
    };
    presetsSection->saveButton->onClick = [this]
    {
        saveFilterPreset();
        clearKeyboardFocus(*this);
    };
    presetsSection->renameButton->onClick = [this]
    {
        if (presetsSection != nullptr)
            presetsSection->beginRename();
    };
    presetsSection->defaultButton->onClick = [this]
    {
        setDefaultFilterPreset();
        clearKeyboardFocus(*this);
    };
    presetsSection->deleteButton->onClick = [this]
    {
        clearKeyboardFocus(*this);
    };
    presetsSection->deleteButton->setLongPressPromptActions([this]
    {
        deleteSelectedFilterPreset();
        clearKeyboardFocus(*this);
    });
    presetsSection->deleteButton->setLongPressPrimaryPromptIcon("trash");
    addAndMakeVisible(*presetsSection->actionsToggleButton);
    addAndMakeVisible(presetsSection->presetClip);
    addAndMakeVisible(presetsSection->actionsClip);
    presetsSection->presetClip.addAndMakeVisible(presetsSection->presetCombo);
    presetsSection->presetClip.addAndMakeVisible(*presetsSection->addButton);
    presetsSection->actionsClip.addAndMakeVisible(*presetsSection->saveButton);
    presetsSection->actionsClip.addAndMakeVisible(*presetsSection->renameButton);
    presetsSection->actionsClip.addAndMakeVisible(*presetsSection->defaultButton);
    presetsSection->actionsClip.addAndMakeVisible(*presetsSection->deleteButton);
    presetsSection->onRenameRequested = [this] (const juce::String& currentName)
    {
        auto promptBounds = moduleTitle != nullptr ? getLocalArea(moduleTitle.get(), moduleTitle->getLocalBounds())
                                                   : juce::Rectangle<int>();

        if (! promptBounds.isEmpty())
            promptBounds.setY(uiGap);

        showTextPrompt(currentName,
                       [this, currentName] (const juce::String& newName)
                       {
                           if (! renameFilterPreset(currentName, newName))
                               return false;

                           clearKeyboardFocus(*this);
                           return true;
                       },
                       promptBounds);
    };
    if (auto* eqlProcessor = getActiveEqlProcessor())
        refreshFilterPresetList(eqlProcessor->getSelectedFilterPresetName());
    else
        refreshFilterPresetList({});
}
