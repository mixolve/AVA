#pragma once

#include <JuceHeader.h>

#include <vector>

struct OscParameterInfo
{
    juce::String internalName;
    juce::String acceptedValues;
    juce::String address;

    bool operator==(const OscParameterInfo&) const = default;
};

struct OscSettings
{
    static constexpr int defaultInputPort = 9000;
    static constexpr int defaultOutputPort = 9001;

    bool enabled = false;
    int inputPort = defaultInputPort;
    juce::String outputHost { "127.0.0.1" };
    int outputPort = defaultOutputPort;

    bool operator==(const OscSettings&) const = default;
};

inline bool isValidOscInstanceName(const juce::String& value)
{
    return value.isNotEmpty() && value.length() <= 48
        && value.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
}

inline juce::String makeOscAddressPrefix(const juce::String& instanceName)
{
    return "/ava/" + instanceName.toLowerCase() + "/";
}
