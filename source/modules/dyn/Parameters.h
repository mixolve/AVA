#pragma once

#include <JuceHeader.h>
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
    progression,
    progressionSpan,
    corrProgressionSpan,
    compKnee,
    lookahead,
    tensionFloor,
    tensionHysteresis,
    releaseCurve,
    adaptiveOffset,
    adaptiveAttack,
    adaptiveHold,
    adaptiveRelease,
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
    processorDelay,
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
    releaseType,
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
    count
};

inline constexpr std::array linkedParameterSlots {
    ParameterSlot::morph,
    ParameterSlot::ratio,
    ParameterSlot::progression,
    ParameterSlot::progressionSpan,
    ParameterSlot::corrProgressionSpan,
    ParameterSlot::compKnee,
    ParameterSlot::thresholdHysteresis,
    ParameterSlot::corrThresholdHysteresis,
    ParameterSlot::clipKnee,
    ParameterSlot::lookahead,
    ParameterSlot::tensionFloor,
    ParameterSlot::corrTensionFloor,
    ParameterSlot::tensionHysteresis,
    ParameterSlot::releaseCurve,
    ParameterSlot::autoRelease,
    ParameterSlot::releaseMemory,
    ParameterSlot::releaseDepth,
    ParameterSlot::attackCurve,
    ParameterSlot::adaptiveTime,
    ParameterSlot::adaptiveOffset,
    ParameterSlot::adaptiveAttack,
    ParameterSlot::adaptiveHold,
    ParameterSlot::adaptiveRelease
};

inline constexpr size_t numParameterSlots = static_cast<size_t>(ParameterSlot::count);

struct ParameterSpec
{
    const char* suffix = "";
    ParameterType type = ParameterType::floating;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.01f;
    float defaultValue = 0.0f;
    const char* label = "";
};

inline constexpr auto parameterSpecs = std::to_array<ParameterSpec>({
    { "morph", ParameterType::floating, 0.0f, 100.0f, 0.01f, 100.0f, "%" },
    { "ratio", ParameterType::floating, 1.0f, 100.0f, 0.01f, 100.0f, ":1" },
    { "progression", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "prog-span", ParameterType::floating, 0.1f, 96.0f, 0.01f, 12.0f, "dB" },
    { "corr-prog-span", ParameterType::floating, 0.01f, 2.0f, 0.01f, 0.25f, "" },
    { "comp-knee", ParameterType::floating, 0.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "lookahead", ParameterType::floating, 0.0f, 30.0f, 0.01f, 0.0f, "ms" },
    { "tension-floor", ParameterType::floating, -96.0f, 0.0f, 0.01f, -96.0f, "dB" },
    { "tension-hyst", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "release-curve", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "offset", ParameterType::floating, -48.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "attack", ParameterType::floating, 0.0f, 200.0f, 1.0f, 30.0f, "ms" },
    { "hold", ParameterType::floating, 0.0f, 2000.0f, 1.0f, 0.0f, "ms" },
    { "release", ParameterType::floating, 0.0f, 2000.0f, 1.0f, 300.0f, "ms" },
    { "threshold", ParameterType::floating, -96.0f, 12.0f, 0.01f, 0.0f, "dB" },
    { "thresh-hyst", ParameterType::floating, 0.0f, 24.0f, 0.01f, 0.0f, "dB" },
    { "adaptive", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "tension", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "auto-rel", ParameterType::floating, 0.0f, 400.0f, 0.01f, 0.0f, "%" },
    { "rel-memory", ParameterType::floating, 10.0f, 2000.0f, 0.01f, 200.0f, "ms" },
    { "rel-depth", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "processor-release-peak", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 0.0f, "ms" },
    { "processor-release-rms", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 150.0f, "ms" },
    { "make-up", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "delta", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "processor-hold", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 0.0f, "ms" },
    { "processor-attack", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 0.0f, "ms" },
    { "attack-curve", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "processor-delay", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 0.0f, "ms" },
    { "input", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "adap-time", ParameterType::floating, 1.0f, 1000.0f, 1.0f, 50.0f, "ms" },
    { "mix", ParameterType::floating, 0.0f, 100.0f, 0.01f, 100.0f, "%" },
    { "output", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "range", ParameterType::floating, 0.0f, 96.0f, 0.01f, 96.0f, "dB" },
    { "sc-channel", ParameterType::choice, 0.0f, float(ava::channels::choiceCount - 1), 1.0f, 0.0f, "" },
    { "sc-listen", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "peak-rms", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "peak-crest", ParameterType::floating, -24.0f, 24.0f, 0.01f, 0.0f, "dB" },
    { "rms-time", ParameterType::floating, 1.0f, 1000.0f, 1.0f, 50.0f, "ms" },
    { "release-type", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "range-curve", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "processor-attack-mode", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "processor-attack-note", ParameterType::choice, 0.0f, 11.0f, 1.0f, 3.0f, "" },
    { "processor-hold-mode", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "processor-hold-note", ParameterType::choice, 0.0f, 11.0f, 1.0f, 5.0f, "" },
    { "processor-release-peak-mode", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "processor-release-peak-note", ParameterType::choice, 0.0f, 11.0f, 1.0f, 7.0f, "" },
    { "processor-release-rms-mode", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "processor-release-rms-note", ParameterType::choice, 0.0f, 11.0f, 1.0f, 7.0f, "" },
    { "feedback", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "clip-knee", ParameterType::floating, 0.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "domain", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "fft-size", ParameterType::choice, 0.0f, 4.0f, 1.0f, 2.0f, "" },
    { "fft-overlap", ParameterType::choice, 0.0f, 2.0f, 1.0f, 2.0f, "" },
    { "spectral-slope", ParameterType::floating, -9.0f, 9.0f, 0.01f, 4.5f, "dB/oct" },
    { "mode", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "trans-gain", ParameterType::floating, -48.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "sust-gain", ParameterType::floating, -48.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "mute-trans", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "mute-sust", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "retrigger", ParameterType::floating, 1.0f, 5000.0f, 0.01f, 60.0f, "ms" },
    { "one-shot", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "corr-threshold", ParameterType::floating, -1.0f, 1.0f, 0.01f, 0.0f, "" },
    { "corr-thresh-hyst", ParameterType::floating, 0.0f, 2.0f, 0.01f, 0.0f, "" },
    { "corr-knee", ParameterType::floating, 0.0f, 2.0f, 0.01f, 0.0f, "" },
    { "corr-slope", ParameterType::floating, -0.09f, 0.09f, 0.001f, 0.0f, "/oct" },
    { "corr-offset", ParameterType::floating, -1.0f, 1.0f, 0.01f, 0.0f, "" },
    { "corr-smoothing", ParameterType::floating, 0.0f, 100.0f, 1.0f, 30.0f, "%" },
    { "corr-impact", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "corr-tension-floor", ParameterType::floating, -1.0f, 1.0f, 0.01f, -1.0f, "" },
    { "corr-peak-crest", ParameterType::floating, -2.0f, 2.0f, 0.01f, 0.0f, "" },
});

static_assert(parameterSpecs.size() == numParameterSlots);

constexpr size_t toIndex(const ParameterSlot slot)
{
    return static_cast<size_t>(slot);
}

inline constexpr std::array<double, 12> timeNoteWholeFractions {
    1.0 / 512, 1.0 / 256, 1.0 / 128, 1.0 / 64, 1.0 / 32, 1.0 / 16,
    1.0 / 8, 1.0 / 4, 1.0 / 2, 1.0, 2.0, 4.0
};
inline juce::StringArray getTimeNoteChoices()
{
    return { "1/512", "1/256", "1/128", "1/64", "1/32", "1/16",
             "1/8", "1/4", "1/2", "1/1", "2/1", "4/1" };
}
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
