#include "Parameters.h"

#include "../../crossover/ParameterIds.h"
#include "DspCore.h"

#include <array>
#include <algorithm>
#include <memory>

namespace tls::parameters
{
using ava::crossover::parameters::makeRangeGroupId;
using ava::crossover::parameters::makeRangeGroupName;
using ava::crossover::parameters::makeRangeParameterId;

namespace
{
struct ParameterOrderEntry
{
    const char* key;
    const char* block;
    const char* label;
};

inline constexpr auto tlsCrossoverOrder = std::to_array<ParameterOrderEntry>({
    { "gainMid", "GAIN", "MID" },
    { "gainMidMute", "GAIN / MID", "MUTE" },
    { "gainSide", "GAIN", "SIDE" },
    { "gainSideMute", "GAIN / SIDE", "MUTE" },
    { "gainL", "GAIN", "LEFT" },
    { "gainLMute", "GAIN / LEFT", "MUTE" },
    { "gainR", "GAIN", "RIGHT" },
    { "gainRMute", "GAIN / RIGHT", "MUTE" },
    { "gainLr", "GAIN", "STEREO" },
    { "gainLrMute", "GAIN / STEREO", "MUTE" },
    { "gainLOrder", "GAIN ORDER", "LEFT" },
    { "gainROrder", "GAIN ORDER", "RIGHT" },
    { "gainMidOrder", "GAIN ORDER", "MID" },
    { "gainSideOrder", "GAIN ORDER", "SIDE" },
    { "halfPositive", "RECTIFICATION", "HPOS" },
    { "halfNegative", "RECTIFICATION", "HNEG" },
    { "fullPositive", "RECTIFICATION", "FPOS" },
    { "fullNegative", "RECTIFICATION", "FNEG" },
    { "left", "PANORAMA", "LEFT" },
    { "right", "PANORAMA", "RIGHT" },
    { "law", "PANORAMA", "LAW" },
    { "impact", "SHEAR", "IMPACT" },
    { "impactDirection", "SHEAR", "DIRECTION" },
    { "mid", "MS BALANCE", "MID" },
    { "side", "MS BALANCE", "SIDE" },
    { "degree", "ORTHOGONAL", "DEGREE" },
    { "flipRight", "ORTHOGONAL", "FLIP RIGHT" },
    { "listenLc", "LISTEN", "LC" },
    { "listenRc", "LISTEN", "RC" },
    { "listenMc", "LISTEN", "MC" },
    { "listenSc", "LISTEN", "SC" },
    { "listenLl", "LISTEN", "LL" },
    { "listenRr", "LISTEN", "RR" },
    { "listenSs", "LISTEN", "SS" },
    { "stereoDelay", "DELAY", "STEREO" },
    { "leftDelay", "DELAY", "LEFT" },
    { "rightDelay", "DELAY", "RIGHT" },
    { "leftPhase", "PHASE", "LEFT" },
    { "rightPhase", "PHASE", "RIGHT" },
});

constexpr size_t numRanges = tls::dsp::ProcessorBank::numRanges;

juce::String makeCrossoverHostName(const size_t rangeIndex, const juce::String& blockName, const juce::String& parameterName)
{
    return "TLS / CROSSOVER " + juce::String(static_cast<int>(rangeIndex + 1))
        + " / " + blockName + " / " + parameterName;
}

juce::String formatParameterValue(const float value, const int decimalPlaces, const bool muteAtMinimum, const float minimum)
{
    if (muteAtMinimum && value <= minimum)
        return "MUTED";

    return juce::String(value, juce::jmax(0, decimalPlaces));
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using Parameter = std::unique_ptr<juce::RangedAudioParameter>;

    auto floatParam = [] (const juce::String& id,
                          const juce::String& name,
                          const float min,
                          const float max,
                          const float step,
                          const float defaultValue,
                          const juce::String& label,
                          const int displayDecimals,
                          const bool muteAtMinimum,
                          const bool isAutomatable,
                          const bool isMeta = false) -> Parameter
    {
        const auto minimum = min;
        auto range = juce::NormalisableRange<float> { min, max, step };
        auto attributes = juce::AudioParameterFloatAttributes()
                              .withLabel(label)
                              .withAutomatable(isAutomatable)
                              .withMeta(isMeta)
                              .withStringFromValueFunction([displayDecimals, muteAtMinimum, minimum] (float value, int)
                              {
                                  return formatParameterValue(value, displayDecimals, muteAtMinimum, minimum);
                              })
                              .withValueFromStringFunction([minimum] (const juce::String& text)
                              {
                                  if (text.trim().equalsIgnoreCase("MUTED"))
                                      return minimum;

                                  return text.trim().getFloatValue();
                              });
        return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name, range, defaultValue, attributes);
    };

    auto boolParam = [] (const juce::String& id,
                         const juce::String& name,
                         const bool defaultValue,
                         const bool isAutomatable,
                         const bool isMeta = false) -> Parameter
    {
        auto attributes = juce::AudioParameterBoolAttributes()
                              .withAutomatable(isAutomatable)
                              .withMeta(isMeta);
        return std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, name, defaultValue, attributes);
    };

    auto choiceParam = [] (const juce::String& id,
                           const juce::String& name,
                           const juce::StringArray& choices,
                           const int defaultIndex,
                           const bool isAutomatable,
                           const bool isMeta = false) -> Parameter
    {
        auto attributes = juce::AudioParameterChoiceAttributes()
                              .withAutomatable(isAutomatable)
                              .withMeta(isMeta);
        return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, name, choices, defaultIndex, attributes);
    };

    Layout layout;

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>(makeRangeGroupId(rangeIndex),
                                                                          makeRangeGroupName(rangeIndex),
                                                                          " | ");

        for (const auto& entry : tlsCrossoverOrder)
        {
            const auto it = std::find_if(parameterSpecs.begin(), parameterSpecs.end(), [&entry] (const auto& spec)
            {
                return juce::String(spec.suffix) == entry.key;
            });

            if (it == parameterSpecs.end())
                continue;

            const auto parameterId = makeRangeParameterId(rangeIndex, it->suffix);
            const auto parameterName = makeCrossoverHostName(rangeIndex, entry.block, entry.label);

            if (it->type == ParameterType::boolean)
                group->addChild(boolParam(parameterId,
                                          parameterName,
                                          it->defaultValue >= 0.5f,
                                          false));
            else if (it->type == ParameterType::choice)
                group->addChild(choiceParam(parameterId,
                                            parameterName,
                                            juce::StringArray { "LEFT", "RIGHT" },
                                            juce::roundToInt(it->defaultValue),
                                            false));
            else
                group->addChild(floatParam(parameterId,
                                           parameterName,
                                           it->min,
                                           it->max,
                                           it->step,
                                           it->defaultValue,
                                           it->label,
                                           it->displayDecimals,
                                           it->muteAtMinimum,
                                           false));
        }

        layout.add(std::move(group));
    }

    return layout;
}
} // namespace tls::parameters
