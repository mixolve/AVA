#pragma once

#include <JuceHeader.h>

#include <vector>

struct OscParameterInfo
{
    juce::String name;
    juce::String acceptedValues;
    juce::String address;
    juce::String sourceId;

    bool operator==(const OscParameterInfo&) const = default;
};

inline juce::String getOscAddressName(const juce::String& sourceId)
{
    auto name = sourceId;
    if (name == "bp")
        name = "bypass";
    else if (name == "b.ab")
        name = "b";
    else if (name.endsWith("_bp"))
        name = name.dropLastCharacters(3) + "_bypass";
    else if (name.endsWith(".icon"))
        name = name.dropLastCharacters(5);
    else if (name.endsWith(".hidden"))
        name = name.dropLastCharacters(7);
    return name.replaceCharacter('.', '-');
}

inline juce::String getOscDisplayName(const juce::String& sourceId)
{
    auto name = getOscAddressName(sourceId);
    if (sourceId.endsWith(".hidden"))
        return name + " (hidden, icon)";
    if (sourceId.endsWith(".icon") || sourceId == "bp" || sourceId.endsWith("_bp"))
        return name + " (icon)";
    return name;
}

inline juce::String getOscSourceId(const juce::String& addressName,
                                   const std::vector<OscParameterInfo>& parameters)
{
    for (const auto& parameter : parameters)
        if (getOscAddressName(parameter.sourceId) == addressName)
            return parameter.sourceId;

    if (addressName == "bypass")
        return "bp";
    if (addressName == "b")
        return "b.ab";
    if (addressName.endsWith("_bypass"))
        return addressName.dropLastCharacters(7) + "_bp";
    if (addressName == "ab-switch" || addressName == "undo" || addressName == "redo"
        || addressName.endsWith(".mute")
        || addressName.endsWith("mute.transient") || addressName.endsWith("mute.sustain"))
        return addressName + ".icon";
    if (addressName == "close-module" || addressName.endsWith("_delete-all")
        || (addressName.contains("_filter-") && addressName.endsWith("_delete")))
        return addressName + ".hidden";
    return addressName;
}

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
