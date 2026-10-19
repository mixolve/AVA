#include "Editor.h"
#include "FilterSection.h"
#include "../modules/eql/Processor.h"

#include <algorithm>

void AvaAudioProcessorEditor::selectFilterSection(const int filterIndex)
{
    if (! eqlModuleLoaded)
        return;

    if (! juce::isPositiveAndBelow(filterIndex, getActiveFilterCount()))
        return;

    updateSectionStates();
    resized();

    if (filterSections[static_cast<size_t>(filterIndex)] != nullptr)
    {
        const auto sectionBounds = getFilterSectionBounds(filterIndex);
        const auto currentY = filterViewport.getViewPositionY();
        const auto viewportHeight = filterViewport.getHeight();
        auto targetY = currentY;

        if (sectionBounds.getY() < currentY)
            targetY = sectionBounds.getY();
        else if (sectionBounds.getBottom() > currentY + viewportHeight)
            targetY = sectionBounds.getBottom() - viewportHeight;

        const auto maxOffset = juce::jmax(0, getFilterContentHeight() - viewportHeight);
        filterViewport.setViewPosition(0, juce::jlimit(0, maxOffset, targetY));
    }

}

juce::Rectangle<int> AvaAudioProcessorEditor::getFilterSectionBounds(const int filterIndex) const
{
    if (! juce::isPositiveAndBelow(filterIndex, getActiveFilterCount()))
        return {};

    const auto* section = filterSections[static_cast<size_t>(filterIndex)].get();

    if (section == nullptr || section->header == nullptr)
        return {};

    auto bounds = filterContent.getLocalArea(section->header.get(), section->header->getLocalBounds());

    auto includeVisibleComponent = [this, &bounds] (const juce::Component* component)
    {
        if (component == nullptr || ! component->isVisible() || component->getBounds().isEmpty())
            return;

        bounds = bounds.getUnion(filterContent.getLocalArea(component, component->getLocalBounds()));
    };

    includeVisibleComponent(&section->accentFrame);
    includeVisibleComponent(section->typeControl.get());
    includeVisibleComponent(section->orderControl.get());
    includeVisibleComponent(section->frequencyControl.get());
    includeVisibleComponent(section->bandwidthControl.get());
    includeVisibleComponent(section->gainControl.get());
    includeVisibleComponent(section->bypassButton.get());

    return bounds;
}

void AvaAudioProcessorEditor::enforceSingleExpandedFilterSection(const int preferredFilterIndex)
{
    const auto activeCount = getActiveFilterCount();
    const auto targetFilterIndex = juce::isPositiveAndBelow(preferredFilterIndex, activeCount) ? preferredFilterIndex : -1;

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

void AvaAudioProcessorEditor::moveFilterSectionTo(const int filterIndex, const int destinationOrderPosition)
{
    const auto activeCount = getActiveFilterCount();
    const auto sourceOrderPosition = getFilterOrderPositionForIndex(filterIndex);

    if (activeCount <= 1 || ! juce::isPositiveAndBelow(sourceOrderPosition, activeCount))
        return;

    const auto destination = juce::jlimit(0, activeCount - 1, destinationOrderPosition);

    if (sourceOrderPosition == destination)
        return;

    std::vector<int> orderedIndices(filterDisplayOrder.begin(), filterDisplayOrder.begin() + activeCount);

    const auto movedFilterIndex = orderedIndices[static_cast<size_t>(sourceOrderPosition)];
    orderedIndices.erase(orderedIndices.begin() + sourceOrderPosition);
    orderedIndices.insert(orderedIndices.begin() + destination, movedFilterIndex);
    applyFilterSortOrder(orderedIndices);
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
