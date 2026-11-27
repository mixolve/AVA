#include "Editor.h"
#include "FilterSection.h"
#include "../modules/eql/Processor.h"

#include <algorithm>
#include <utility>

void AvaAudioProcessorEditor::selectFilterSection(const int filterIndex)
{
    if (! juce::isPositiveAndBelow(filterIndex, getActiveFilterCount()))
        return;

    updateSectionStates();
    resized();

    if (filterSections[static_cast<size_t>(filterIndex)] != nullptr)
    {
        const auto* section = filterSections[size_t(filterIndex)].get();
        const auto sectionBounds = moduleContent.getLocalArea(section->typeControl.get(),
            section->typeControl->getLocalBounds());
        const auto currentY = filterViewport.getViewPositionY();
        const auto viewportHeight = filterViewport.getViewArea().getHeight();
        auto targetY = currentY;

        if (sectionBounds.getY() < currentY)
            targetY = sectionBounds.getY();
        else if (sectionBounds.getBottom() > currentY + viewportHeight)
            targetY = sectionBounds.getBottom() - viewportHeight;

        const auto maxOffset = juce::jmax(0, getActiveFilterContentHeight() - viewportHeight);
        filterViewport.setViewPosition(0, juce::jlimit(0, maxOffset, targetY));
    }

}

void AvaAudioProcessorEditor::enforceSingleExpandedFilterSection(const int preferredFilterIndex)
{
    const auto activeCount = getActiveFilterCount();
    auto targetFilterIndex = preferredFilterIndex;
    if (!juce::isPositiveAndBelow(targetFilterIndex, activeCount))
    {
        targetFilterIndex = activeCount > 0 ? getFilterIndexForOrderPosition(0) : -1;
        for (int index = 0; index < activeCount; ++index)
            if (filterSections[size_t(index)] != nullptr && filterSections[size_t(index)]->expanded)
            {
                targetFilterIndex = index;
                break;
            }
    }

    for (int filterIndex = 0; filterIndex < static_cast<int>(filterSections.size()); ++filterIndex)
    {
        auto* section = filterSections[static_cast<size_t>(filterIndex)].get();

        if (section == nullptr)
            continue;

        section->expanded = filterIndex == targetFilterIndex;
    }
}

void AvaAudioProcessorEditor::normalizeOrderForType(const int filterIndex)
{
    if (! juce::isPositiveAndBelow(filterIndex, static_cast<int>(filterSections.size())))
        return;

    auto* filterSection = filterSections[static_cast<size_t>(filterIndex)].get();

    if (filterSection == nullptr || filterSection->getFilterType() != EqlModuleProcessor::FilterType::bell)
        return;

    const auto selectedChoiceIndex = filterSection->orderControl->getSelectedChoiceIndex();

    if (selectedChoiceIndex != 0)
        return;

    const juce::ScopedValueSetter<bool> suppressHandlers(suppressFilterSectionValueChangeHandlers, true);
    filterSection->orderControl->setSelectedChoiceIndex(
        EqlModuleProcessor::getOrderChoiceForSlopeDbPerOct(EqlModuleProcessor::fixedSlopeDbPerOct),
        true);
}

namespace
{
struct FilterSortKey
{
    int index = 0;
    double frequency = 0.0;
    bool phase = false;
};

}

void AvaAudioProcessorEditor::sortFilterSections()
{
    const auto activeCount = getActiveFilterCount();

    if (activeCount <= 1)
        return;

    std::vector<FilterSortKey> sortKeys;
    sortKeys.reserve(static_cast<size_t>(activeCount));

    for (int filterIndex = 0; filterIndex < activeCount; ++filterIndex)
    {
        auto* section = filterSections[static_cast<size_t>(filterIndex)].get();

        if (section == nullptr)
            return;

        const auto sortFrequency = section->getFilterType() != EqlModuleProcessor::FilterType::volume
            ? section->getFrequency() : 0.0;
        const auto type = section->getFilterType();
        const auto phase = section->isPhaseEnabled()
            && type != EqlModuleProcessor::FilterType::lowCut
            && type != EqlModuleProcessor::FilterType::highCut
            && type != EqlModuleProcessor::FilterType::volume;
        sortKeys.push_back({ filterIndex, sortFrequency, phase });
    }

    std::stable_sort(sortKeys.begin(),
                     sortKeys.end(),
                     [] (const FilterSortKey& left, const FilterSortKey& right)
                     {
                         if (left.phase != right.phase)
                             return !left.phase;
                         return left.frequency < right.frequency;
                     });

    std::vector<int> orderedIndices;
    orderedIndices.reserve(sortKeys.size());

    for (const auto& key : sortKeys)
        orderedIndices.push_back(key.index);

    applyFilterSortOrder(orderedIndices);
}

void AvaAudioProcessorEditor::applyFilterSortOrder(const std::vector<int>& orderedIndices)
{
    const auto activeCount = getActiveFilterCount();

    if (activeCount <= 1 || static_cast<int>(orderedIndices.size()) != activeCount
        || filterDisplayOrder.size() < orderedIndices.size())
        return;

    if (std::equal(orderedIndices.begin(), orderedIndices.end(), filterDisplayOrder.begin()))
        return;

    std::copy(orderedIndices.begin(), orderedIndices.end(), filterDisplayOrder.begin());

    storeEditorStateToValueTree();
    updateSectionStates();
    resized();
    scheduleHistorySnapshot();
}

int AvaAudioProcessorEditor::getFilterIndexForOrderPosition(const int orderIndex) const noexcept
{
    if (! juce::isPositiveAndBelow(orderIndex, getActiveFilterCount()))
        return -1;

    if (! juce::isPositiveAndBelow(orderIndex, static_cast<int>(filterDisplayOrder.size())))
        return -1;

    return filterDisplayOrder[static_cast<size_t>(orderIndex)];
}

int AvaAudioProcessorEditor::getFilterOrderPositionForIndex(const int filterIndex) const noexcept
{
    const auto activeCount = getActiveFilterCount();

    for (int orderIndex = 0; orderIndex < activeCount; ++orderIndex)
    {
        if (filterDisplayOrder[static_cast<size_t>(orderIndex)] == filterIndex)
            return orderIndex;
    }

    return -1;
}

void AvaAudioProcessorEditor::cancelFilterDrag()
{
    if (std::exchange(filterDragSourceIndex, -1) < 0) return;
    juce::Component::beginDragAutoRepeat(0);
    for (auto& section : filterSections)
        if (section != nullptr) section->header->setDragTargetHighlighted(false);
}

void AvaAudioProcessorEditor::handleFilterDrag(const int filterIndex, juce::Point<float> screenPosition, const bool released)
{
    const auto activeCount = getActiveFilterCount();
    if (hasUnitPage()
        || !juce::isPositiveAndBelow(filterIndex, activeCount)) { cancelFilterDrag(); return; }
    if (released && filterDragSourceIndex != filterIndex) return;
    if (!released && filterDragSourceIndex < 0) filterDragSourceIndex = filterIndex;
    auto viewportPoint = filterViewport.getLocalPoint(nullptr, screenPosition);
    if (!released) filterViewport.autoScroll(juce::roundToInt(viewportPoint.x), juce::roundToInt(viewportPoint.y),
        2 * getUiGap(*this), getUiGap(*this));
    const auto inViewport = filterViewport.getLocalBounds().toFloat().contains(viewportPoint);
    // EQL filter headers are now children of their own horizontal viewport.
    // Hit-test in screen coordinates (not the former filterContent parent),
    // and auto-scroll that row while dragging a header near either edge.
    auto* headerRow = eqlScrollRows[2].get();
    bool inRow = false;
    if (headerRow != nullptr)
    {
        auto rowPoint = headerRow->viewport.getLocalPoint(nullptr, screenPosition);
        inRow = headerRow->viewport.getLocalBounds().toFloat().contains(rowPoint);
        if (!released && inRow)
            headerRow->viewport.autoScroll(juce::roundToInt(rowPoint.x), juce::roundToInt(rowPoint.y),
                2 * getUiGap(*this), getUiGap(*this));
    }
    int destination = -1;
    for (int index = 0; index < activeCount; ++index) {
        auto& header = *filterSections[size_t(index)]->header;
        const auto over = inViewport && inRow && header.getScreenBounds().toFloat().contains(screenPosition);
        header.setDragTargetHighlighted(over && index != filterDragSourceIndex);
        if (over) destination = index;
    }
    if (!released) return;
    const auto source = filterDragSourceIndex;
    cancelFilterDrag();
    if (destination < 0 || destination == source) return;
    const auto from = getFilterOrderPositionForIndex(source), to = getFilterOrderPositionForIndex(destination);
    if (from < 0 || to < 0) return;
    std::vector<int> order(filterDisplayOrder.begin(), filterDisplayOrder.begin() + activeCount);
    order.erase(order.begin() + from);
    order.insert(order.begin() + to, source);
    applyFilterSortOrder(order);
}
