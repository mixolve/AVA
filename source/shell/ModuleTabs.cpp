#include "Editor.h"
#include "Controls.h"
#include "FilterSection.h"
#include "PresetSection.h"
#include "FilterOrderState.h"
#include "../crossover/Editor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/Processor.h"
#include "../modules/tls/Processor.h"

#include <algorithm>
#include <utility>
#include <vector>

void AvaAudioProcessorEditor::showModulePicker()
{
    if (moduleAddButton == nullptr)
        return;

    const auto canLoadModule = audioProcessor.getActiveModule() == AvaAudioProcessor::ActiveModule::none;

    if (! canLoadModule)
        return;

    auto anchorBounds = getLocalArea(moduleAddButton.get(), moduleAddButton->getLocalBounds());
    anchorBounds.setSize(juce::jmax(120, anchorBounds.getWidth()), anchorBounds.getHeight());
    anchorBounds.setCentre(getLocalArea(moduleAddButton.get(), moduleAddButton->getLocalBounds()).getCentre());

    juce::StringArray moduleNames;
    for (const auto module : AvaAudioProcessor::availableModules)
        moduleNames.add(juce::String(AvaAudioProcessor::stateIdForModule(module)).toUpperCase());

    showChoicePrompt(anchorBounds,
                     moduleNames,
                     -1,
                     std::vector<bool>(AvaAudioProcessor::availableModules.size(), true),
                     juce::Justification::centred,
                       [safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)] (const int selectedIndex)
                       {
                           const auto& moduleOrder = AvaAudioProcessor::availableModules;

                           if (safeEditor != nullptr && juce::isPositiveAndBelow(selectedIndex, static_cast<int>(moduleOrder.size())))
                               safeEditor->loadModule(moduleOrder[static_cast<size_t>(selectedIndex)]);
                       },
                     {},
                     {},true);
}

void AvaAudioProcessorEditor::closeActiveModule()
{
    if (audioProcessor.getActiveModule() == AvaAudioProcessor::ActiveModule::none)
        return;

    cancelHostSlotMove();

    std::vector<juce::String> closingModuleParameterIds;
    const auto collectParameterIds = [&closingModuleParameterIds] (const auto* processor)
    {
        if (processor == nullptr)
            return;

        for (const auto parameterState : processor->getValueTreeState().state)
        {
            const auto parameterId = parameterState.getProperty("id").toString();

            if (parameterId.isNotEmpty())
                closingModuleParameterIds.push_back(parameterId);
        }
    };

    switch (audioProcessor.getActiveModule())
    {
        case AvaAudioProcessor::ActiveModule::eql: collectParameterIds(audioProcessor.getEqlModuleProcessor()); break;
        case AvaAudioProcessor::ActiveModule::tls: collectParameterIds(audioProcessor.getTlsModuleProcessor()); break;
        case AvaAudioProcessor::ActiveModule::dyn: collectParameterIds(audioProcessor.getDynModuleProcessor()); break;
        case AvaAudioProcessor::ActiveModule::none: break;
    }

    detachModuleEditorBindings();

    if (! audioProcessor.clearLoadedModule())
        return;

    setLoadedModuleFlags(AvaAudioProcessor::ActiveModule::none);

    for (int slotIndex = 0; slotIndex < static_cast<int>(hostSlotAssignments.size()); ++slotIndex)
    {
        const auto& assignment = hostSlotAssignments[static_cast<size_t>(slotIndex)];

        if (std::find(closingModuleParameterIds.begin(),
                      closingModuleParameterIds.end(),
                      assignment.parameterId) != closingModuleParameterIds.end())
        {
            clearHostSlot(slotIndex);
        }
    }

    filterViewport.setVisible(false);
    filterViewport.setBounds({});
    filterContent.setSize(0, 0);

    auto hideComponent = [] (juce::Component* component)
    {
        if (component == nullptr)
            return;

        component->setVisible(false);
        component->setBounds({});
    };

    hideComponent(addFilterButton.get());
    hideComponent(sortFiltersButton.get());

    if (presetsSection != nullptr)
    {
        hideComponent(presetsSection->actionsToggleButton.get());
        hideComponent(&presetsSection->presetCombo);
        hideComponent(presetsSection->addButton.get());
        hideComponent(presetsSection->saveButton.get());
        hideComponent(presetsSection->renameButton.get());
        hideComponent(presetsSection->defaultButton.get());
        hideComponent(presetsSection->deleteButton.get());
    }

    for (auto& section : filterSections)
    {
        if (section == nullptr)
            continue;

        section->expanded = false;
        hideComponent(section->header.get());
        hideComponent(section->typeControl.get());
        hideComponent(section->orderControl.get());
        hideComponent(section->frequencyControl.get());
        hideComponent(section->bandwidthControl.get());
        hideComponent(section->gainControl.get());
        hideComponent(section->bypassButton.get());
    }

    rebindActiveModuleEditors();
    syncEditorWidthToBounds();
    ensureModuleTitle();
    storeEditorStateToValueTree();
    updateSectionStates();
    resized();
    if (moduleAddButton != nullptr)
        moduleAddButton->inheritHover(moduleAddButton->getLocalBounds().contains(moduleAddButton->getMouseXYRelative()));
    scheduleHistorySnapshot();
}

void AvaAudioProcessorEditor::loadModule(const AvaAudioProcessor::ActiveModule module)
{
    if (module == AvaAudioProcessor::ActiveModule::none || ! audioProcessor.loadModule(module))
        return;

    setLoadedModuleFlags(module);
    hostParametersExpanded = false;
    rebindActiveModuleEditors();

    if (module == AvaAudioProcessor::ActiveModule::eql)
    {
        filterDisplayOrder = shell_filter_order_state::makeIdentity(EqlModuleProcessor::maxFilterCount);
        enforceSingleExpandedFilterSection();
        storeFilterDisplayOrderToValueTree();
    }

    syncEditorWidthToBounds();
    ensureModuleTitle();
    storeEditorStateToValueTree();
    updateSectionStates();
    resized();
    if (moduleTitle != nullptr)
        moduleTitle->inheritHover(moduleTitle->getLocalBounds().contains(moduleTitle->getMouseXYRelative()));
    scheduleHistorySnapshot();
}

void AvaAudioProcessorEditor::ensureModuleTitle()
{
    if (moduleTitle == nullptr)
    {
        moduleTitle = std::make_unique<BoxTextButton>(uiGreyLight);
        moduleTitle->getProperties().set(juce::Identifier("oscParameterId"),
                                         AvaAudioProcessor::oscCloseModuleId);
        moduleTitle->setTextJustification(juce::Justification::centred);
        moduleTitle->setActiveFill(false);
        moduleTitle->setToggleActiveFillEnabled(false);
        moduleTitle->setLongPressPromptActions([this]
        {
            // The shell owns this button across module replacement. Complete
            // replacement in the same event, without painting the old title first.
            closeActiveModule();
            clearKeyboardFocus(*this);
        });
        moduleTitle->setLongPressPrimaryPromptIcon("x");
        addChildComponent(*moduleTitle);
    }

    if (auto* crossover = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get()))
        crossover->setModuleActionButtons(*moduleAddButton, *moduleTitle);
}

void AvaAudioProcessorEditor::toggleHostParametersSection()
{
    hostParametersExpanded = ! hostParametersExpanded;
    if (hostParametersExpanded)
    {
        routingExpanded = false;
        oscExpanded = false;
    }

    storeEditorStateToValueTree();
    updateSectionStates();
    resized();
}
