#include "Parameters.h"

#include "../../crossover/ParameterIds.h"
#include "SampleProcessor.h"
#include "../shared/ParameterFormatting.h"
#include "../shared/ParameterLayout.h"

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
    { ParameterSlot::halfPositive, "RECT", "HALF-POS" },
    { ParameterSlot::halfNegative, "RECT", "HALF-NEG" },
    { ParameterSlot::fullPositive, "RECT", "FULL-POS" },
    { ParameterSlot::fullNegative, "RECT", "FULL-NEG" },
    { ParameterSlot::left, "PANORAMA", "LEFT" },
    { ParameterSlot::right, "PANORAMA", "RIGHT" },
    { ParameterSlot::law, "PANORAMA", "LAW" },
    { ParameterSlot::impact, "SHEAR", "IMPACT" },
    { ParameterSlot::impactDirection, "SHEAR", "DIRECTION" },
    { ParameterSlot::mid, "MS-BALANCE", "MID" },
    { ParameterSlot::side, "MS-BALANCE", "SIDE" },
    { ParameterSlot::degree, "ORTHO", "DEGREE" },
    { ParameterSlot::flipRight, "ORTHO", "FLIP-RIGHT" },
    { ParameterSlot::listenLc, "LISTEN", "LC" },
    { ParameterSlot::listenRc, "LISTEN", "RC" },
    { ParameterSlot::listenMc, "LISTEN", "MC" },
    { ParameterSlot::listenSc, "LISTEN", "SC" },
    { ParameterSlot::listenLl, "LISTEN", "LL" },
    { ParameterSlot::listenRr, "LISTEN", "RR" },
    { ParameterSlot::listenMsPlus3, "LISTEN", "MS+3" },
    { ParameterSlot::listenSs, "LISTEN", "SS" },
    { ParameterSlot::stereoDelay, "DELAY", "STEREO" },
    { ParameterSlot::leftDelay, "DELAY", "LEFT" },
    { ParameterSlot::rightDelay, "DELAY", "RIGHT" },
    { ParameterSlot::stereoPhase, "PHASE", "STEREO" },
    { ParameterSlot::leftPhase, "PHASE", "LEFT" },
    { ParameterSlot::rightPhase, "PHASE", "RIGHT" },
    { ParameterSlot::combCount, "COMB", "COUNT" },
    { ParameterSlot::combShift, "COMB", "SHIFT" },
    { ParameterSlot::combShiftBypass, "COMB / SHIFT", "BYPASS" },
    { ParameterSlot::combCountBypass, "COMB / COUNT", "BYPASS" },
    { ParameterSlot::listenBypass, "LISTEN", "BYPASS" },
    { ParameterSlot::gainBypass, "GAIN", "BYPASS" },
    { ParameterSlot::delayBypass, "DELAY", "BYPASS" },
    { ParameterSlot::phaseBypass, "PHASE", "BYPASS" },
    { ParameterSlot::panoramaBypass, "PANORAMA", "BYPASS" },
    { ParameterSlot::shearBypass, "SHEAR", "BYPASS" },
    { ParameterSlot::balanceBypass, "MS-BALANCE", "BYPASS" },
    { ParameterSlot::orthoBypass, "ORTHO", "BYPASS" },
    { ParameterSlot::rectBypass, "RECT", "BYPASS" },
    { ParameterSlot::gainStereoBypass, "GAIN / STEREO", "BYPASS" },
    { ParameterSlot::gainLeftBypass, "GAIN / LEFT", "BYPASS" },
    { ParameterSlot::gainRightBypass, "GAIN / RIGHT", "BYPASS" },
    { ParameterSlot::gainMidBypass, "GAIN / MID", "BYPASS" },
    { ParameterSlot::gainSideBypass, "GAIN / SIDE", "BYPASS" },
    { ParameterSlot::listenHidden, "LISTEN", "HIDDEN" },
    { ParameterSlot::gainHidden, "GAIN", "HIDDEN" },
    { ParameterSlot::delayHidden, "DELAY", "HIDDEN" },
    { ParameterSlot::phaseHidden, "PHASE", "HIDDEN" },
    { ParameterSlot::panoramaHidden, "PANORAMA", "HIDDEN" },
    { ParameterSlot::shearHidden, "SHEAR", "HIDDEN" },
    { ParameterSlot::balanceHidden, "MS-BALANCE", "HIDDEN" },
    { ParameterSlot::orthoHidden, "ORTHO", "HIDDEN" },
    { ParameterSlot::rectHidden, "RECT", "HIDDEN" },
    { ParameterSlot::combHidden, "COMB", "HIDDEN" },
});

static_assert(parameterOrder.size() == numParameterSlots);

constexpr size_t numRanges = tls::dsp::ProcessorBank::numRanges;

juce::String makeRangeHostName(const size_t rangeIndex, const juce::String& blockName, const juce::String& parameterName)
{
    return "TLS / BAND " + juce::String(static_cast<int>(rangeIndex + 1))
        + " / " + blockName + " / " + parameterName;
}

}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using ava::modules::parameters::makeBoolParameter;
    using ava::modules::parameters::makeChoiceParameter;
    using ava::modules::parameters::makeFloatParameter;

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
                group->addChild(makeBoolParameter(parameterId, parameterName, spec.defaultValue >= 0.5f));
            else if (spec.type == ParameterType::choice)
                group->addChild(makeChoiceParameter(parameterId, parameterName,
                                                     { "LEFT", "RIGHT" }, juce::roundToInt(spec.defaultValue)));
            else
            {
                auto range=juce::NormalisableRange<float>(spec.min,spec.max,spec.step);
                if(entry.slot==ParameterSlot::combCount)
                {
                    range=juce::NormalisableRange<float>(spec.min,spec.max,
                        [](float minimum,float maximum,float value) { return minimum+(maximum-minimum)*value; },
                        [](float minimum,float maximum,float value) { return (value-minimum)/(maximum-minimum); },
                        [](float minimum,float maximum,float value) {
                        const auto count=juce::jlimit(minimum,maximum,static_cast<float>(juce::roundToInt(value)));
                        return count<=0.0f ? 0.0f : juce::jmax(2.0f,count);
                    });
                    range.interval=spec.step;
                }
                group->addChild(makeFloatParameter(
                    parameterId, parameterName,
                    range, spec.defaultValue,
                    [decimalPlaces = spec.displayDecimals] (float value, int)
                    {
                        return ava::modules::parameters::formatFixedDecimalValue(value, decimalPlaces);
                    }, spec.label));
            }
        }

        layout.add(std::move(group));
    }

    return layout;
}
}
