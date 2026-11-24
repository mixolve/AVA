#include "BlockParameters.h"

#include "../../crossover/ParameterIds.h"
#include "BlockDspCore.h"
#include "ParameterFormatting.h"
#include "ParameterLayout.h"

#include <array>
#include <memory>

namespace ava::modules::chain::parameters
{
using ava::modules::parameters::ValueText;
using ava::crossover::parameters::makeRangeGroupId;
using ava::crossover::parameters::makeRangeGroupName;
using ava::crossover::parameters::makeRangeParameterId;

namespace
{
inline constexpr std::array<ParameterSlot, numParameterSlots> parameterOrder {{
    ParameterSlot::gainMid,
    ParameterSlot::gainMidMute,
    ParameterSlot::gainSide,
    ParameterSlot::gainSideMute,
    ParameterSlot::gainL,
    ParameterSlot::gainLMute,
    ParameterSlot::gainR,
    ParameterSlot::gainRMute,
    ParameterSlot::gainLr,
    ParameterSlot::gainLrMute,
    ParameterSlot::halfPositive,
    ParameterSlot::halfNegative,
    ParameterSlot::fullPositive,
    ParameterSlot::fullNegative,
    ParameterSlot::left,
    ParameterSlot::right,
    ParameterSlot::law,
    ParameterSlot::impact,
    ParameterSlot::impactDirection,
    ParameterSlot::mid,
    ParameterSlot::side,
    ParameterSlot::degree,
    ParameterSlot::flipRight,
    ParameterSlot::listenLc,
    ParameterSlot::listenRc,
    ParameterSlot::listenMc,
    ParameterSlot::listenSc,
    ParameterSlot::listenLl,
    ParameterSlot::listenRr,
    ParameterSlot::listenMsPlus3,
    ParameterSlot::listenSs,
    ParameterSlot::stereoDelay,
    ParameterSlot::leftDelay,
    ParameterSlot::rightDelay,
    ParameterSlot::delayLinkOpp,
    ParameterSlot::stereoPhase,
    ParameterSlot::leftPhase,
    ParameterSlot::rightPhase,
    ParameterSlot::combCount,
    ParameterSlot::combShift,
    ParameterSlot::combShiftBypass,
    ParameterSlot::combCountBypass,
    ParameterSlot::listenBypass,
    ParameterSlot::gainBypass,
    ParameterSlot::delayBypass,
    ParameterSlot::phaseBypass,
    ParameterSlot::panoramaBypass,
    ParameterSlot::shearBypass,
    ParameterSlot::balanceBypass,
    ParameterSlot::orthoBypass,
    ParameterSlot::rectBypass,
    ParameterSlot::gainStereoBypass,
    ParameterSlot::gainLeftBypass,
    ParameterSlot::gainRightBypass,
    ParameterSlot::gainMidBypass,
    ParameterSlot::gainSideBypass,
    ParameterSlot::listenHidden,
    ParameterSlot::gainHidden,
    ParameterSlot::delayHidden,
    ParameterSlot::phaseHidden,
    ParameterSlot::panoramaHidden,
    ParameterSlot::shearHidden,
    ParameterSlot::balanceHidden,
    ParameterSlot::orthoHidden,
    ParameterSlot::rectHidden,
    ParameterSlot::combHidden,
    ParameterSlot::combMix,
    ParameterSlot::shiftPitch,
    ParameterSlot::shiftFrequency,
    ParameterSlot::shiftBypass,
    ParameterSlot::shiftHidden,
    ParameterSlot::shiftWindow,
    ParameterSlot::shiftPitchMode,
    ParameterSlot::shiftDetector,
    ParameterSlot::shiftTransients,
    ParameterSlot::shiftIndependentPhase,
    ParameterSlot::shiftOrder,
    ParameterSlot::shiftQ,
    ParameterSlot::shiftGainOrig,
    ParameterSlot::shiftGainPitched,
    ParameterSlot::varispeedRate,
    ParameterSlot::varispeedThreshold,
    ParameterSlot::varispeedRetrigger,
    ParameterSlot::varispeedCrossfade,
    ParameterSlot::varispeedPreserve,
    ParameterSlot::varispeedBypass,
    ParameterSlot::varispeedHidden,
    ParameterSlot::varispeedUnits,
    ParameterSlot::varispeedQuality,
    ParameterSlot::flipRightBypass,
    ParameterSlot::shiftSoloOrig,
    ParameterSlot::shiftSoloPitched,
    ParameterSlot::shiftGainOrigBypass,
    ParameterSlot::shiftGainPitchedBypass,
    ParameterSlot::sibilLevel, ParameterSlot::sibilBrightness, ParameterSlot::sibilNoise,
    ParameterSlot::sibilFade,
    ParameterSlot::sibilGainS, ParameterSlot::sibilGainRest,
    ParameterSlot::sibilSoloS, ParameterSlot::sibilSoloRest,
    ParameterSlot::sibilBypass, ParameterSlot::sibilHidden,
    ParameterSlot::sibilEdgeDetect,
}};

static_assert(parameterOrder.size() == numParameterSlots);
static_assert([] {
    std::array<bool,numParameterSlots> seen {};
    for (const auto slot:parameterOrder) {
        const auto i=toIndex(slot);
        if(i>=seen.size()||seen[i])return false;
        seen[i]=true;
    }
    return true;
}(), "Every parameter must be registered exactly once");

constexpr size_t numRanges = ava::modules::chain::dsp::ProcessorBank::numRanges;

juce::String makeRangeHostName(const size_t rangeIndex, const juce::String& blockName, const juce::String& parameterName)
{
    return "BAND " + juce::String(static_cast<int>(rangeIndex + 1))
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

        for (const auto slot : parameterOrder)
        {
            const auto& spec = parameterSpecs[toIndex(slot)];
            const auto parameterId = makeRangeParameterId(rangeIndex, spec.suffix);
            const auto& label = hostLabels[toIndex(slot)];
            const auto parameterName = makeRangeHostName(rangeIndex, label.block, label.label);

            if (spec.type == ParameterType::boolean)
                group->addChild(makeBoolParameter(parameterId, parameterName, spec.defaultValue >= 0.5f));
            else if (spec.type == ParameterType::choice)
            {
                const auto choices = [&]() -> juce::StringArray {
                    if (slot == ParameterSlot::varispeedQuality) return { "DEFAULT", "FAST", "HIGH QUALITY" };
                    if (slot == ParameterSlot::shiftOrder) return { "1", "2", "4", "6", "8" };
                    if (slot == ParameterSlot::shiftWindow) return { "SHORT", "STANDARD", "LONG" };
                    if (slot == ParameterSlot::shiftPitchMode) return { "SPEED", "QUALITY", "CONSIST" };
                    if (slot == ParameterSlot::shiftDetector) return { "COMPOUND", "PERC", "SOFT" };
                    if (slot == ParameterSlot::shiftTransients) return { "CRISP", "MIXED", "SMOOTH" };
                    return { "LEFT", "RIGHT" };
                }();
                group->addChild(makeChoiceParameter(parameterId, parameterName, choices, juce::roundToInt(spec.defaultValue)));
            }
            else
            {
                auto range=juce::NormalisableRange<float>(spec.min,spec.max,spec.step);
                if(slot==ParameterSlot::combCount)
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
                if (slot == ParameterSlot::shiftGainOrig || slot == ParameterSlot::shiftGainPitched)
                {
                    // Snap around zero so a nominal 0 dB remains exactly unity.
                    range = juce::NormalisableRange<float>(spec.min, spec.max,
                        [](float minimum, float maximum, float value) { return minimum + (maximum - minimum) * value; },
                        [](float minimum, float maximum, float value) { return (value - minimum) / (maximum - minimum); },
                        [](float minimum, float maximum, float value) {
                            return juce::jlimit(minimum, maximum, std::round(value * 100.0f) / 100.0f);
                        });
                    range.interval = spec.step;
                }
                group->addChild(makeFloatParameter(
                    parameterId, parameterName,
                    range, spec.defaultValue,
                    [decimalPlaces = spec.displayDecimals, gain = juce::String(spec.suffix).endsWith(".gain")] (float value) -> ValueText
                    {
                        if (gain && ava::modules::dsp::isGainMuted(value)) return ValueText::named("MUTED", true);
                        return ava::modules::parameters::formatFixedDecimalValue(value, decimalPlaces);
                    }, spec.label));
            }
        }

        layout.add(std::move(group));
    }

    return layout;
}
}

namespace ava::modules::chain::parameters
{
ControlPolicy getControlPolicy(const juce::AudioProcessorValueTreeState& state, size_t range, const juce::String& suffix)
{
    ControlPolicy result;
    for (const auto& spec : parameterSpecs) if (suffix == spec.suffix) result.metadata = spec.control;
    if (suffix == "varispeed.rate") {
        static const char* const dependencies[] {"varispeed.units"};
        result.dependencies = dependencies;
        const auto* value = state.getParameter(ava::crossover::parameters::makeRangeParameterId(range, "varispeed.units"));
        const auto semitones = value != nullptr && value->getValue() >= .5f;
        result.title = semitones ? "PITCH" : "RATE";
        if (semitones) result.presentation = {1.0, {-24.0, 24.0}, ava::modules::parameters::NumericPresentation::Mapping::rateToSemitones};
    }
    if (suffix == "varispeed.quality") {
        static const char* const dependencies[] {"varispeed.preserve"};
        result.dependencies = dependencies;
        const auto* preserve = state.getParameter(ava::crossover::parameters::makeRangeParameterId(range, "varispeed.preserve"));
        result.enabled = preserve != nullptr && preserve->getValue() >= .5f;
    }
    if (suffix == "comb.shift" || suffix == "comb.mix")
    {
        const auto id = ava::crossover::parameters::makeRangeParameterId(range, "comb.count");
        static const char* const dependencies[] { "comb.count" };
        result.dependencies = dependencies;
        const auto* count = state.getRawParameterValue(id);
        result.enabled = count != nullptr && count->load() > 0.0f;
    }
    return result;
}
}
