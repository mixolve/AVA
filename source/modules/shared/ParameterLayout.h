#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <utility>

namespace ava::modules::parameters
{
inline std::unique_ptr<juce::RangedAudioParameter> makeBoolParameter(
    const juce::String& id, const juce::String& name, const bool defaultValue)
{
    return std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { id, 1 }, name, defaultValue,
        juce::AudioParameterBoolAttributes().withAutomatable(false));
}

inline std::unique_ptr<juce::RangedAudioParameter> makeChoiceParameter(
    const juce::String& id, const juce::String& name,
    const juce::StringArray& choices, const int defaultIndex)
{
    return std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { id, 1 }, name, choices, defaultIndex,
        juce::AudioParameterChoiceAttributes().withAutomatable(false));
}

inline std::unique_ptr<juce::RangedAudioParameter> makeFloatParameter(
    const juce::String& id, const juce::String& name,
    const juce::NormalisableRange<float>& range, const float defaultValue,
    std::function<juce::String(float, int)> formatter, const juce::String& label = {})
{
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, range, defaultValue,
        juce::AudioParameterFloatAttributes()
            .withAutomatable(false)
            .withLabel(label)
            .withStringFromValueFunction(std::move(formatter))
            .withValueFromStringFunction([] (const juce::String& text)
            {
                return text.trim().getFloatValue();
            }));
}
}
