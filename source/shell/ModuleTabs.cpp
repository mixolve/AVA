#include "Editor.h"
#include "Controls.h"
#include "FilterSection.h"
#include "PresetSections.h"
#include "FilterOrderState.h"
#include "../crossover/Component.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/Processor.h"
#include "../modules/fft/Processor.h"
#include "../modules/tls/Processor.h"
#include "../modules/trs/Processor.h"

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

    showChoicePrompt(anchorBounds,
                     { "TLS", "EQL", "FFT", "DYN", "TRS" },
                     -1,
                     { canLoadModule, canLoadModule, canLoadModule, canLoadModule, canLoadModule },
                     juce::Justification::centred,
                       [safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)] (const int selectedIndex)
                       {
                           static constexpr std::array moduleOrder {
                               AvaAudioProcessor::ActiveModule::tls,
                               AvaAudioProcessor::ActiveModule::eql,
                               AvaAudioProcessor::ActiveModule::fft,
                               AvaAudioProcessor::ActiveModule::dyn,
                               AvaAudioProcessor::ActiveModule::trs
                           };

                           if (safeEditor != nullptr && juce::isPositiveAndBelow(selectedIndex, static_cast<int>(moduleOrder.size())))
                               safeEditor->loadModule(moduleOrder[static_cast<size_t>(selectedIndex)]);
                       },
                     {},
                     {});
}

void AvaAudioProcessorEditor::closeActiveModule()
{
    if (audioProcessor.getActiveModule() == AvaAudioProcessor::ActiveModule::none)
        return;

    hostSlotMoveSourceIndex = -1;
    for (const auto& field : hostSlotNameFields)
        if (field != nullptr)
            field->setDragTargetOutlineVisible(false);

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
        case AvaAudioProcessor::ActiveModule::fft: collectParameterIds(audioProcessor.getFftModuleProcessor()); break;
        case AvaAudioProcessor::ActiveModule::tls: collectParameterIds(audioProcessor.getTlsModuleProcessor()); break;
        case AvaAudioProcessor::ActiveModule::dyn: collectParameterIds(audioProcessor.getDynModuleProcessor()); break;
        case AvaAudioProcessor::ActiveModule::trs: collectParameterIds(audioProcessor.getTrsModuleProcessor()); break;
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
    hideComponent(sortPlaceButton.get());
    hideComponent(sortFreqButton.get());
    hideComponent(sortDuoButton.get());

    if (presetsSection != nullptr)
    {
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
        hideComponent(section->placeControl.get());
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
    scheduleHistorySnapshot();
}

void AvaAudioProcessorEditor::ensureModuleTitle()
{
    if (moduleTitle == nullptr)
    {
        moduleTitle = std::make_unique<BoxTextButton>(uiGrey500);
        moduleTitle->getProperties().set(juce::Identifier("oscParameterId"),
                                         AvaAudioProcessor::oscCloseModuleId);
        moduleTitle->setTextJustification(juce::Justification::centred);
        moduleTitle->setAlwaysAccentOutline(false);
        moduleTitle->setToggleAccentVisible(false);
        moduleTitle->setLongPressPromptActions([this]
        {
            juce::MessageManager::callAsync([safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)]
            {
                if (safeEditor == nullptr)
                    return;

                safeEditor->closeActiveModule();
                clearKeyboardFocus(*safeEditor);
            });
        }, {}, "CLOSE?");
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
