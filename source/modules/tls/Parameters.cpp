#include "Parameters.h"

#include "../../crossover/ParameterIds.h"
#include "DspCore.h"

#include <array>
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
    ParameterSlot slot;
    const char* block;
    const char* label;
};

inline constexpr auto parameterOrder = std::to_array<ParameterOrderEntry>({
    { ParameterSlot::gainMid, "GAIN", "MID" },
    { ParameterSlot::gainMidMute, "GAIN / MID", "MUTE" },
    { ParameterSlot::gainSide, "GAIN", "SIDE" },
    { ParameterSlot::gainSideMute, "GAIN / SIDE", "MUTE" },
    { ParameterSlot::gainL, "GAIN", "LEFT" },
    { ParameterSlot::gainLMute, "GAIN / LEFT", "MUTE" },
    { ParameterSlot::gainR, "GAIN", "RIGHT" },
    { ParameterSlot::gainRMute, "GAIN / RIGHT", "MUTE" },
    { ParameterSlot::gainLr, "GAIN", "STEREO" },
    { ParameterSlot::gainLrMute, "GAIN / STEREO", "MUTE" },
    { ParameterSlot::gainLOrder, "GAIN ORDER", "LEFT" },
    { ParameterSlot::gainROrder, "GAIN ORDER", "RIGHT" },
    { ParameterSlot::gainMidOrder, "GAIN ORDER", "MID" },
    { ParameterSlot::gainSideOrder, "GAIN ORDER", "SIDE" },
    { ParameterSlot::halfPositive, "RECTIFICATION", "HPOS" },
    { ParameterSlot::halfNegative, "RECTIFICATION", "HNEG" },
    { ParameterSlot::fullPositive, "RECTIFICATION", "FPOS" },
    { ParameterSlot::fullNegative, "RECTIFICATION", "FNEG" },
    { ParameterSlot::left, "PANORAMA", "LEFT" },
    { ParameterSlot::right, "PANORAMA", "RIGHT" },
    { ParameterSlot::law, "PANORAMA", "LAW" },
    { ParameterSlot::impact, "SHEAR", "IMPACT" },
    { ParameterSlot::impactDirection, "SHEAR", "DIRECTION" },
    { ParameterSlot::mid, "MS BALANCE", "MID" },
    { ParameterSlot::side, "MS BALANCE", "SIDE" },
    { ParameterSlot::degree, "ORTHOGONAL", "DEGREE" },
    { ParameterSlot::flipRight, "ORTHOGONAL", "FLIP RIGHT" },
    { ParameterSlot::listenLc, "LISTEN", "LC" },
    { ParameterSlot::listenRc, "LISTEN", "RC" },
    { ParameterSlot::listenMc, "LISTEN", "MC" },
    { ParameterSlot::listenSc, "LISTEN", "SC" },
    { ParameterSlot::listenLl, "LISTEN", "LL" },
    { ParameterSlot::listenRr, "LISTEN", "RR" },
    { ParameterSlot::listenSs, "LISTEN", "SS" },
    { ParameterSlot::stereoDelay, "DELAY", "STEREO" },
    { ParameterSlot::leftDelay, "DELAY", "LEFT" },
    { ParameterSlot::rightDelay, "DELAY", "RIGHT" },
    { ParameterSlot::stereoPhase, "PHASE", "STEREO" },
    { ParameterSlot::leftPhase, "PHASE", "LEFT" },
    { ParameterSlot::rightPhase, "PHASE", "RIGHT" },
});

static_assert(parameterOrder.size() == numParameterSlots);

constexpr size_t numRanges = tls::dsp::ProcessorBank::numRanges;

juce::String makeRangeHostName(const size_t rangeIndex, const juce::String& blockName, const juce::String& parameterName)
{
    return "TLS / BAND " + juce::String(static_cast<int>(rangeIndex + 1))
        + " / " + blockName + " / " + parameterName;
}

juce::String formatParameterValue(const float value, const int decimalPlaces, const bool muteAtMinimum, const float minimum)
{
    if (muteAtMinimum && value <= minimum)
        return "MUTED";

    return juce::String(value, juce::jmax(0, decimalPlaces));
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

        for (const auto& entry : parameterOrder)
        {
            const auto& spec = parameterSpecs[toIndex(entry.slot)];
            const auto parameterId = makeRangeParameterId(rangeIndex, spec.suffix);
            const auto parameterName = makeRangeHostName(rangeIndex, entry.block, entry.label);

            if (spec.type == ParameterType::boolean)
                group->addChild(boolParam(parameterId,
                                          parameterName,
                                          spec.defaultValue >= 0.5f,
                                          false));
            else if (spec.type == ParameterType::choice)
                group->addChild(choiceParam(parameterId,
                                            parameterName,
                                            juce::StringArray { "LEFT", "RIGHT" },
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
                                           spec.displayDecimals,
                                           spec.muteAtMinimum,
                                           false));
        }

        layout.add(std::move(group));
    }

    return layout;
}
}
