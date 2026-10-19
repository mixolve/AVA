#include "FilterSupport.h"

juce::String formatDecibelValue(const float value)
{
    return juce::String::formatted("%.2f dB", static_cast<double>(value));
}

juce::String formatFrequencyValue(const float value)
{
    return juce::String::formatted("%.2f Hz", static_cast<double>(value));
}

juce::String formatBandwidthValue(const float value)
{
    return juce::String::formatted("%.2f oct", static_cast<double>(value));
}

juce::String makeFilterParameterId(const char* suffix, const int filterIndex)
{
    return "filter-" + juce::String(filterIndex + 1) + "_" + suffix;
}

int clampActiveFilterCount(const int filterCount)
{
    return juce::jlimit(0, EqlModuleProcessor::maxFilterCount, filterCount);
}

juce::String filterTypeDisplayPrefix(const EqlModuleProcessor::FilterType type)
{
    switch (type)
    {
        case EqlModuleProcessor::FilterType::lowShelf: return "LSH";
        case EqlModuleProcessor::FilterType::lowCut: return "LCT";
        case EqlModuleProcessor::FilterType::highCut: return "HCT";
        case EqlModuleProcessor::FilterType::highShelf: return "HSH";
        case EqlModuleProcessor::FilterType::tilt: return "FTL";
        case EqlModuleProcessor::FilterType::volume: return "VOL";
        case EqlModuleProcessor::FilterType::bell:
        default: return "BEL";
    }
}
