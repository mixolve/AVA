#include "FilterSupport.h"
#include "FilterParameters.h"

#include <array>
#include <memory>
#include <vector>

namespace
{
struct ParameterOrderEntry
{
    const char* key;
    const char* label;
};

inline constexpr auto eqlFilterOrder = std::to_array<ParameterOrderEntry>({
    { "type", "TYPE" },
    { "place", "PLACE" },
    { "order", "ORDER" },
    { "freq", "FREQ" },
    { "bw", "BW" },
    { "gain", "GAIN" },
    { "bypass", "B" },
});
}

void EqlModuleProcessor::appendEqlParameters(std::vector<std::unique_ptr<juce::RangedAudioParameter>>& parameterLayout)
{
    for (int filterIndex = 0; filterIndex < maxFilterCount; ++filterIndex)
    {
        for (const auto& entry : eqlFilterOrder)
        {
            const auto key = juce::String(entry.key);
            const auto name = "EQL / FILTER " + juce::String(filterIndex + 1) + " / " + juce::String(entry.label);

            if (key == "type")
            {
                parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
                    juce::ParameterID { getFilterTypeParamId(filterIndex), 1 },
                    name,
                    filterTypeChoices,
                    EqlModuleProcessor::choiceIndexFromFilterType(EqlModuleProcessor::FilterType::bell),
                    juce::AudioParameterChoiceAttributes()));
                continue;
            }

            if (key == "place")
            {
                parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
                    juce::ParameterID { getFilterPlaceParamId(filterIndex), 1 },
                    name,
                    filterPlaceChoices,
                    0,
                    juce::AudioParameterChoiceAttributes()));
                continue;
            }

            if (key == "order")
            {
                parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
                    juce::ParameterID { getFilterOrderParamId(filterIndex), 1 },
                    name,
                    EqlModuleProcessor::getFilterOrderChoices(),
                    EqlModuleProcessor::getOrderChoiceForSlopeDbPerOct(EqlModuleProcessor::fixedSlopeDbPerOct),
                    juce::AudioParameterChoiceAttributes()));
                continue;
            }

            if (key == "freq")
            {
                auto filterFrequencyRange = juce::NormalisableRange<float> { minimumVisibleFilterFrequency, maximumVisibleFilterFrequency, 0.01f };
                filterFrequencyRange.setSkewForCentre(defaultFilterFrequencyHz);

                parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID { getFilterFrequencyParamId(filterIndex), 1 },
                    name,
                    filterFrequencyRange,
                    defaultFilterFrequencyHz,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                        [] (float value, int)
                        {
                            return formatFrequencyValue(value);
                        })));
                continue;
            }

            if (key == "bw")
            {
                parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID { getFilterBandwidthParamId(filterIndex), 1 },
                    name,
                    juce::NormalisableRange<float> { minimumBellBandwidth, maximumBellBandwidth, 0.001f },
                    1.0f,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                        [] (float value, int)
                        {
                            return formatBandwidthValue(value);
                        })));
                continue;
            }

            if (key == "gain")
            {
                parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID { getFilterGainParamId(filterIndex), 1 },
                    name,
                    juce::NormalisableRange<float> { -48.0f, 48.0f, 0.01f },
                    0.0f,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                        [] (float value, int)
                        {
                            return formatDecibelValue(value);
                        })));
                continue;
            }

            if (key == "bypass")
            {
                parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
                    juce::ParameterID { getFilterBypassParamId(filterIndex), 1 },
                    name,
                    false,
                    juce::AudioParameterBoolAttributes()));
            }
        }
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout EqlModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameterLayout;
    appendEqlParameters(parameterLayout);
    return { parameterLayout.begin(), parameterLayout.end() };
}

void EqlModuleProcessor::parameterChanged(const juce::String&, float)
{
    markEqlFiltersDirty();
}

void EqlModuleProcessor::setParameterListenersEnabled(const bool enabled)
{
    for (int filterIndex = 0; filterIndex < maxFilterCount; ++filterIndex)
    {
        forEachFilterParameterId(filterIndex,
                                 [this, enabled] (const juce::String& parameterId)
                                 {
                                     if (enabled)
                                         parameters.addParameterListener(parameterId, this);
                                     else
                                         parameters.removeParameterListener(parameterId, this);
                                 });
    }
}
