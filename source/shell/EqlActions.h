#pragma once
#include <JuceHeader.h>
#include "../modules/eql/Processor.h"

inline int parseEqlFilterDeleteActionIndex(const juce::String& action)
{
    constexpr auto prefix = "filter-";
    constexpr auto suffix = "_delete.hidden";
    if (!action.startsWith(prefix) || !action.endsWith(suffix)) return -1;
    const auto number = action.substring(juce::String(prefix).length(), action.length() - juce::String(suffix).length());
    if (number.isEmpty() || number.length() > juce::String(EqlModuleProcessor::maxFilterCount).length()
        || ! number.containsOnly("0123456789"))
        return -1;
    const auto index = number.getIntValue() - 1;
    return juce::isPositiveAndBelow(index, EqlModuleProcessor::maxFilterCount)
        && number == juce::String(index + 1) ? index : -1;
}
