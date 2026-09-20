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

    refreshFftAnalyserResponse();
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

    includeVisibleComponent(section->typeControl.get());
    includeVisibleComponent(section->placeControl.get());
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
    int place = 0;
    double frequency = 0.0;
};

int sortPlaceFor(const int place) noexcept
{
    switch (juce::jlimit(0, 7, place))
    {
        case 5: return 0; // PHS -> LR
        case 6: return 1; // PHL -> LL
        case 7: return 2; // PHR -> RR
        default: return juce::jlimit(0, 4, place);
    }
}

}

void AvaAudioProcessorEditor::sortFilterSectionsByPlace()
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

        const auto sortFrequency = section->getFilterType() == EqlModuleProcessor::FilterType::volume ? 0.0
                                                                                                      : section->getFrequency();
        sortKeys.push_back({ filterIndex, sortPlaceFor(section->getPlace()), sortFrequency });
    }

    std::stable_sort(sortKeys.begin(),
                     sortKeys.end(),
                     [] (const FilterSortKey& left, const FilterSortKey& right)
                     {
                         return left.place < right.place;
                     });

    std::vector<int> orderedIndices;
    orderedIndices.reserve(sortKeys.size());

    for (const auto& key : sortKeys)
        orderedIndices.push_back(key.index);

    applyFilterSortOrder(orderedIndices);
}

void AvaAudioProcessorEditor::sortFilterSectionsByFrequency()
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

        const auto sortFrequency = section->getFilterType() == EqlModuleProcessor::FilterType::volume ? 0.0
                                                                                                      : section->getFrequency();
        sortKeys.push_back({ filterIndex, sortPlaceFor(section->getPlace()), sortFrequency });
    }

    std::stable_sort(sortKeys.begin(),
                     sortKeys.end(),
                     [] (const FilterSortKey& left, const FilterSortKey& right)
                     {
                         return left.frequency < right.frequency;
                     });

    std::vector<int> orderedIndices;
    orderedIndices.reserve(sortKeys.size());

    for (const auto& key : sortKeys)
        orderedIndices.push_back(key.index);

    applyFilterSortOrder(orderedIndices);
}

void AvaAudioProcessorEditor::sortFilterSectionsByDuo()
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

        const auto sortFrequency = section->getFilterType() == EqlModuleProcessor::FilterType::volume ? 0.0
                                                                                                      : section->getFrequency();
        sortKeys.push_back({ filterIndex, sortPlaceFor(section->getPlace()), sortFrequency });
    }

    std::stable_sort(sortKeys.begin(),
                     sortKeys.end(),
                     [] (const FilterSortKey& left, const FilterSortKey& right)
                     {
                         if (left.place != right.place)
                             return left.place < right.place;

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

    if (activeCount <= 1 || static_cast<int>(orderedIndices.size()) != activeCount)
        return;

    for (int destinationIndex = 0; destinationIndex < activeCount; ++destinationIndex)
        filterDisplayOrder[static_cast<size_t>(destinationIndex)] = orderedIndices[static_cast<size_t>(destinationIndex)];

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

    std::vector<int> orderedIndices;
    orderedIndices.reserve(static_cast<size_t>(activeCount));

    for (int orderPosition = 0; orderPosition < activeCount; ++orderPosition)
        orderedIndices.push_back(filterDisplayOrder[static_cast<size_t>(orderPosition)]);

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
