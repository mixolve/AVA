#include "Editor.h"
#include "../crossover/ModuleBindings.h"
#include "../crossover/ParameterIds.h"
#include "../modules/dyn/Processor.h"
#include "FilterSection.h"
#include "EditorSetup.h"

void AvaAudioProcessorEditor::rebindActiveModuleEditors()
{
    using namespace crossover_configs;
    quickAccessContent.removeAllChildren();
    quickAccessItems.clear();
    quickAccessOrderCache.clear();

    if (crossoverEditor == nullptr)
    {
        auto config = makeCrossoverConfig(audioProcessor);
        config.uiState = &valueTreeState;
        config.onPageChanged = [this]
        {
            // A BAND/SETT navigation click can reach the fixed left sidebar
            // while an arrangement popup is open. Close that popup before
            // restoring the new page; otherwise both pages get painted.
            if (textPromptOverlay != nullptr
                && bool(textPromptOverlay->getProperties().getWithDefault("hidesModuleContent", false)))
                dismissTextPrompt();
            storeEditorUiState();
            storeFilterDisplayOrderToValueTree();

            if (const auto* editor = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get()))
                audioProcessor.setSelectedCrossoverRange(editor->getVisibleCrossoverRange());

            restoreFilterDisplayOrderFromValueTree();
            rebindActiveModuleEditors();
            restoreEditorUiState();
            refreshHostSlotButtons();
            updateSectionStates();
            resized();
            if (controlSearchPanel != nullptr) controlSearchPanel->refreshMatches();
        };
        crossoverEditor = std::make_unique<CrossoverModuleComponent>(std::move(config));
        addAndMakeVisible(*crossoverEditor);
    }

    if (auto* editor = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get()))
    {
        editor->refreshExternalState();
        const auto visibleRange = editor->getVisibleCrossoverRange();

        if (audioProcessor.getSelectedCrossoverRange() != visibleRange)
            audioProcessor.setSelectedCrossoverRange(visibleRange);
        // An instance UID belongs to one band. Never bind the next band's
        // filter UI to a stale copied EQL from the previous band.
        const auto selectedUid = audioProcessor.getSelectedModuleCopy();
        if (selectedUid >= ava::module_stack::originalCount)
        {
            const auto slots = audioProcessor.getModuleStackForRange(visibleRange);
            if (ava::module_stack::find(slots, selectedUid) == nullptr)
                audioProcessor.selectModuleCopy(-1);
        }
    }

    auto rebindEqlEditorSections = [this]
    {
        if (auto* eqlProcessor = getActiveEqlProcessor())
        {
            auto& eqlState = eqlProcessor->getValueTreeState();

            if (filterSections.front() == nullptr || addFilterButton == nullptr)
            {
                setupEqlControls(eqlState);
            }
            else if (boundEqlState != &eqlState)
            {
                for (auto& section : filterSections)
                    if (section != nullptr)
                        section->rebind(eqlState);

                boundEqlState = &eqlState;
            }

            refreshFilterPresetList();
        }
    };

    auto rebindCrossoverEditor = [this] (const bool moduleLoaded,
                                         auto* processor,
                                         std::unique_ptr<juce::Component>& editor,
                                         auto makeConfig)
    {
        if (! moduleLoaded)
        {
            shell_setup_support::removeOwnedChild(*this, editor);
            return;
        }

        if (processor == nullptr)
            return;

        auto config = makeConfig(*processor);
        config.uiState = &valueTreeState;
        // Originals have a separate scope from every copied module.
        config.collapseScope = config.moduleKey + ".original";
        auto* currentEditor = dynamic_cast<CrossoverModuleComponent*>(editor.get());

        if (currentEditor == nullptr || currentEditor->getProcessorIdentity() != config.processorIdentity)
        {
            shell_setup_support::removeOwnedChild(*this, editor);
            editor = std::make_unique<CrossoverModuleComponent>(std::move(config));
            addAndMakeVisible(*editor);
        }
        else
        {
            currentEditor->refreshExternalState();
        }

        if (auto* crossoverModuleEditor = dynamic_cast<CrossoverModuleComponent*>(editor.get()))
        {
            const auto* crossover = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get());
            crossoverModuleEditor->setExternalCrossoverRange(crossover != nullptr ? crossover->getVisibleCrossoverRange() : 0);
        }

        const auto* crossover = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get());
        editor->setVisible(moduleLoaded && !hasUnitPage()
            && crossover != nullptr);
    };

    refreshModuleStateListeners();

    rebindEqlEditorSections();
    rebindCrossoverEditor(audioProcessor.getBlockChainProcessor() != nullptr,
                          audioProcessor.getBlockChainProcessor(),
                          blockChainModuleEditor,
                          [] (auto& processor) { return makeBlockChainCrossoverConfig(processor); });
    rebindCrossoverEditor(audioProcessor.getDynModuleProcessor() != nullptr,
                          audioProcessor.getDynModuleProcessor(),
                          dynModuleEditor,
                          [this] (auto& processor) { return makeDynCrossoverConfig(processor, *this, audioProcessor.getSelectedCrossoverRange()); });
    refreshHostSlotButtons();
}
