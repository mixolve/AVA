#include "Editor.h"
#include "PresetSections.h"

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
    presetsSection->filterActionsToggleButton->onClick = [this]
    {
        presetsSection->filterActionsExpanded = presetsSection->filterActionsToggleButton->getToggleState();
        if (presetsSection->filterActionsExpanded)
        {
            presetsSection->actionsExpanded = false;
            presetsSection->actionsToggleButton->setToggleState(false, juce::dontSendNotification);
        }
        setPresetsVisible(eqlModuleLoaded);
        resized();
        clearKeyboardFocus(*this);
    };
    presetsSection->actionsToggleButton->onClick = [this]
    {
        presetsSection->actionsExpanded = presetsSection->actionsToggleButton->getToggleState();
        if (presetsSection->actionsExpanded)
        {
            presetsSection->filterActionsExpanded = false;
            presetsSection->filterActionsToggleButton->setToggleState(false, juce::dontSendNotification);
        }
        setPresetsVisible(eqlModuleLoaded);
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
    presetsSection->deleteButton->setLongPressAction([this]
    {
        deleteSelectedFilterPreset();
        clearKeyboardFocus(*this);
    }, 500, "S?");
    addAndMakeVisible(presetsSection->presetCombo);
    addAndMakeVisible(*presetsSection->filterActionsToggleButton);
    addAndMakeVisible(*presetsSection->actionsToggleButton);
    addAndMakeVisible(*presetsSection->addButton);
    addAndMakeVisible(*presetsSection->saveButton);
    addAndMakeVisible(*presetsSection->renameButton);
    addAndMakeVisible(*presetsSection->defaultButton);
    addAndMakeVisible(*presetsSection->deleteButton);
    presetsSection->onRenameRequested = [this] (const juce::String& currentName)
    {
        auto promptBounds = moduleTitle != nullptr ? getLocalArea(moduleTitle.get(), moduleTitle->getLocalBounds())
                                                   : juce::Rectangle<int>();

        if (! promptBounds.isEmpty())
            promptBounds.setY(getEditorInsetTop(getHeight()));

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
