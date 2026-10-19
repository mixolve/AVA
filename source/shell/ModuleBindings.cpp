#include "Editor.h"
#include "../crossover/ModuleBindings.h"
#include "FilterSection.h"
#include "EditorSetup.h"

void AvaAudioProcessorEditor::rebindActiveModuleEditors()
{
    using namespace crossover_configs;

    if (crossoverEditor == nullptr)
    {
        auto config = makeCrossoverConfig(audioProcessor);
        config.assignHostSlot = [this] (const juce::String& parameterId,
                                        const juce::String& parameterName,
                                        const float normalizedValue)
        {
            return handleHostSlotAssignRequest(parameterId, parameterName, normalizedValue);
        };
        config.onPageChanged = [this]
        {
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
        };
        crossoverEditor = std::make_unique<CrossoverModuleComponent>(std::move(config));
        addAndMakeVisible(*crossoverEditor);
    }

    if (const auto* editor = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get()))
    {
        const auto visibleRange = editor->getVisibleCrossoverRange();

        if (audioProcessor.getSelectedCrossoverRange() != visibleRange)
            audioProcessor.setSelectedCrossoverRange(visibleRange);
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

            refreshFilterPresetList(eqlProcessor->getSelectedFilterPresetName());
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
        auto* currentEditor = dynamic_cast<CrossoverModuleComponent*>(editor.get());

        if (currentEditor == nullptr || currentEditor->getProcessorIdentity() != config.processorIdentity)
        {
            shell_setup_support::removeOwnedChild(*this, editor);
            config.assignHostSlot = [this] (const juce::String& parameterId,
                                            const juce::String& parameterName,
                                            const float normalizedValue)
            {
                return handleHostSlotAssignRequest(parameterId, parameterName, normalizedValue);
            };
            config.onModuleCloseRequest = [this]
            {
                juce::MessageManager::callAsync([safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)]
                {
                    if (safeEditor == nullptr)
                        return;

                    safeEditor->closeActiveModule();
                    clearKeyboardFocus(*safeEditor);
                });
            };
            if(config.moduleKey=="tls") {
                config.showGainArrangePrompt=[this, state=config.valueTreeState](size_t range, tls::gain::Order order, auto preview, auto commit, auto dismiss) {
                    showGainArrangePrompt(*state,range,order,std::move(preview),
                        [this, action=std::move(commit)](const tls::gain::Order& value) {
                            if(action) action(value);
                            scheduleHistorySnapshot();
                        },std::move(dismiss));
                };
                config.showArrangePrompt=[this, state=config.valueTreeState](size_t range, tls::blocks::Order order, auto preview, auto commit, auto dismiss) {
                    showBlockArrangePrompt(*state,range,order,std::move(preview),
                        [this, action=std::move(commit)](const tls::blocks::Order& value) {
                            if(action) action(value);
                            scheduleHistorySnapshot();
                        },std::move(dismiss));
                };
            }
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

        editor->setVisible(moduleLoaded);
    };

    refreshModuleStateListeners();

    rebindEqlEditorSections();
    rebindCrossoverEditor(tlsModuleLoaded,
                          audioProcessor.getTlsModuleProcessor(),
                          tlsModuleEditor,
                          [] (auto& processor) { return makeTlsCrossoverConfig(processor); });
    rebindCrossoverEditor(dynModuleLoaded,
                          audioProcessor.getDynModuleProcessor(),
                          dynModuleEditor,
                          [this] (auto& processor) { return makeDynCrossoverConfig(processor, *this, dynModuleEditor, audioProcessor.getSelectedCrossoverRange()); });
    refreshHostSlotButtons();
}
