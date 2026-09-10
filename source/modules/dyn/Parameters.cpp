#include "Parameters.h"

#include "../../crossover/ParameterIds.h"
#include "DspCore.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <memory>

namespace dyn::parameters
{
using ava::crossover::parameters::makeRangeGroupId;
using ava::crossover::parameters::makeRangeGroupName;
using ava::crossover::parameters::makeRangeParameterId;

namespace
{
struct ParameterOrderEntry
{
    const char* key;
    const char* label;
};

inline constexpr auto dynCrossoverOrder = std::to_array<ParameterOrderEntry>({
    { "morph", "MORPH" },
    { "ratio", "RATIO" },
    { "knee", "KNEE" },
    { "peak_hold", "PEAK-HOLD" },
    { "lookahead", "LOOKAHEAD" },
    { "tension_floor", "TEN-FLOOR" },
    { "tension_hysteresis", "TEN-HYST" },
    { "release_form", "REL-FORM" },
    { "release_curve", "REL-CURVE" },
    { "adaptive_offset", "ADAP SETTINGS / OFFSET" },
    { "adaptive_attack", "ADAP SETTINGS / ATTACK" },
    { "adaptive_hold", "ADAP SETTINGS / HOLD" },
    { "adaptive_release", "ADAP SETTINGS / RELEASE" },
    { "linkUpDown", "LINKING / UPDN (DUAL-MONO)" },
    { "linkLeftRight", "LINKING / LR (STEREO)" },
    { "linkOpposite", "LINKING / OPP" },
    { "leftUpThreshold", "L.UP.THR" },
    { "leftUpAdaptive", "L.UP.ADAP" },
    { "leftUpTension", "L.UP.TENS" },
    { "leftUpRelease", "L.UP.REL" },
    { "leftUpOutput", "L.UP.OUT" },
    { "leftDownThreshold", "L.DN.THR" },
    { "leftDownAdaptive", "L.DN.ADAP" },
    { "leftDownTension", "L.DN.TENS" },
    { "leftDownRelease", "L.DN.REL" },
    { "leftDownOutput", "L.DN.OUT" },
    { "rightUpThreshold", "R.UP.THR" },
    { "rightUpAdaptive", "R.UP.ADAP" },
    { "rightUpTension", "R.UP.TENS" },
    { "rightUpRelease", "R.UP.REL" },
    { "rightUpOutput", "R.UP.OUT" },
    { "rightDownThreshold", "R.DN.THR" },
    { "rightDownAdaptive", "R.DN.ADAP" },
    { "rightDownTension", "R.DN.TENS" },
    { "rightDownRelease", "R.DN.REL" },
    { "rightDownOutput", "R.DN.OUT" },
    { "delta", "DELTA" },
});

constexpr size_t numRanges = dyn::dsp::ProcessorBank::numRanges;

juce::String makeCrossoverHostName(const size_t rangeIndex, const juce::String& moduleName, const juce::String& parameterName)
{
    return moduleName + " / CROSSOVER " + juce::String(static_cast<int>(rangeIndex + 1))
        + " / DYNAMIC PROCESSOR / " + parameterName;
}

double roundToDisplayStep(const double value) noexcept
{
    auto rounded = std::floor((value * 100.0) + 0.5) * 0.01;

    if (std::abs(rounded) < 0.05)
        rounded = 0.0;

    return rounded;
}

juce::String formatParameterValue(const float value)
{
    return juce::String::formatted("%.2f", roundToDisplayStep(value));
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
                          const bool isAutomatable,
                          const bool isRatio,
                          const bool isMeta = false) -> Parameter
    {
        auto range = juce::NormalisableRange<float> { min, max, step };
        auto attributes = juce::AudioParameterFloatAttributes()
                              .withLabel(label)
                              .withAutomatable(isAutomatable)
                              .withMeta(isMeta)
                              .withStringFromValueFunction([isRatio] (float value, int)
                              {
                                  return isRatio ? formatParameterValue(value) + ":1"
                                                 : formatParameterValue(value);
                              })
                              .withValueFromStringFunction([] (const juce::String& text)
                              {
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
                           const int defaultValue,
                           const bool isAutomatable,
                           const bool isMeta = false) -> Parameter
    {
        auto attributes = juce::AudioParameterChoiceAttributes()
                              .withAutomatable(isAutomatable)
                              .withMeta(isMeta);
        return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 },
                                                            name,
                                                            juce::StringArray { "LIN", "LOG" },
                                                            defaultValue,
                                                            attributes);
    };

    Layout layout;

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>(makeRangeGroupId(rangeIndex),
                                                                          makeRangeGroupName(rangeIndex),
                                                                          " | ");

        for (const auto& entry : dynCrossoverOrder)
        {
            const auto it = std::find_if(parameterSpecs.begin(), parameterSpecs.end(), [&entry] (const auto& spec)
            {
                return juce::String(spec.suffix) == entry.key;
            });

            if (it == parameterSpecs.end())
                continue;

            const auto parameterId = makeRangeParameterId(rangeIndex, it->suffix);
            const auto parameterName = makeCrossoverHostName(rangeIndex, "DYN", entry.label);

            if (it->type == ParameterType::boolean)
                group->addChild(boolParam(parameterId, parameterName, it->defaultValue >= 0.5f, false));
            else if (it->type == ParameterType::choice)
                group->addChild(choiceParam(parameterId,
                                            parameterName,
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
                                           false,
                                           juce::String(it->suffix) == "ratio"));
        }

        layout.add(std::move(group));
    }

    return layout;
}
} // namespace dyn::parameters
