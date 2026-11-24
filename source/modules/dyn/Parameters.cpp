#include "../shared/TimeUnits.h"
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
using ava::modules::parameters::ValueText;
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
    { ParameterSlot::corrSmoothing, "CORR / SMOOTH" },
    { ParameterSlot::corrImpact, "CORR / IMPACT" },
    { ParameterSlot::corrTensionFloor, "CORR / TENSION-FLOOR" },
    { ParameterSlot::transientGain, "TRANS SUST / TRANS-GAIN" },
    { ParameterSlot::sustainGain, "TRANS SUST / SUST-GAIN" },
    { ParameterSlot::transientMute, "TRANS SUST / TRANS-MUTE" },
    { ParameterSlot::sustainMute, "TRANS SUST / SUST-MUTE" },
    { ParameterSlot::retrigger, "TRANS SUST / RETRIG" },
    { ParameterSlot::oneShot, "TRANS SUST / ONE" },
    { ParameterSlot::fftSize, "SPECTRAL / WIN-SIZE" },
    { ParameterSlot::fftOverlap, "SPECTRAL / OVERLAP" },
    { ParameterSlot::spectralSlope, "SPECTRAL / SLOPE" },
    { ParameterSlot::morph, "PROCESSOR / MORPH" },
    { ParameterSlot::ratio, "PROCESSOR / RATIO" },
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
    { ParameterSlot::delta, "PROCESSOR / DELTA" },
    { ParameterSlot::processorHold, "PROCESSOR / HOLD" },
    { ParameterSlot::processorAttack, "PROCESSOR / ATTACK" },
    { ParameterSlot::attackCurve, "PROCESSOR / ATTACK-CURVE" },
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
    { ParameterSlot::lookaheadCurve, "PROCESSOR / LOOKAHEAD-CURVE" },
    { ParameterSlot::clipEql, "PROCESSOR / CLIP-EQL" },
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
            if (entry.slot == ParameterSlot::clipEql) continue;
            const auto& spec = parameterSpecs[toIndex(entry.slot)];
            const auto parameterId = makeRangeParameterId(rangeIndex, spec.suffix);
            const auto parameterName = makeRangeHostName(rangeIndex, entry.label);

            if (spec.type == ParameterType::boolean)
                group->addChild(makeBoolParameter(parameterId, parameterName, spec.defaultValue >= 0.5f));
            else if (spec.type == ParameterType::choice)
                group->addChild(makeChoiceParameter(parameterId, parameterName,
                                                     entry.slot == ParameterSlot::mode ? juce::StringArray { "COMP CLIP / FREQ", "TRANS SUST / CORR" }
                                                         : entry.slot == ParameterSlot::sidechainChannel ? ava::channels::choices()
                                                         : entry.slot == ParameterSlot::domain ? juce::StringArray { "TIME", "SPECTRAL" }
                                                         : entry.slot == ParameterSlot::fftSize ? juce::StringArray { "1024", "2048", "4096", "8192", "16384" }
                                                         : entry.slot == ParameterSlot::fftOverlap ? juce::StringArray { "8", "16", "32" }
                                                         : juce::String(spec.suffix).endsWith("-mode") ? juce::StringArray { "MS", "NOTE" }
                                                         : ava::modules::parameters::getTimeNoteChoices(),
                                                     juce::roundToInt(spec.defaultValue)));
            else
                group->addChild(makeFloatParameter(
                    parameterId, parameterName,
                    { spec.min, spec.max, spec.step }, spec.defaultValue,
                    [isTransientSustainGain = entry.slot == ParameterSlot::transientGain || entry.slot == ParameterSlot::sustainGain,
                     isMorph = entry.slot == ParameterSlot::morph,
                     isImpact = entry.slot == ParameterSlot::corrImpact,
                     isCorrSlope = entry.slot == ParameterSlot::corrSlope,
                     isMix = entry.slot == ParameterSlot::mix,
                     isPeakRms = entry.slot == ParameterSlot::peakRms,
                     isRangeCurve = entry.slot == ParameterSlot::rangeCurve,
                     isKnee = entry.slot == ParameterSlot::compKnee || entry.slot == ParameterSlot::clipKnee || entry.slot == ParameterSlot::corrKnee,
                     isFeedback = entry.slot == ParameterSlot::feedback,
                     isRatio = entry.slot == ParameterSlot::ratio,
                     isAttackCurve = entry.slot == ParameterSlot::attackCurve,
                     isCurve = entry.slot == ParameterSlot::releaseCurve || entry.slot == ParameterSlot::attackCurve || entry.slot == ParameterSlot::lookaheadCurve] (float value) -> ValueText
                    {
                        if (isTransientSustainGain && ava::modules::dsp::isGainMuted(value)) return ValueText::named("MUTED", true);
                        if (isPeakRms && value <= 0.0f) return ValueText::named("PEAK");
                        if (isPeakRms && value >= 100.0f) return ValueText::named("RMS");
                        if (isCorrSlope) return ava::modules::parameters::formatFixedDecimalValue(value, 2);
                        if (isImpact && value <= -99.995f) return ValueText::named("LEFT");
                        if (isImpact && std::abs(value) < 0.005f) return ValueText::named("BOTH");
                        if (isImpact && value >= 99.995f) return ValueText::named("RIGHT");
                        if (isRangeCurve && value == 0.0f) return ValueText::named("HARD");
                        if (isRangeCurve && value == 100.0f) return ValueText::named("SOFT");

                        if (isKnee && value == 0.0f) return ValueText::named("HARD");

                        if (isMorph && value == 0.0f)
                            return ValueText::named("CLIPPER");
                        if (isMorph && value == 100.0f)
                            return ValueText::named("COMPRESS");

                        if (isFeedback && value == 0.0f) return ValueText::named("FWD");
                        if (isFeedback && value == 100.0f) return ValueText::named("FB");

                        if (isMix && value == 0.0f)
                            return ValueText::named("DRY");
                        if (isMix && value == 100.0f)
                            return ValueText::named("WET");

                        if (isCurve && std::abs(value) < 0.005f)
                            return ValueText::named("LIN");

                        if (isAttackCurve) return ava::modules::parameters::formatFixedDecimalValue(value * 0.001f, 5);
                        return isRatio ? formatParameterValue(value) + ":1" : formatParameterValue(value);
                    }, spec.label, false,
                    juce::AudioParameterFloatAttributes().withAutomatable(false),
                    entry.slot == ParameterSlot::attackCurve ? 0.001 : 1.0));
        }

        layout.add(std::move(group));
    }

    // Append new parameters after the existing layout to preserve its indices.
    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>(
            makeRangeGroupId(rangeIndex) + "-bypasses", makeRangeGroupName(rangeIndex) + " / BYPASS", " | ");
        for (const auto& entry : parameterOrder)
        {
            const auto& spec = parameterSpecs[toIndex(entry.slot)];
            if (hasBypass(spec))
                group->addChild(makeBoolParameter(makeRangeParameterId(rangeIndex, bypassSuffix(spec).toRawUTF8()),
                    makeRangeHostName(rangeIndex, juce::String(entry.label) + " / BYPASS"), false));
        }
        layout.add(std::move(group));
    }

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>(
            makeRangeGroupId(rangeIndex) + "-internal",
            makeRangeGroupName(rangeIndex) + " / INTERNAL", " | ");
        group->addChild(makeBoolParameter(
            makeRangeParameterId(rangeIndex, "clip-qual"),
            makeRangeHostName(rangeIndex, "PROCESSOR / CLIP-EQL"), false));
        layout.add(std::move(group));
    }

    return layout;
}
}

namespace dyn::parameters
{
ControlPolicy getControlPolicy(const juce::AudioProcessorValueTreeState& state, const size_t range,
                               const juce::String& suffix)
{
    ControlPolicy result;
    for (const auto& spec : parameterSpecs) if (suffix == spec.suffix) result.metadata = spec.control;
    if (suffix == "attack-curve") result.presentation.scale = 0.001;
    const auto& metadata = result.metadata;
    const auto read = [&] (const char* name)
    {
        const auto* value = state.getRawParameterValue(makeRangeParameterId(range, name));
        const auto raw = value != nullptr ? value->load() : 0.0f;
        static const juce::StringArray rounded { "morph", "tension", "adaptive", "peak-rms", "range", "processor-release-peak", "processor-release-rms", "processor-attack" };
        if (rounded.contains(name))
            return float(ava::modules::dsp::roundToParameterStep(raw));
        return raw;
    };
    static const char* const dependencies[] { "domain", "mode", "morph", "tension", "adaptive", "peak-rms", "range", "auto-rel", "lookahead",
        "processor-attack-mode", "processor-attack", "processor-release-peak-mode", "processor-release-peak", "processor-release-rms-mode", "processor-release-rms" };
    result.dependencies = dependencies;
    const auto spectral = read("domain") >= 0.5f;
    const auto transientSustain = !spectral && read("mode") >= 0.5f;
    const auto correlation = spectral && read("mode") >= 0.5f;
    const auto spectralOnly = suffix == "fft-size" || suffix == "fft-overlap" || suffix == "spectral-slope";
    const auto timeOnly = suffix == "morph" || suffix == "clip-knee" || suffix == "feedback";
    const auto transientSustainOnly = suffix == "trans-gain" || suffix == "sust-gain" || suffix == "retrigger";
    result.visible = (!spectralOnly || spectral) && (!timeOnly || (!spectral && !transientSustain))
        && (!transientSustainOnly || transientSustain);
    // TENSION is also available in TRANS/SUST. Only COMP/CLIP-specific
    // processing controls remain disabled in TRANS/SUST.
    static const juce::StringArray compClipParameters { "morph", "clip-knee", "ratio", "feedback", "range", "range-curve" };
    const auto compClipOnly = compClipParameters.contains(suffix);
    result.enabled = (!spectralOnly || spectral) && (!timeOnly || !spectral)
        && (!transientSustainOnly || transientSustain) && (!transientSustain || !compClipOnly)
        && (!suffix.startsWith("corr-") || correlation);
    if (suffix == "mode")
    {
        result.cellSpan = 1;
        result.title = "MODE";
        result.choices = spectral ? juce::StringArray { "FREQ", "CORR" }
                                  : juce::StringArray { "COMP CLIP", "TRANS SUST" };
    }
    const auto finish = [&] (bool enabled) { result.enabled = result.enabled && enabled; return result; };
    {
        const auto pureClip = read("domain") < 0.5f && read("mode") < 0.5f && read("morph") == 0.0f;
        if (suffix == "tension-floor" || suffix == "corr-tension-floor" || suffix == "tension-hyst")
            return finish(std::abs(read("tension")) > 0.0f);
        // LOOKAHEAD is independent of MORPH, TENSION and ADAPTIVE.
        // Keep the control enabled in every processor mode, including pure CLIPPER.
        if (suffix == "lookahead")
            return result;
        if (suffix == "lookahead-curve")
            return finish(read("lookahead") > 0.0f);
        const auto envelopeActive = !pureClip;
        const auto detectorActive = envelopeActive || std::abs(read("tension")) > 0.0f;
        const auto peakActive = read("peak-rms") < 100.0f;
        const auto rmsActive = read("peak-rms") > 0.0f;
        if (suffix == "thresh-hyst" || suffix == "corr-thresh-hyst") return finish(detectorActive);
        if (suffix == "peak-rms") return finish(detectorActive);
        if (suffix == "peak-crest" || suffix == "corr-peak-crest") return finish(detectorActive && peakActive && rmsActive);
        if (suffix == "rms-time") return finish(detectorActive && rmsActive);
        if (suffix == "processor-release-peak") return finish(detectorActive && peakActive);
        if (suffix == "processor-release-rms") return finish(detectorActive && rmsActive);
        if (suffix == "attack-curve")
            return finish(envelopeActive
                && (read("processor-attack-mode") >= 0.5f || read("processor-attack") > 0.0f));
        if (suffix == "release-curve" || suffix == "auto-rel" || suffix == "rel-memory" || suffix == "rel-depth")
            return finish(((suffix != "rel-memory" && suffix != "rel-depth") || read("auto-rel") >= 0.005f)
                && (envelopeActive || std::abs(read("tension")) > 0.0f)
                && ((peakActive && (read("processor-release-peak-mode") >= 0.5f || read("processor-release-peak") > 0.0f))
                    || (rmsActive && (read("processor-release-rms-mode") >= 0.5f || read("processor-release-rms") > 0.0f))));
    }
    if (metadata.modeSuffix[0] != '\0')
    {
        const auto compressor = read("domain") >= 0.5f || read("morph") > 0.0f;
        return finish(transientSustain || compressor || (suffix != "processor-attack" && std::abs(read("tension")) > 0.0f));
    }
    static const juce::StringArray morphDependencies { "feedback", "clip-knee", "ratio", "comp-knee", "corr-knee", "attack-curve" };
    const auto morphDependent = morphDependencies.contains(suffix);
    if (morphDependent && !spectral && !transientSustain) return finish(suffix == "clip-knee" ? read("morph") < 100.0f : read("morph") > 0.0f);
    if (suffix == "threshold" || suffix == "corr-threshold") return finish(read("adaptive") < 100.0f);
    if (suffix == "range-curve") return finish(read("range") > 0.0f);
    if (suffix == "adap-time" || suffix == "offset" || suffix == "corr-offset") return finish(read("adaptive") > 0.0f);
    return result;
}
}
