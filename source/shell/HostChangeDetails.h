#pragma once
#include <JuceHeader.h>

inline juce::AudioProcessorListener::ChangeDetails makeHostStateChangeDetails()
{
    auto details = juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true);
   #if JucePlugin_Build_AU
    // The AU wrapper needs the program flag to request and persist ClassInfo.
    details = details.withProgramChanged(true);
   #endif
    return details;
}
