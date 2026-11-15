#include "../shared/TimeUnits.h"
#pragma once

#include <JuceHeader.h>
#include "../shared/ParameterLayout.h"
#include "../shared/DspUtilities.h"
#include "../shared/ChannelRouting.h"

#include <array>
#include <cstddef>

namespace dyn::parameters
{

enum class ParameterType
{
    floating,
    boolean,
    choice,
};

enum class ParameterSlot : size_t
{
    morph,
    ratio,
    // Retain legacy host parameter positions/IDs to avoid breaking old REAPER
    // envelopes. These parameters are no longer visible or used by DSP.
    legacyProgression,
    legacyProgressionSpan,
    legacyCorrProgressionSpan,
    compKnee,
    lookahead,
    tensionFloor,
    tensionHysteresis,
    releaseCurve,
    adaptiveOffset,
    threshold,
    thresholdHysteresis,
    adaptive,
    tension,
    autoRelease,
    releaseMemory,
    releaseDepth,
    processorReleasePeak,
    processorReleaseRms,
    makeUp,
    delta,
    processorHold,
    processorAttack,
    attackCurve,
    inputGain,
    adaptiveTime,
    mix,
    output,
    range,
    sidechainChannel,
    sidechainListen,
    peakRms,
    peakCrest,
    rmsTime,
    rangeCurve,
    attackMode,
    attackNote,
    holdMode,
    holdNote,
    releasePeakMode,
    releasePeakNote,
    releaseRmsMode,
    releaseRmsNote,
    feedback,
    clipKnee,
    domain,
    fftSize,
    fftOverlap,
    spectralSlope,
    mode,
    transientGain,
    sustainGain,
    transientMute,
    sustainMute,
    retrigger,
    oneShot,
    corrThreshold,
    corrThresholdHysteresis,
    corrKnee,
    corrSlope,
    corrOffset,
    corrSmoothing,
    corrImpact,
    corrTensionFloor,
    corrPeakCrest,
    lookaheadCurve,
    clipEql,
    count
};

inline constexpr std::array linkedParameterSlots {
    ParameterSlot::morph,
    ParameterSlot::ratio,
    ParameterSlot::compKnee,
    ParameterSlot::thresholdHysteresis,
    ParameterSlot::corrThresholdHysteresis,
    ParameterSlot::clipKnee,
    ParameterSlot::lookahead,
    ParameterSlot::lookaheadCurve,
    ParameterSlot::tensionFloor,
    ParameterSlot::corrTensionFloor,
    ParameterSlot::tensionHysteresis,
    ParameterSlot::releaseCurve,
    ParameterSlot::autoRelease,
    ParameterSlot::releaseMemory,
    ParameterSlot::releaseDepth,
    ParameterSlot::attackCurve,
    ParameterSlot::adaptiveTime,
    ParameterSlot::adaptiveOffset
};

inline constexpr size_t numParameterSlots = static_cast<size_t>(ParameterSlot::count);

using ava::modules::parameters::ControlMetadata;
using ava::modules::parameters::ControlPolicy;

struct ParameterSpec
{
    const char* suffix = "";
    ParameterType type = ParameterType::floating;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.01f;
    float defaultValue = 0.0f;
    const char* label = "";
    ControlMetadata control {};
};

inline constexpr auto parameterSpecs = std::to_array<ParameterSpec>({
    { "morph", ParameterType::floating, 0.0f, 100.0f, 0.01f, 100.0f, "" },
    { "ratio", ParameterType::floating, 1.0f, 100.0f, 0.01f, 100.0f, ":1" },
    // Legacy placeholders: preserving these IDs keeps existing host automation
    // indices stable. Old values are ignored by the DYN audio processors.
    { "progression", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "" },
    { "prog-span", ParameterType::floating, 0.1f, 96.0f, 0.01f, 12.0f, "dB" },
    { "corr-prog-span", ParameterType::floating, 0.01f, 2.0f, 0.01f, 0.25f, "" },
    { "comp-knee", ParameterType::floating, 0.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "lookahead", ParameterType::floating, 0.0f, 30.0f, 0.01f, 0.0f, "ms" },
    { "tension-floor", ParameterType::floating, -96.0f, 0.0f, 0.01f, -96.0f, "dB" },
    { "tension-hyst", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "" },
    { "release-curve", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "" },
    { "offset", ParameterType::floating, -48.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "threshold", ParameterType::floating, -96.0f, 12.0f, 0.01f, 0.0f, "dB" },
    { "thresh-hyst", ParameterType::floating, 0.0f, 24.0f, 0.01f, 0.0f, "dB" },
    { "adaptive", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "" },
    { "tension", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "" },
    { "auto-rel", ParameterType::floating, 0.0f, 400.0f, 0.01f, 0.0f, "" },
    { "rel-memory", ParameterType::floating, 10.0f, 2000.0f, 0.01f, 200.0f, "ms" },
    { "rel-depth", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "" },
    { "processor-release-peak", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 10.0f, "ms", { "", "", false, true, "processor-release-peak-mode", "processor-release-peak-note" } },
    { "processor-release-rms", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 150.0f, "ms", { "", "", false, true, "processor-release-rms-mode", "processor-release-rms-note" } },
    { "make-up", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "delta", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "processor-hold", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 0.0f, "ms", { "", "", false, true, "processor-hold-mode", "processor-hold-note" } },
    { "processor-attack", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 0.0f, "ms", { "", "", false, true, "processor-attack-mode", "processor-attack-note" } },
    { "attack-curve", ParameterType::floating, -1000.0f, 1000.0f, 0.01f, 0.0f, "" },
    { "input", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "adap-time", ParameterType::floating, 50.0f, 10000.0f, 1.0f, 50.0f, "ms" },
    { "mix", ParameterType::floating, 0.0f, 100.0f, 0.01f, 100.0f, "" },
    { "output", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB", { "delta", "DT" } },
    { "range", ParameterType::floating, 0.0f, 96.0f, 0.01f, 96.0f, "dB" },
    { "sc-channel", ParameterType::choice, 0.0f, float(ava::channels::choiceCount - 1), 1.0f, 0.0f, "", { "sc-listen", "LSN" } },
    { "sc-listen", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "peak-rms", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "" },
    { "peak-crest", ParameterType::floating, -24.0f, 24.0f, 0.01f, 0.0f, "dB" },
    { "rms-time", ParameterType::floating, 1.0f, 1000.0f, 0.01f, 50.0f, "ms" },
    { "range-curve", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "" },
    { "processor-attack-mode", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "processor-attack-note", ParameterType::choice, 0.0f, 11.0f, 1.0f, 3.0f, "" },
    { "processor-hold-mode", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "processor-hold-note", ParameterType::choice, 0.0f, 11.0f, 1.0f, 5.0f, "" },
    { "processor-release-peak-mode", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "processor-release-peak-note", ParameterType::choice, 0.0f, 11.0f, 1.0f, 7.0f, "" },
    { "processor-release-rms-mode", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "processor-release-rms-note", ParameterType::choice, 0.0f, 11.0f, 1.0f, 7.0f, "" },
    { "feedback", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "" },
    { "clip-knee", ParameterType::floating, 0.0f, 96.0f, 0.01f, 0.0f, "dB", { "clip-qual", "EQL" } },
    { "domain", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "", { "", "", false, false } },
    { "fft-size", ParameterType::choice, 0.0f, 4.0f, 1.0f, 2.0f, "" },
    { "fft-overlap", ParameterType::choice, 0.0f, 2.0f, 1.0f, 2.0f, "" },
    { "spectral-slope", ParameterType::floating, -9.0f, 9.0f, 0.01f, 4.5f, "dB/oct" },
    { "mode", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "", { "", "", false, false } },
    { "trans-gain", ParameterType::floating, float(ava::modules::dsp::gainMinimumDb), float(ava::modules::dsp::gainMaximumDb), 0.01f, 0.0f, "dB", { "mute-trans", "MT", true } },
    { "sust-gain", ParameterType::floating, float(ava::modules::dsp::gainMinimumDb), float(ava::modules::dsp::gainMaximumDb), 0.01f, 0.0f, "dB", { "mute-sust", "MT", true } },
    { "mute-trans", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "mute-sust", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "retrigger", ParameterType::floating, 1.0f, 5000.0f, 0.01f, 60.0f, "ms", { "one-shot", "ONE" } },
    { "one-shot", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "corr-threshold", ParameterType::floating, -1.0f, 1.0f, 0.01f, 0.0f, "" },
    { "corr-thresh-hyst", ParameterType::floating, 0.0f, 2.0f, 0.01f, 0.0f, "" },
    { "corr-knee", ParameterType::floating, 0.0f, 2.0f, 0.01f, 0.0f, "" },
    { "corr-slope", ParameterType::floating, -0.09f, 0.09f, 0.001f, 0.0f, "/oct" },
    { "corr-offset", ParameterType::floating, -1.0f, 1.0f, 0.01f, 0.0f, "" },
    { "corr-smoothing", ParameterType::floating, 0.0f, 100.0f, 1.0f, 30.0f, "" },
    { "corr-impact", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "" },
    { "corr-tension-floor", ParameterType::floating, -1.0f, 1.0f, 0.01f, -1.0f, "" },
    { "corr-peak-crest", ParameterType::floating, -2.0f, 2.0f, 0.01f, 0.0f, "" },
    { "lookahead-curve", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "" },
    { "clip-qual", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
});

static_assert(parameterSpecs.size() == numParameterSlots);

constexpr size_t toIndex(const ParameterSlot slot)
{
    return static_cast<size_t>(slot);
}

constexpr bool hasBypass(const ParameterSpec& spec) noexcept
{
    return spec.type == ParameterType::floating;
}

inline juce::String bypassSuffix(const ParameterSpec& spec)
{
    return juce::String(spec.suffix) + ".bypass";
}

// Bypass changes the effective DSP value, never the stored control value.
constexpr float bypassValue(const ParameterSlot slot) noexcept
{
    if (slot == ParameterSlot::ratio || slot == ParameterSlot::rmsTime
        || slot == ParameterSlot::retrigger) return 1.0f;
    if (slot == ParameterSlot::adaptiveTime) return 50.0f;
    if (slot == ParameterSlot::threshold) return 12.0f;
    if (slot == ParameterSlot::corrThreshold) return 1.0f;
    if (slot == ParameterSlot::releaseMemory) return 10.0f;
    if (slot == ParameterSlot::tensionFloor) return -96.0f;
    if (slot == ParameterSlot::corrTensionFloor) return -1.0f;
    if (slot == ParameterSlot::range) return 96.0f;
    if (slot == ParameterSlot::morph || slot == ParameterSlot::mix) return 100.0f;
    // Legacy span parameter IDs must still pass the bypass range assertion.
    if (slot == ParameterSlot::legacyProgressionSpan || slot == ParameterSlot::legacyCorrProgressionSpan)
        return parameterSpecs[toIndex(slot)].defaultValue;
    return 0.0f;
}

static_assert([]
{
    for (size_t index = 0; index < parameterSpecs.size(); ++index)
    {
        const auto& spec = parameterSpecs[index];
        if (!hasBypass(spec)) continue;
        const auto value = bypassValue(static_cast<ParameterSlot>(index));
        if (value < spec.min || value > spec.max) return false;
    }
    for (const auto slot : linkedParameterSlots)
        if (!hasBypass(parameterSpecs[toIndex(slot)])) return false;
    return true;
}());

ControlPolicy getControlPolicy(const juce::AudioProcessorValueTreeState& state, size_t range, const juce::String& suffix);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
