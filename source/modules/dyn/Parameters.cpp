#include "Parameters.h"

#include "../../crossover/ParameterIds.h"
#include "DspCore.h"

#include <array>
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
    ParameterSlot slot;
    const char* label;
};

inline constexpr auto parameterOrder = std::to_array<ParameterOrderEntry>({
    { ParameterSlot::morph, "MAIN / MORPH" },
    { ParameterSlot::ratio, "MAIN / RATIO" },
    { ParameterSlot::knee, "MAIN / KNEE" },
    { ParameterSlot::peakHoldMs, "MAIN / PEAK-HOLD" },
    { ParameterSlot::lookahead, "MAIN / LOOKAHEAD" },
    { ParameterSlot::tensionFloor, "MAIN / TENSION-FLOOR" },
    { ParameterSlot::tensionHysteresis, "MAIN / TENSION-HYSTERESIS" },
    { ParameterSlot::releaseForm, "MAIN / RELEASE-FORM" },
    { ParameterSlot::releaseCurve, "MAIN / RELEASE-CURVE" },
    { ParameterSlot::linkUpDown, "LINKING / UP-DN (DUAL-MONO)" },
    { ParameterSlot::linkLeftRight, "LINKING / L-R (STEREO)" },
    { ParameterSlot::linkOpposite, "LINKING / OPPOSITE" },
    { ParameterSlot::leftUpThreshold, "PROCESSOR / L-UP-THRESHOLD" },
    { ParameterSlot::leftUpAdaptive, "PROCESSOR / L-UP-ADAPTIVE" },
    { ParameterSlot::leftUpTension, "PROCESSOR / L-UP-TENSION" },
    { ParameterSlot::leftUpRelease, "PROCESSOR / L-UP-RELEASE" },
    { ParameterSlot::leftUpOutput, "PROCESSOR / L-UP-OUTPUT" },
    { ParameterSlot::leftDownThreshold, "PROCESSOR / L-DN-THRESHOLD" },
    { ParameterSlot::leftDownAdaptive, "PROCESSOR / L-DN-ADAPTIVE" },
    { ParameterSlot::leftDownTension, "PROCESSOR / L-DN-TENSION" },
    { ParameterSlot::leftDownRelease, "PROCESSOR / L-DN-RELEASE" },
    { ParameterSlot::leftDownOutput, "PROCESSOR / L-DN-OUTPUT" },
    { ParameterSlot::rightUpThreshold, "PROCESSOR / R-UP-THRESHOLD" },
    { ParameterSlot::rightUpAdaptive, "PROCESSOR / R-UP-ADAPTIVE" },
    { ParameterSlot::rightUpTension, "PROCESSOR / R-UP-TENSION" },
    { ParameterSlot::rightUpRelease, "PROCESSOR / R-UP-RELEASE" },
    { ParameterSlot::rightUpOutput, "PROCESSOR / R-UP-OUTPUT" },
    { ParameterSlot::rightDownThreshold, "PROCESSOR / R-DN-THRESHOLD" },
    { ParameterSlot::rightDownAdaptive, "PROCESSOR / R-DN-ADAPTIVE" },
    { ParameterSlot::rightDownTension, "PROCESSOR / R-DN-TENSION" },
    { ParameterSlot::rightDownRelease, "PROCESSOR / R-DN-RELEASE" },
    { ParameterSlot::rightDownOutput, "PROCESSOR / R-DN-OUTPUT" },
    { ParameterSlot::adaptiveOffset, "ADAPTIVE / OFFSET" },
    { ParameterSlot::adaptiveAttack, "ADAPTIVE / ATTACK" },
    { ParameterSlot::adaptiveHold, "ADAPTIVE / HOLD" },
    { ParameterSlot::adaptiveRelease, "ADAPTIVE / RELEASE" },
    { ParameterSlot::delta, "DELTA" },
});

static_assert(parameterOrder.size() == numParameterSlots);

constexpr size_t numRanges = dyn::dsp::ProcessorBank::numRanges;

juce::String makeRangeHostName(const size_t rangeIndex, const juce::String& parameterName)
{
    return "DYN / BAND " + juce::String(static_cast<int>(rangeIndex + 1))
        + " / " + parameterName;
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

}

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

        for (const auto& entry : parameterOrder)
        {
            const auto& spec = parameterSpecs[toIndex(entry.slot)];
            const auto parameterId = makeRangeParameterId(rangeIndex, spec.suffix);
            const auto parameterName = makeRangeHostName(rangeIndex, entry.label);

            if (spec.type == ParameterType::boolean)
                group->addChild(boolParam(parameterId, parameterName, spec.defaultValue >= 0.5f, false));
            else if (spec.type == ParameterType::choice)
                group->addChild(choiceParam(parameterId,
                                            parameterName,
                                            juce::roundToInt(spec.defaultValue),
                                            false));
            else
                group->addChild(floatParam(parameterId,
                                           parameterName,
                                           spec.min,
                                           spec.max,
                                           spec.step,
                                           spec.defaultValue,
                                           spec.label,
                                           false,
                                           entry.slot == ParameterSlot::ratio));
        }

        layout.add(std::move(group));
    }

    return layout;
}
}
