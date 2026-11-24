#include "Editor.h"
#include "FilterSection.h"
#include "FilterOrderState.h"
#include "../modules/eql/FilterSupport.h"
#include "../modules/eql/Processor.h"
#include "../modules/eql/RangeBank.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <array>

namespace
{
// Clipboard payload contains values, not filter indices or pointers. The system
// clipboard allows transfers between different AVA instances and plugin windows.
using FilterParameterId = juce::String (*)(int);
constexpr std::array<FilterParameterId, 7> filterParameterIds {
    &EqlModuleProcessor::getFilterTypeParamId,
    &EqlModuleProcessor::getFilterPhaseParamId,
    &EqlModuleProcessor::getFilterFrequencyParamId,
    &EqlModuleProcessor::getFilterBandwidthParamId,
    &EqlModuleProcessor::getFilterOrderParamId,
    &EqlModuleProcessor::getFilterGainParamId,
    &EqlModuleProcessor::getFilterBypassParamId
};
constexpr auto eqlClipboardHeader = "AVA-EQL-FILTER-V1\n";
constexpr auto eqlAllFiltersClipboardHeader = "AVA-EQL-FILTERS-V1\n";

bool copyEqlFilterToClipboard(juce::AudioProcessorValueTreeState& source, int filter)
{
    juce::Array<juce::var> values;
    for (auto id : filterParameterIds)
    {
        auto* parameter = source.getParameter(id(filter));
        if (parameter == nullptr) return false;
        values.add(double(parameter->convertFrom0to1(parameter->getValue())));
    }
    juce::SystemClipboard::copyTextToClipboard(juce::String(eqlClipboardHeader)
                                                 + juce::JSON::toString(juce::var(values), true));
    return true;
}

bool pasteEqlFilterFromClipboard(juce::AudioProcessorValueTreeState& destination, int filter)
{
    const auto text = juce::SystemClipboard::getTextFromClipboard();
    if (!text.startsWith(eqlClipboardHeader)) return false;
    const auto parsed = juce::JSON::parse(text.substring(int(juce::String(eqlClipboardHeader).length())));
    const auto* values = parsed.getArray();
    if (values == nullptr || values->size() != int(filterParameterIds.size())) return false;

    std::array<juce::RangedAudioParameter*, filterParameterIds.size()> targets {};
    std::array<float, filterParameterIds.size()> normalised {};
    for (size_t index = 0; index < filterParameterIds.size(); ++index)
    {
        const auto& value = values->getReference(int(index));
        if (! (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())) return false;
        const auto realValue = double(value);
        if (!std::isfinite(realValue)) return false;
        targets[index] = destination.getParameter(filterParameterIds[index](filter));
        if (targets[index] == nullptr) return false;
        const auto scaled = targets[index]->convertTo0to1(float(realValue));
        if (!std::isfinite(scaled)) return false;
        normalised[index] = juce::jlimit(0.0f, 1.0f, scaled);
    }
    for (size_t index = 0; index < targets.size(); ++index)
    {
        targets[index]->beginChangeGesture();
        targets[index]->setValueNotifyingHost(normalised[index]);
        targets[index]->endChangeGesture();
    }
    return true;
}

bool isClipboardNumber(const juce::var& value)
{
    return value.isInt() || value.isInt64() || value.isDouble();
}
}

void AvaAudioProcessorEditor::copyAllEqlFilters()
{
    auto* processor = getActiveEqlProcessor();
    if (processor == nullptr) return;
    const auto count = processor->getActiveFilterCount();
    if (count <= 0 || count > EqlModuleProcessor::maxFilterCount) return;
    const auto order = shell_filter_order_state::encode(filterDisplayOrder, count);
    if (order.isEmpty()) return;

    auto& state = processor->getValueTreeState();
    juce::Array<juce::var> filters;
    for (int filter = 0; filter < count; ++filter)
    {
        juce::Array<juce::var> values;
        for (auto id : filterParameterIds)
        {
            auto* parameter = state.getParameter(id(filter));
            if (parameter == nullptr) return;
            values.add(double(parameter->convertFrom0to1(parameter->getValue())));
        }
        filters.add(juce::var(values));
    }
    juce::Array<juce::var> payload;
    payload.add(count);
    payload.add(juce::var(filters));
    payload.add(order);
    juce::SystemClipboard::copyTextToClipboard(juce::String(eqlAllFiltersClipboardHeader)
        + juce::JSON::toString(juce::var(payload), true));
    clearKeyboardFocus(*this);
}

void AvaAudioProcessorEditor::pasteAllEqlFilters()
{
    auto* processor = getActiveEqlProcessor();
    if (processor == nullptr) return;
    const auto contents = juce::SystemClipboard::getTextFromClipboard();
    if (!contents.startsWith(eqlAllFiltersClipboardHeader)) return;
    const auto parsed = juce::JSON::parse(contents.substring(juce::String(eqlAllFiltersClipboardHeader).length()));
    const auto* root = parsed.getArray();
    if (root == nullptr || root->size() != 3 || !root->getReference(0).isInt()) return;
    const auto count = int(root->getReference(0));
    if (count < 1 || count > EqlModuleProcessor::maxFilterCount) return;
    const auto* filters = root->getReference(1).getArray();
    if (filters == nullptr || filters->size() != count || !root->getReference(2).isString()) return;
    const auto order = shell_filter_order_state::decode(root->getReference(2).toString(),
        count, EqlModuleProcessor::maxFilterCount);
    if (!order) return;

    // Validate every destination parameter/value *before* replacing the filter
    // collection. A wrong clipboard format must never remove existing filters.
    auto& state = processor->getValueTreeState();
    std::vector<std::array<float, filterParameterIds.size()>> normalised(static_cast<size_t>(count));
    for (int filter = 0; filter < count; ++filter)
    {
        const auto* values = filters->getReference(filter).getArray();
        if (values == nullptr || values->size() != int(filterParameterIds.size())) return;
        for (size_t i = 0; i < filterParameterIds.size(); ++i)
        {
            const auto& value = values->getReference(int(i));
            if (!isClipboardNumber(value)) return;
            const auto real = double(value);
            if (!std::isfinite(real)) return;
            auto* parameter = state.getParameter(filterParameterIds[i](filter));
            if (parameter == nullptr) return;
            const auto scaled = parameter->convertTo0to1(float(real));
            if (!std::isfinite(scaled)) return;
            normalised[size_t(filter)][i] = juce::jlimit(0.0f, 1.0f, scaled);
        }
    }

    const juce::ScopedValueSetter<bool> guard(suppressFilterSectionValueChangeHandlers, true);
    while (processor->getActiveFilterCount() > count)
        if (!processor->removeFilter(processor->getActiveFilterCount() - 1)) return;
    while (processor->getActiveFilterCount() < count)
        if (!processor->addFilter()) return;
    for (int filter = 0; filter < count; ++filter)
        for (size_t i = 0; i < filterParameterIds.size(); ++i)
            if (auto* parameter = state.getParameter(filterParameterIds[i](filter)))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(normalised[size_t(filter)][i]);
                parameter->endChangeGesture();
            }

    filterDisplayOrder = *order; // Includes the unused slots after the active range.
    for (auto& section : filterSections)
        if (section != nullptr) section->refreshTypeDependentControls(false);
    enforceSingleExpandedFilterSection((*order)[0]);
    storeEditorStateToValueTree();
    updateSectionStates();
    resized();
    scheduleHistorySnapshot();
    audioProcessor.notifyHostOfStateChange();
    clearKeyboardFocus(*this);
}

void AvaAudioProcessorEditor::setupEqlControls(juce::AudioProcessorValueTreeState& initialEqlState)
{
    boundEqlState = &initialEqlState;

    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
    {
        auto section = std::make_unique<FilterSection>(initialEqlState, filterIndex);
        section->frequencyControl->onResetRequested = [this, filterIndex]
        {
            auto* filterSection = filterSections[static_cast<size_t>(filterIndex)].get();

            if (filterSection == nullptr)
                return;

            filterSection->frequencyControl->setValue(defaultFilterFrequencyHz, true);
        };
        section->bandwidthControl->onResetRequested = [this, filterIndex]
        {
            auto* filterSection = filterSections[static_cast<size_t>(filterIndex)].get();

            if (filterSection == nullptr)
                return;

            filterSection->bandwidthControl->setValue(defaultFilterBandwidthOctaves, true);
        };
        section->orderControl->onResetRequested = [this, filterIndex]
        {
            auto* filterSection = filterSections[static_cast<size_t>(filterIndex)].get();

            if (filterSection == nullptr)
                return;

            filterSection->orderControl->setSelectedChoiceIndex(
                EqlModuleProcessor::getOrderChoiceForSlopeDbPerOct(defaultFilterSlopeDbPerOct),
                true);
        };
        section->gainControl->onResetRequested = [this, filterIndex]
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
        section->phaseIndicator->onValueChanged = updateStateOnValueChange;
        section->bandwidthControl->onValueChanged = updateStateOnValueChange;
        section->orderControl->onValueChanged = [this, filterIndex]
        {
            if (suppressFilterSectionValueChangeHandlers)
                return;

            normalizeOrderForType(filterIndex);

            updateSectionStates();
        };
        section->gainControl->onValueChanged = updateStateOnValueChange;
        section->header->onTrigger = [this, filterIndex]
        {
            auto* filter = filterSections[size_t(filterIndex)].get();
            if (filter == nullptr) return;
            enforceSingleExpandedFilterSection(filterIndex);
            storeEditorStateToValueTree();
            selectFilterSection(filterIndex);
        };
        section->header->setDragAction([this, filterIndex](const juce::MouseEvent& event, bool released)
        {
            handleFilterDrag(filterIndex, event.getScreenPosition().toFloat(), released);
        }, [this] { cancelFilterDrag(); });
        for (auto* control : std::array<juce::Component*, 5> { section->typeControl.get(),
                section->orderControl.get(), section->frequencyControl.get(),
                section->bandwidthControl.get(), section->gainControl.get() })
            control->getProperties().set("foregroundOccluder", true);
        eqlContent.addChildComponent(section->body);
        filterContent.addAndMakeVisible(*section->header);
        section->body.addAndMakeVisible(*section->typeControl);
        section->body.addAndMakeVisible(*section->frequencyControl);
        section->body.addAndMakeVisible(*section->bandwidthControl);
        section->body.addAndMakeVisible(*section->orderControl);
        section->body.addAndMakeVisible(*section->gainControl);
        filterSections[static_cast<size_t>(filterIndex)] = std::move(section);

        normalizeOrderForType(filterIndex);
    }

    addFilterButton = std::make_unique<PushControl>("ONE\nADD");
    addFilterButton->getProperties().set("disableDockSelection", true);
    addFilterButton->getProperties().set("foregroundOccluder", true);
    addFilterButton->setBottomIndicatorsVisible(false, false);
    addFilterButton->onTrigger = [this]
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
    filterContent.addAndMakeVisible(*addFilterButton);
    copyFilterButton = std::make_unique<PushControl>("ONE\nCOPY");
    copyFilterButton->getProperties().set("disableDockSelection", true);
    copyFilterButton->setBottomIndicatorsVisible(false, false);
    copyFilterButton->onTrigger = [this]
    {
        auto* processor = getActiveEqlProcessor();
        if (processor == nullptr) return;
        for (int index = 0; index < getActiveFilterCount(); ++index)
            if (filterSections[size_t(index)] != nullptr && filterSections[size_t(index)]->expanded)
            {
                copyEqlFilterToClipboard(processor->getValueTreeState(), index);
                return;
            }
    };
    eqlContent.addChildComponent(*copyFilterButton);

    pasteFilterButton = std::make_unique<PushControl>("ONE\nPASTE");
    pasteFilterButton->getProperties().set("disableDockSelection", true);
    pasteFilterButton->setBottomIndicatorsVisible(false, false);
    pasteFilterButton->onTrigger = [this]
    {
        auto* processor = getActiveEqlProcessor();
        if (processor == nullptr) return;
        for (int index = 0; index < getActiveFilterCount(); ++index)
            if (filterSections[size_t(index)] != nullptr && filterSections[size_t(index)]->expanded)
            {
                const juce::ScopedValueSetter<bool> suppressHandlers(suppressFilterSectionValueChangeHandlers, true);
                if (!pasteEqlFilterFromClipboard(processor->getValueTreeState(), index)) return;
                filterSections[size_t(index)]->refreshTypeDependentControls(false);
                updateSectionStates();
                resized();
                scheduleHistorySnapshot();
                audioProcessor.notifyHostOfStateChange();
                return;
            }
    };
    eqlContent.addChildComponent(*pasteFilterButton);

    deleteFilterButton = std::make_unique<PushControl>("ONE\nDELETE");
    deleteFilterButton->getProperties().set("disableDockSelection", true);
    deleteFilterButton->setBottomIndicatorsVisible(false, false);
    deleteFilterButton->onTrigger = [this]
    {
        auto* processor = getActiveEqlProcessor();
        if (processor == nullptr) return;
        auto selectedIndex = -1;
        for (int index = 0; index < getActiveFilterCount(); ++index)
            if (filterSections[size_t(index)] != nullptr && filterSections[size_t(index)]->expanded)
                selectedIndex = index;
        if (selectedIndex < 0) return;
        dismissTextPrompt();
        const auto orderPosition = getFilterOrderPositionForIndex(selectedIndex);
        const juce::ScopedValueSetter<bool> suppressHandlers(suppressFilterSectionValueChangeHandlers, true);
        const auto mapping = processor->removeFilter(selectedIndex);
        if (!mapping) return;

        filterDisplayOrder.erase(std::remove_if(filterDisplayOrder.begin(), filterDisplayOrder.end(),
            [&] (int index) { return (*mapping)[size_t(index)] < 0; }), filterDisplayOrder.end());
        for (auto& index : filterDisplayOrder)
            index = (*mapping)[size_t(index)];
        filterDisplayOrder.push_back(EqlModuleProcessor::maxFilterCount - 1);
        for (auto& section : filterSections)
            if (section != nullptr) section->refreshTypeDependentControls();
        const auto nextIndex = getFilterIndexForOrderPosition(juce::jmin(orderPosition, getActiveFilterCount() - 1));
        enforceSingleExpandedFilterSection(nextIndex);
        storeEditorStateToValueTree();
        updateSectionStates();
        resized();
        if (nextIndex >= 0) selectFilterSection(nextIndex);
        scheduleHistorySnapshot();
        clearKeyboardFocus(*this);
    };
    eqlContent.addChildComponent(*deleteFilterButton);
}
