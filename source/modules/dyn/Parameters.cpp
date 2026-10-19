#include "Parameters.h"

#include "../../crossover/ParameterIds.h"
#include "SampleProcessor.h"
#include "../shared/ParameterFormatting.h"
#include "../shared/ParameterLayout.h"

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
    { ParameterSlot::domain, "DOMAIN" },
    { ParameterSlot::mode, "MODE" },
    { ParameterSlot::corrPeakCrest, "CORR / PEAK-CREST" },
    { ParameterSlot::corrThreshold, "CORR / THRESHOLD" },
    { ParameterSlot::corrThresholdHysteresis, "CORR / THRESH-HYST" },
    { ParameterSlot::corrKnee, "CORR / COMP-KNEE" },
    { ParameterSlot::corrSlope, "CORR / SLOPE" },
    { ParameterSlot::corrOffset, "CORR / ADAP-OFFSET" },
    { ParameterSlot::corrSmoothing, "CORR / SMOOTHING" },
    { ParameterSlot::corrImpact, "CORR / IMPACT" },
    { ParameterSlot::corrTensionFloor, "CORR / TENSION-FLOOR" },
    { ParameterSlot::transientGain, "TRANS/SUST / TRANS-GAIN" },
    { ParameterSlot::sustainGain, "TRANS/SUST / SUST-GAIN" },
    { ParameterSlot::transientMute, "TRANS/SUST / TRANS-MUTE" },
    { ParameterSlot::sustainMute, "TRANS/SUST / SUST-MUTE" },
    { ParameterSlot::retrigger, "TRANS/SUST / RETRIGGER" },
    { ParameterSlot::oneShot, "TRANS/SUST / ONE-SHOT" },
    { ParameterSlot::fftSize, "SPECTRAL / WIN-SIZE" },
    { ParameterSlot::fftOverlap, "SPECTRAL / OVERLAP" },
    { ParameterSlot::spectralSlope, "SPECTRAL / SLOPE" },
    { ParameterSlot::morph, "PROCESSOR / MORPH" },
    { ParameterSlot::ratio, "PROCESSOR / RATIO" },
    { ParameterSlot::progression, "PROCESSOR / PROGRESSION" },
    { ParameterSlot::progressionSpan, "PROCESSOR / PROG-SPAN" },
    { ParameterSlot::corrProgressionSpan, "CORR / PROG-SPAN" },
    { ParameterSlot::compKnee, "PROCESSOR / COMP-KNEE" },
    { ParameterSlot::lookahead, "PROCESSOR / LOOKAHEAD" },
    { ParameterSlot::tensionFloor, "PROCESSOR / TENSION-FLOOR" },
    { ParameterSlot::tensionHysteresis, "PROCESSOR / TENSION-HYST" },
    { ParameterSlot::autoRelease, "PROCESSOR / AUTO-REL" },
    { ParameterSlot::releaseMemory, "PROCESSOR / REL-MEMORY" },
    { ParameterSlot::releaseDepth, "PROCESSOR / REL-DEPTH" },
    { ParameterSlot::releaseCurve, "PROCESSOR / RELEASE-CURVE" },
    { ParameterSlot::threshold, "PROCESSOR / THRESHOLD" },
    { ParameterSlot::thresholdHysteresis, "PROCESSOR / THRESH-HYST" },
    { ParameterSlot::adaptive, "PROCESSOR / THRESHOLD-ADAP" },
    { ParameterSlot::tension, "PROCESSOR / TENSION" },
    { ParameterSlot::processorReleasePeak, "PROCESSOR / RELEASE-PEAK" },
    { ParameterSlot::processorReleaseRms, "PROCESSOR / RELEASE-RMS" },
    { ParameterSlot::makeUp, "PROCESSOR / MAKE-UP" },
    { ParameterSlot::adaptiveOffset, "ADAPTIVE / ADAP-OFFSET" },
    { ParameterSlot::adaptiveAttack, "ADAPTIVE / ADAP-ATTACK" },
    { ParameterSlot::adaptiveHold, "ADAPTIVE / ADAP-HOLD" },
    { ParameterSlot::adaptiveRelease, "ADAPTIVE / ADAP-RELEASE" },
    { ParameterSlot::delta, "PROCESSOR / DELTA" },
    { ParameterSlot::processorHold, "PROCESSOR / HOLD" },
    { ParameterSlot::processorAttack, "PROCESSOR / ATTACK" },
    { ParameterSlot::attackCurve, "PROCESSOR / ATTACK-CURVE" },
    { ParameterSlot::processorDelay, "PROCESSOR / DELAY" },
    { ParameterSlot::inputGain, "PROCESSOR / INPUT" },
    { ParameterSlot::adaptiveTime, "ADAPTIVE / ADAP-TIME" },
    { ParameterSlot::mix, "PROCESSOR / MIX" },
    { ParameterSlot::output, "PROCESSOR / OUTPUT" },
    { ParameterSlot::range, "PROCESSOR / RANGE" },
    { ParameterSlot::sidechainChannel, "SIDE CHAIN / SC-CHANNEL" },
    { ParameterSlot::sidechainListen, "SIDE CHAIN / SC-LISTEN" },
    { ParameterSlot::peakRms, "PROCESSOR / PEAK/RMS" },
    { ParameterSlot::peakCrest, "PROCESSOR / PEAK-CREST" },
    { ParameterSlot::rmsTime, "PROCESSOR / RMS-TIME" },
    { ParameterSlot::releaseType, "PROCESSOR / RELEASE-TYPE" },
    { ParameterSlot::rangeCurve, "PROCESSOR / RANGE-CURVE" },
    { ParameterSlot::attackMode, "PROCESSOR / ATTACK MODE" },
    { ParameterSlot::attackNote, "PROCESSOR / ATTACK NOTE" },
    { ParameterSlot::holdMode, "PROCESSOR / HOLD MODE" },
    { ParameterSlot::holdNote, "PROCESSOR / HOLD NOTE" },
    { ParameterSlot::releasePeakMode, "PROCESSOR / RELEASE-PEAK MODE" },
    { ParameterSlot::releasePeakNote, "PROCESSOR / RELEASE-PEAK NOTE" },
    { ParameterSlot::releaseRmsMode, "PROCESSOR / RELEASE-RMS MODE" },
    { ParameterSlot::releaseRmsNote, "PROCESSOR / RELEASE-RMS NOTE" },
    { ParameterSlot::feedback, "PROCESSOR / FWD/FB" },
    { ParameterSlot::clipKnee, "PROCESSOR / CLIP-KNEE" },
});

static_assert(parameterOrder.size() == numParameterSlots);

constexpr size_t numRanges = dyn::dsp::ProcessorBank::numRanges;

juce::String makeRangeHostName(const size_t rangeIndex, const juce::String& parameterName)
{
    return "DYN / BAND " + juce::String(static_cast<int>(rangeIndex + 1))
        + " / " + parameterName;
}

juce::String formatParameterValue(const float value)
{
    return ava::modules::parameters::formatFixedDecimalValue(value, 2);
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
            const auto parameterName = makeRangeHostName(rangeIndex, entry.label);

            if (spec.type == ParameterType::boolean)
                group->addChild(makeBoolParameter(parameterId, parameterName, spec.defaultValue >= 0.5f));
            else if (spec.type == ParameterType::choice)
                group->addChild(makeChoiceParameter(parameterId, parameterName,
                                                     entry.slot == ParameterSlot::mode ? juce::StringArray { "COMP/CLIP / FREQ", "TRANS/SUST / CORR" }
                                                         : entry.slot == ParameterSlot::sidechainChannel ? ava::channels::choices()
                                                         : entry.slot == ParameterSlot::domain ? juce::StringArray { "TIME", "SPECTRAL" }
                                                         : entry.slot == ParameterSlot::fftSize ? juce::StringArray { "1024", "2048", "4096", "8192", "16384" }
                                                         : entry.slot == ParameterSlot::fftOverlap ? juce::StringArray { "8", "16", "32" }
                                                         : juce::String(spec.suffix).endsWith("-mode") ? juce::StringArray { "MS", "NOTE" }
                                                         : getTimeNoteChoices(),
                                                     juce::roundToInt(spec.defaultValue)));
            else
                group->addChild(makeFloatParameter(
                    parameterId, parameterName,
                    { spec.min, spec.max, spec.step }, spec.defaultValue,
                    [isMorph = entry.slot == ParameterSlot::morph,
                     isImpact = entry.slot == ParameterSlot::corrImpact,
                     isCorrSlope = entry.slot == ParameterSlot::corrSlope,
                     isMix = entry.slot == ParameterSlot::mix,
                     isPeakRms = entry.slot == ParameterSlot::peakRms,
                     isRangeCurve = entry.slot == ParameterSlot::rangeCurve,
                     isKnee = entry.slot == ParameterSlot::compKnee || entry.slot == ParameterSlot::clipKnee || entry.slot == ParameterSlot::corrKnee,
                     isReleaseType = entry.slot == ParameterSlot::releaseType,
                     isFeedback = entry.slot == ParameterSlot::feedback,
                     isRatio = entry.slot == ParameterSlot::ratio,
                     isCurve = entry.slot == ParameterSlot::releaseCurve || entry.slot == ParameterSlot::attackCurve] (float value, int)
                    {
                        if (isPeakRms && value <= 0.0f) return juce::String("PEAK");
                        if (isPeakRms && value >= 100.0f) return juce::String("RMS");
                        if (isCorrSlope) return ava::modules::parameters::formatFixedDecimalValue(value, 2);
                        if (isImpact && value <= -99.995f) return juce::String("LEFT");
                        if (isImpact && std::abs(value) < 0.005f) return juce::String("BOTH");
                        if (isImpact && value >= 99.995f) return juce::String("RIGHT");
                        if (isRangeCurve && value == 0.0f) return juce::String("HARD");
                        if (isRangeCurve && value == 100.0f) return juce::String("SOFT");

                        if (isKnee && value == 0.0f) return juce::String("HARD");

                        if (isMorph && value == 0.0f)
                            return juce::String("CLIPPER");
                        if (isMorph && value == 100.0f)
                            return juce::String("COMPRESSOR");

                        if (isFeedback && value == 0.0f) return juce::String("FWD");
                        if (isFeedback && value == 100.0f) return juce::String("FB");

                        if (isReleaseType && value == 0.0f) return juce::String("TO-ZERO");
                        if (isReleaseType && value == 100.0f) return juce::String("TO-SIGNAL");

                        if (isMix && value == 0.0f)
                            return juce::String("DRY");
                        if (isMix && value == 100.0f)
                            return juce::String("WET");

                        if (isCurve && std::abs(value) < 0.005f)
                            return juce::String("LIN");

                        return isRatio ? formatParameterValue(value) + ":1" : formatParameterValue(value);
                    }, spec.label));
        }

        layout.add(std::move(group));
    }

    return layout;
}
}
