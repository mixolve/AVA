#include "Editor.h"
#include "EqlActions.h"
#include "FilterSection.h"
#include "../modules/eql/FilterSupport.h"
#include "../modules/eql/Processor.h"
#include "../modules/eql/RangeBank.h"

#include <cmath>
#include <utility>

namespace
{

}

void AvaAudioProcessorEditor::setupEqlControls(juce::AudioProcessorValueTreeState& initialEqlState)
{
    boundEqlState = &initialEqlState;

    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
    {
        auto orderLabel = std::make_unique<BoxTextButton>(uiGreyLight);
        orderLabel->setButtonText(juce::String::formatted("%02d", filterIndex + 1));
        orderLabel->setTextJustification(juce::Justification::centred);
        orderLabel->setFillVisible(false);
        orderLabel->setInteractionOutlineEnabled(false);
        orderLabel->onClick = [this, filterIndex]
        {
            if (! juce::isPositiveAndBelow(filterMoveSourceIndex, getActiveFilterCount()))
                return;

            const auto sourceIndex = filterMoveSourceIndex;
            filterMoveSourceIndex = -1;

            for (const auto& label : filterOrderLabels)
                if (label != nullptr)
                {
                    label->setDragTargetHighlighted(false);
                    label->setMoveTargetAvailable(false);
                }

            moveFilterSectionTo(sourceIndex, filterIndex);

            if (auto* label = filterOrderLabels[static_cast<size_t>(filterIndex)].get())
                label->flashConfirmationFill();

            clearKeyboardFocus(*this);
        };

        auto section = std::make_unique<FilterSection>(initialEqlState, filterIndex);
        section->frequencyControl->onTitleClick = [this, filterIndex]
        {
            auto* filterSection = filterSections[static_cast<size_t>(filterIndex)].get();

            if (filterSection == nullptr)
                return;

            filterSection->frequencyControl->setValue(defaultFilterFrequencyHz, true);
        };
        section->bandwidthControl->onTitleClick = [this, filterIndex]
        {
            auto* filterSection = filterSections[static_cast<size_t>(filterIndex)].get();

            if (filterSection == nullptr)
                return;

            filterSection->bandwidthControl->setValue(defaultFilterBandwidthOctaves, true);
        };
        section->orderControl->onTitleClick = [this, filterIndex]
        {
            auto* filterSection = filterSections[static_cast<size_t>(filterIndex)].get();

            if (filterSection == nullptr)
                return;

            filterSection->orderControl->setSelectedChoiceIndex(
                EqlModuleProcessor::getOrderChoiceForSlopeDbPerOct(defaultFilterSlopeDbPerOct),
                true);
        };
        section->gainControl->onTitleClick = [this, filterIndex]
        {
            auto* filterSection = filterSections[static_cast<size_t>(filterIndex)].get();

            if (filterSection == nullptr)
                return;

            filterSection->gainControl->setValue(0.0, true);
        };
        section->typeControl->onValueChanged = [this, filterIndex]
        {
            if (suppressFilterSectionValueChangeHandlers)
                return;

            auto* filterSection = filterSections[static_cast<size_t>(filterIndex)].get();

            if (filterSection == nullptr)
                return;

            filterSection->refreshTypeDependentControls();
            updateSectionStates();
            resized();
        };
        const auto updateStateOnValueChange = [this]
        {
            if (! suppressFilterSectionValueChangeHandlers)
                updateSectionStates();
        };
        section->frequencyControl->onValueChanged = updateStateOnValueChange;
        section->phaseButton->onClick = updateStateOnValueChange;
        section->phaseButton->setLongPressPromptActions({}, [this, filterIndex]
        {
            const auto id = EqlModuleProcessor::getFilterPhaseParamId(filterIndex);
            if (auto* parameter = findHostAssignableParameter(id))
                handleHostSlotAssignRequest(id, "PHASE", parameter->getValue());
        });
        section->bandwidthControl->onValueChanged = updateStateOnValueChange;
        section->orderControl->onValueChanged = [this, filterIndex]
        {
            if (suppressFilterSectionValueChangeHandlers)
                return;

            normalizeOrderForType(filterIndex);

            updateSectionStates();
        };
        section->gainControl->onValueChanged = updateStateOnValueChange;
        section->header->onClick = [this, filterIndex]
        {
            auto* filterSection = filterSections[static_cast<size_t>(filterIndex)].get();

            if (filterSection == nullptr)
                return;

            enforceSingleExpandedFilterSection(filterSection->expanded ? -1 : filterIndex);
            storeEditorStateToValueTree();
            updateSectionStates();
            resized();
            clearKeyboardFocus(*this);
        };
        section->header->setLongPressPromptActions([this, filterIndex]
        {
            const juce::ScopedValueSetter<bool> suppressHandlers(suppressFilterSectionValueChangeHandlers, true);
            auto* eqlProcessor = getActiveEqlProcessor();
            const auto previousCount = getActiveFilterCount();

            if (eqlProcessor != nullptr && eqlProcessor->removeFilter(filterIndex))
            {
                removeFilterSectionUiState(filterIndex, previousCount);
                enforceSingleExpandedFilterSection();
                storeEditorStateToValueTree();
                updateSectionStates();
                resized();
            }

            clearKeyboardFocus(*this);
        });
        section->header->setLongPressPrimaryPromptIcon("trash");
        section->header->onMoveArmed = [this, filterIndex]
        {
            const auto sourceOrderPosition = getFilterOrderPositionForIndex(filterIndex);

            if (sourceOrderPosition < 0)
                return;

            filterMoveSourceIndex = filterIndex;

            for (const auto& label : filterOrderLabels)
                if (label != nullptr)
                {
                    const auto isSource = label.get() == filterOrderLabels[static_cast<size_t>(sourceOrderPosition)].get();
                    label->setDragTargetHighlighted(isSource);
                    label->setMoveTargetAvailable(!isSource);
                }
        };
        const auto assignBypassToHostSlot = [this, filterIndex]
        {
            const auto parameterId = EqlModuleProcessor::getFilterBypassParamId(filterIndex);

            if (auto* parameter = findHostAssignableParameter(parameterId))
                handleHostSlotAssignRequest(parameterId, "BP", parameter->getValue());
        };
        section->header->setLongPressTrailingPromptIconAction(assignBypassToHostSlot, "map-pin-share");
        section->bypassButton->setLongPressPromptActions({}, assignBypassToHostSlot);
        section->bypassButton->onClick = [this]
        {
            updateSectionStates();
            clearKeyboardFocus(*this);
        };
        section->disclosure.onUpdate = [safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this), filterIndex]
        {
            if (safeEditor != nullptr)
            {
                auto* animated = safeEditor->filterSections[static_cast<size_t>(filterIndex)].get();
                if (animated == nullptr) return;
                const auto opacity = static_cast<float>(animated->disclosure.value());
                animated->body.setAlpha(opacity);
                animated->accentFrame.setAlpha(opacity);
                if (! animated->disclosure.isRunning()) safeEditor->updateSectionStates();
                safeEditor->resized();
            }
        };
        filterContent.addChildComponent(section->body);
        filterContent.addChildComponent(section->accentFrame);
        filterContent.addAndMakeVisible(*orderLabel);
        filterContent.addAndMakeVisible(*section->header);
        section->body.addAndMakeVisible(*section->typeControl);
        section->body.addAndMakeVisible(*section->frequencyControl);
        section->body.addAndMakeVisible(*section->bandwidthControl);
        section->body.addAndMakeVisible(*section->orderControl);
        section->body.addAndMakeVisible(*section->gainControl);
        filterContent.addAndMakeVisible(*section->bypassButton);
        filterOrderLabels[static_cast<size_t>(filterIndex)] = std::move(orderLabel);
        filterSections[static_cast<size_t>(filterIndex)] = std::move(section);

        normalizeOrderForType(filterIndex);
    }

    addFilterButton = std::make_unique<BoxTextButton>(uiGreyLight);
    addFilterButton->setButtonText("ADD-FILTER");
    addFilterButton->onClick = [this]
    {
        const juce::ScopedValueSetter<bool> suppressHandlers(suppressFilterSectionValueChangeHandlers, true);
        auto* eqlProcessor = getActiveEqlProcessor();

        if (eqlProcessor != nullptr && eqlProcessor->addFilter())
        {
            const auto newFilterIndex = getActiveFilterCount() - 1;
            filterDisplayOrder[static_cast<size_t>(newFilterIndex)] = newFilterIndex;
            resetFilterSectionUiState(getActiveFilterCount() - 1);
            enforceSingleExpandedFilterSection(newFilterIndex);
            storeEditorStateToValueTree();
            selectFilterSection(newFilterIndex);
        }

        clearKeyboardFocus(*this);
    };
    addFilterButton->setLongPressPromptActions([this]
    {
        clearAllFilters();
        clearKeyboardFocus(*this);
    });
    addFilterButton->setLongPressPrimaryPromptIcon("trash");
    addAndMakeVisible(*addFilterButton);
}

bool AvaAudioProcessorEditor::handleOscEqlAction(const size_t bandIndex,
                                                  const juce::String& action,
                                                  const float value)
{
    if (audioProcessor.getActiveModule() != AvaAudioProcessor::ActiveModule::eql
        || bandIndex >= audioProcessor.getCrossoverSettings().activeSplitCount + 1)
        return false;

    auto* bank = audioProcessor.getEqlProcessorBank();
    auto* eqlProcessor = bank != nullptr ? bank->getProcessor(bandIndex) : nullptr;
    if (eqlProcessor == nullptr)
        return false;

    const auto selectedBand = bandIndex == audioProcessor.getSelectedCrossoverRange();
    const auto filterDeleteIndex = parseEqlFilterDeleteActionIndex(action);

    if (filterDeleteIndex >= 0)
    {
        if (std::abs(value - 1.0f) > 1.0e-6f)
            return false;

        const auto previousCount = eqlProcessor->getActiveFilterCount();
        const juce::ScopedValueSetter<bool> suppressHandlers(suppressFilterSectionValueChangeHandlers, true);
        if (! eqlProcessor->removeFilter(filterDeleteIndex))
            return false;

        if (selectedBand)
        {
            removeFilterSectionUiState(filterDeleteIndex, previousCount);
            enforceSingleExpandedFilterSection();
            storeEditorStateToValueTree();
            updateSectionStates();
            resized();
        }

        scheduleHistorySnapshot();
        return true;
    }

    if (action == "add")
    {
        if (std::abs(value - 1.0f) > 1.0e-6f || ! eqlProcessor->addFilter())
            return false;

        if (selectedBand)
        {
            const auto newFilterIndex = eqlProcessor->getActiveFilterCount() - 1;
            filterDisplayOrder[static_cast<size_t>(newFilterIndex)] = newFilterIndex;
            resetFilterSectionUiState(newFilterIndex);
            enforceSingleExpandedFilterSection(newFilterIndex);
            storeEditorStateToValueTree();
            selectFilterSection(newFilterIndex);
        }

        scheduleHistorySnapshot();
        return true;
    }

    if (action == "delete-all.hidden")
    {
        if (std::abs(value - 1.0f) > 1.0e-6f)
            return false;

        if (selectedBand)
        {
            clearAllFilters();
            return true;
        }

        if (! eqlProcessor->clearFilters())
            return false;

        scheduleHistorySnapshot();
        return true;
    }

    if (action == "preset")
    {
        const auto presetNames = eqlProcessor->getFilterPresetNames();
        const auto presetIndex = getOscChoiceIndex(value, presetNames.size());
        if (presetIndex < 0)
            return false;

        const auto presetName = presetNames[presetIndex];
        if (! eqlProcessor->loadFilterPreset(presetName))
            return false;

        if (selectedBand)
        {
            reloadFilterPresetFromProcessor();
            refreshFilterPresetList(presetName);
        }

        scheduleHistorySnapshot();
        return true;
    }

    return false;
}
