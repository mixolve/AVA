#include "FilterSupport.h"

#include <array>
#include <cmath>
#include <limits>

namespace
{
constexpr std::array<float, 6> orderSlopeDbPerOctValues
{
    6.0f,
    12.0f,
    24.0f,
    48.0f,
    96.0f,
    96.1f
};
}

juce::StringArray EqlModuleProcessor::getFilterOrderChoices() noexcept
{
    return { "01", "02", "04", "08", "16", "++" };
}

float EqlModuleProcessor::getSlopeDbPerOctForOrderChoice(const int choiceIndex) noexcept
{
    if (! juce::isPositiveAndBelow(choiceIndex, static_cast<int>(orderSlopeDbPerOctValues.size())))
        return fixedSlopeDbPerOct;

    return orderSlopeDbPerOctValues[static_cast<size_t>(choiceIndex)];
}

int EqlModuleProcessor::getOrderChoiceForSlopeDbPerOct(const float slope) noexcept
{
    if (! std::isfinite(slope))
        return juce::jlimit(0, static_cast<int>(orderSlopeDbPerOctValues.size()) - 1, 3);

    if (slope > 96.0f)
        return static_cast<int>(orderSlopeDbPerOctValues.size()) - 1;

    auto bestIndex = 0;
    auto bestDistance = std::numeric_limits<float>::max();

    for (int index = 0; index < static_cast<int>(orderSlopeDbPerOctValues.size()) - 1; ++index)
    {
        const auto distance = std::abs(orderSlopeDbPerOctValues[static_cast<size_t>(index)] - slope);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = index;
        }
    }

    return bestIndex;
}

juce::String EqlModuleProcessor::getFilterTypeParamId(const int filterIndex)
{
    return "filter_" + juce::String(filterIndex + 1) + "_type";
}

juce::String EqlModuleProcessor::getFilterPlaceParamId(const int filterIndex)
{
    return "filter_" + juce::String(filterIndex + 1) + "_place";
}

juce::String EqlModuleProcessor::getFilterFrequencyParamId(const int filterIndex)
{
    return makeFilterParameterId("frequency", filterIndex);
}

juce::String EqlModuleProcessor::getFilterBandwidthParamId(const int filterIndex)
{
    return makeFilterParameterId("bandwidth", filterIndex);
}

juce::String EqlModuleProcessor::getFilterOrderParamId(const int filterIndex)
{
    return makeFilterParameterId("order", filterIndex);
}

juce::String EqlModuleProcessor::getFilterGainParamId(const int filterIndex)
{
    return makeFilterParameterId("gain", filterIndex);
}

juce::String EqlModuleProcessor::getFilterBypassParamId(const int filterIndex)
{
    return makeFilterParameterId("bypass", filterIndex);
}

juce::String EqlModuleProcessor::getFilterHeaderText(const FilterType type, const int filterIndex)
{
    return juce::String::formatted("%02d %s",
                                   filterIndex + 1,
                                   filterTypeDisplayPrefix(type).toRawUTF8());
}

namespace
{
juce::String filterPlaceDisplayPrefix(const int choiceIndex)
{
    switch (juce::jlimit(0, 7, choiceIndex))
    {
        case 0: return "LR";
        case 1: return "LL";
        case 2: return "RR";
        case 3: return "MM";
        case 4: return "SS";
        case 5: return "PHS";
        case 6: return "PHL";
        case 7: return "PHR";
        default: return "LR";
    }
}

}

juce::String EqlModuleProcessor::getFilterHeaderText(const int filterIndex, const int) const noexcept
{
    if (! juce::isPositiveAndBelow(filterIndex, static_cast<int>(filterTypeParams.size())))
        return {};

    const auto filterArrayIndex = static_cast<size_t>(filterIndex);
    const auto filterType = getFilterTypeForSection(filterArrayIndex);
    const auto placeChoice = filterPlaceParams[filterArrayIndex] != nullptr
        ? juce::jlimit(0, 7, static_cast<int>(std::lround(filterPlaceParams[filterArrayIndex]->load(std::memory_order_relaxed))))
        : 0;
    const auto frequency = filterFrequencyParams[filterArrayIndex] != nullptr
        ? filterFrequencyParams[filterArrayIndex]->load(std::memory_order_relaxed)
        : defaultFilterFrequencyHz;

    if (filterType == FilterType::volume)
    {
        return juce::String::formatted("%s-%s",
                                       filterTypeDisplayPrefix(filterType).toRawUTF8(),
                                       filterPlaceDisplayPrefix(placeChoice).toRawUTF8())
            + "-00000";
    }

    return juce::String::formatted("%s-%s-%05d",
                                   filterTypeDisplayPrefix(filterType).toRawUTF8(),
                                   filterPlaceDisplayPrefix(placeChoice).toRawUTF8(),
                                   static_cast<int>(std::lround(frequency)));
}

juce::StringArray getOrderDisplayChoicesForType(const EqlModuleProcessor::FilterType type) noexcept
{
    if (type == EqlModuleProcessor::FilterType::bell)
        return { "OFF", "02", "04", "08", "16", "++" };

    if (type == EqlModuleProcessor::FilterType::volume)
        return { "OFF", "OFF", "OFF", "OFF", "OFF", "OFF" };

    return { "01", "02", "04", "08", "16", "++" };
}

EqlModuleProcessor::FilterType EqlModuleProcessor::filterTypeFromChoiceIndex(const int choiceIndex) noexcept
{
    switch (choiceIndex)
    {
        case 0: return FilterType::lowCut;
        case 1: return FilterType::lowShelf;
        case 2: return FilterType::bell;
        case 3: return FilterType::tilt;
        case 4: return FilterType::highShelf;
        case 5: return FilterType::highCut;
        case 6: return FilterType::volume;
        default: return FilterType::bell;
    }
}

int EqlModuleProcessor::choiceIndexFromFilterType(const FilterType type) noexcept
{
    switch (type)
    {
        case FilterType::lowCut: return 0;
        case FilterType::lowShelf: return 1;
        case FilterType::bell: return 2;
        case FilterType::tilt: return 3;
        case FilterType::highShelf: return 4;
        case FilterType::highCut: return 5;
        case FilterType::volume: return 6;
        default: return 2;
    }
}
