#pragma once

#include <JuceHeader.h>

namespace shell_state_validation
{
// Keep valid state fields and leave damaged fields at their live values.
// Does not reject the surrounding plugin snapshot.
void retainValidProperties(juce::ValueTree& incoming,
                           const juce::ValueTree& current,
                           bool includeABCompareState);
}
