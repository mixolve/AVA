#include "Editor.h"
#include "FilterSection.h"
#include "../modules/eql/FilterSupport.h"
#include "../modules/eql/Processor.h"
#include "../modules/eql/ProcessorBank.h"

#include <cmath>
#include <utility>

namespace
{
int parseFilterDeleteActionIndex(const juce::String& action)
{
    constexpr auto prefix = "filter-";
    constexpr auto suffix = "_delete.hidden";

    if (! action.startsWith(prefix) || ! action.endsWith(suffix))
        return -1;

    const auto numberText = action.substring(juce::String(prefix).length(),
                                             action.length() - juce::String(suffix).length());
    if (numberText.isEmpty() || ! numberText.containsOnly("0123456789"))
        return -1;

    return numberText.getIntValue() - 1;
}
}

void AvaAudioProcessorEditor::setupEqlControls(juce::AudioProcessorValueTreeState& initialEqlState)
{
    boundEqlState = &initialEqlState;

    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
    {
        auto orderLabel = std::make_unique<BoxTextButton>(uiGrey500);
        orderLabel->setButtonText(juce::String::formatted("%02d", filterIndex + 1));
        orderLabel->setTextJustification(juce::Justification::centred);
        orderLabel->setFillVisible(false);
        orderLabel->setPressFillEnabled(false);
        orderLabel->onClick = [this, filterIndex]
        {
            if (! juce::isPositiveAndBelow(filterMoveSourceIndex, getActiveFilterCount()))
                return;

            const auto sourceIndex = filterMoveSourceIndex;
            filterMoveSourceIndex = -1;

            for (const auto& label : filterOrderLabels)
                if (label != nullptr)
                    label->setDragTargetOutlineVisible(false);

            moveFilterSectionTo(sourceIndex, filterIndex);

            if (auto* label = filterOrderLabels[static_cast<size_t>(filterIndex)].get())
                label->flashConfirmationOutline();

            clearKeyboardFocus(*this);
        };

        auto section = std::make_unique<FilterSection>(initialEqlState, filterIndex);
        section->placeControl->onTitleClick = [this, filterIndex]
        {
            auto* filterSection = filterSections[static_cast<size_t>(filterIndex)].get();

            if (filterSection == nullptr)
                return;

            filterSection->placeControl->setSelectedChoiceIndex(0, true);
        };
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
        section->placeControl->onValueChanged = updateStateOnValueChange;
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
        }, {}, "D?");
        section->header->onMoveArmed = [this, filterIndex]
        {
            const auto sourceOrderPosition = getFilterOrderPositionForIndex(filterIndex);

            if (sourceOrderPosition < 0)
                return;

            filterMoveSourceIndex = filterIndex;

            for (const auto& label : filterOrderLabels)
                if (label != nullptr)
                    label->setDragTargetOutlineVisible(label.get() == filterOrderLabels[static_cast<size_t>(sourceOrderPosition)].get());
        };
        section->header->setLongPressTrailingPromptIconAction([this, filterIndex]
        {
            const auto parameterId = EqlModuleProcessor::getFilterBypassParamId(filterIndex);

            if (auto* parameter = findHostAssignableParameter(parameterId))
                handleHostSlotAssignRequest(parameterId, "BP", parameter->getValue());
        }, "map-pin-share");
        section->bypassButton->onClick = [this]
        {
            updateSectionStates();
            clearKeyboardFocus(*this);
        };
        filterContent.addAndMakeVisible(*orderLabel);
        filterContent.addAndMakeVisible(*section->header);
        filterContent.addAndMakeVisible(*section->typeControl);
        filterContent.addAndMakeVisible(*section->placeControl);
        filterContent.addAndMakeVisible(*section->frequencyControl);
        filterContent.addAndMakeVisible(*section->bandwidthControl);
        filterContent.addAndMakeVisible(*section->orderControl);
        filterContent.addAndMakeVisible(*section->gainControl);
        filterContent.addAndMakeVisible(*section->bypassButton);
        filterOrderLabels[static_cast<size_t>(filterIndex)] = std::move(orderLabel);
        filterSections[static_cast<size_t>(filterIndex)] = std::move(section);

        normalizeOrderForType(filterIndex);
    }

    addFilterButton = std::make_unique<BoxTextButton>(uiGrey500);
    addFilterButton->setButtonText("ADD");
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
    }, {}, "DALL?");
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
    const auto filterDeleteIndex = parseFilterDeleteActionIndex(action);

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
        const auto presetNumber = juce::roundToInt(value);
        const auto presetNames = eqlProcessor->getFilterPresetNames();
        if (! juce::isPositiveAndBelow(presetNumber - 1, presetNames.size())
            || std::abs(value - static_cast<float>(presetNumber)) > 1.0e-6f)
            return false;

        const auto presetName = presetNames[presetNumber - 1];
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
