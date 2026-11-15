#pragma once

#include <JuceHeader.h>

#include <optional>
#include <array>
#include <utility>
#include <vector>

inline bool parseOscBandParameterId(const juce::String& id, size_t& bandIndex, juce::String& suffix)
{
    if (!id.startsWith("band-")) return false;
    const auto separator = id.indexOfChar('_');
    if (separator <= 5) return false;
    const auto number = id.substring(5, separator);
    const auto band = number.getIntValue();
    if (band < 1 || band > 6 || number != juce::String(band)) return false;
    bandIndex = size_t(band - 1);
    suffix = id.substring(separator + 1);
    return suffix.isNotEmpty();
}

struct OscParameterInfo
{
    juce::String sourceId;
    float valueScale = 1.0f;
    std::optional<juce::Range<float>> parameterRange {};

    bool operator==(const OscParameterInfo& other) const
    {
        return sourceId == other.sourceId
            && juce::exactlyEqual(valueScale, other.valueScale) && parameterRange == other.parameterRange;
    }
};

inline juce::String getOscAddressName(const juce::String& sourceId)
{
    auto name = sourceId;
    if (name == "process-input") name = "ch-in";
    else if (name == "bp") name = "bypass";
    else if (name.endsWith("_bp")) name = name.dropLastCharacters(3) + "_bypass";

    const auto bandSeparator = name.startsWith("band-") ? name.indexOfChar('_') : -1;
    if (bandSeparator >= 0)
    {
        const auto prefix = name.substring(0, bandSeparator + 1);
        auto suffix = name.substring(bandSeparator + 1);
        // CORR addresses must stay distinct from FREQ: the controls use different scales.
        static constexpr auto names = std::to_array<std::pair<const char*, const char*>>({
        { "stereo.gain", "gain-stereo" },
        { "left.gain", "gain-left" },
        { "right.gain", "gain-right" },
        { "mid.gain", "gain-mid" },
        { "side.gain", "gain-side" },
        { "stereo.mute", "gain-stereo-mute" },
        { "left.mute", "gain-left-mute" },
        { "right.mute", "gain-right-mute" },
        { "mid.mute", "gain-mid-mute" },
        { "side.mute", "gain-side-mute" },
        { "stereo.delay", "delay-stereo" },
        { "left.delay", "delay-left" },
        { "right.delay", "delay-right" },
        { "stereo.phase", "phase-stereo" },
        { "left.phase", "phase-left" },
        { "right.phase", "phase-right" },
        { "comb.count", "comb-count" },
        { "comb.shift", "comb-shift" },
        { "comb.shift.bypass", "comb-shift-bypass" },
        { "comb.count.bypass", "comb-count-bypass" },
        { "listen.bypass", "listen-bypass" },
        { "gain.bypass", "gain-bypass" },
        { "delay.bypass", "delay-bypass" },
        { "phase.bypass", "phase-bypass" },
        { "panorama.bypass", "panorama-bypass" },
        { "shear.bypass", "shear-bypass" },
        { "balance.bypass", "ms-balance-bypass" },
        { "ortho.bypass", "ortho-bypass" },
        { "rect.bypass", "rect-bypass" },
        { "stereo.gain.bypass", "gain-stereo-bypass" },
        { "left.gain.bypass", "gain-left-bypass" },
        { "right.gain.bypass", "gain-right-bypass" },
        { "mid.gain.bypass", "gain-mid-bypass" },
        { "side.gain.bypass", "gain-side-bypass" },
        { "listen.hidden", "listen-hidden" },
        { "gain.hidden", "gain-hidden" },
        { "delay.hidden", "delay-hidden" },
        { "phase.hidden", "phase-hidden" },
        { "panorama.hidden", "panorama-hidden" },
        { "shear.hidden", "shear-hidden" },
        { "balance.hidden", "ms-balance-hidden" },
        { "ortho.hidden", "ortho-hidden" },
        { "rect.hidden", "rect-hidden" },
        { "comb.hidden", "comb-hidden" },
        { "left.pan", "panorama-left" },
        { "right.pan", "panorama-right" },
        { "law", "panorama-law" },
        { "impact", "shear-impact" },
        { "direction", "shear-direction" },
        { "mid.balance", "ms-balance-mid" },
        { "side.balance", "ms-balance-side" },
        { "degree", "ortho-degree" },
        { "hpos", "rect-half-pos" },
        { "hneg", "rect-half-neg" },
        { "fpos", "rect-full-pos" },
        { "fneg", "rect-full-neg" },
        { "fft-size", "win-size" },
        { "fft-overlap", "overlap" },
        { "spectral-slope", "slope" },
        { "corr-knee", "corr-comp-knee" },
        { "corr-offset", "corr-adap-offset" },
        { "corr-smoothing", "smoothing" },
        { "corr-impact", "impact" },
        { "adaptive", "threshold-adap" },
        { "offset", "adap-offset" },
        { "processor-attack", "attack" },
        { "processor-hold", "hold" },
        { "processor-release-peak", "release-peak" },
        { "processor-release-rms", "release-rms" },
        { "processor-attack-mode", "attack-mode" },
        { "processor-attack-note", "attack-note" },
        { "processor-hold-mode", "hold-mode" },
        { "processor-hold-note", "hold-note" },
        { "processor-release-peak-mode", "release-peak-mode" },
        { "processor-release-rms-mode", "release-rms-mode" },
        { "processor-release-peak-note", "release-peak-note" },
        { "processor-release-rms-note", "release-rms-note" },
        { "feedback", "fwd-fb" },
        { "mute-trans", "trans-mute" },
        { "mute-sust", "sust-mute" },
        });
        for (const auto& [id, label] : names)
            if (suffix == id)
            {
                suffix = label;
                break;
            }
        name = prefix + suffix;
    }
    return name.replaceCharacter('.', '-');
}

inline juce::String getOscDisplayName(const juce::String& sourceId)
{
    const auto name = getOscAddressName(sourceId).toUpperCase();
    return sourceId.endsWith(".hidden") ? name + " (VISIBILITY)" : name;
}

inline juce::String getOscSourceId(const juce::String& addressName,
                                   const std::vector<OscParameterInfo>& parameters)
{
    for (const auto& parameter : parameters)
        if (getOscAddressName(parameter.sourceId) == addressName)
            return parameter.sourceId;

    return {};
}

struct OscSettings
{
    static constexpr int defaultInputPort = 9000;
    static constexpr int defaultOutputPort = 9001;

    bool enabled = false;
    int inputPort = defaultInputPort;
    juce::String outputHost { "127.0.0.1" };
    int outputPort = defaultOutputPort;

    bool operator==(const OscSettings&) const = default;
};

inline bool isValidOscInstanceName(const juce::String& value)
{
    return value.isNotEmpty() && value.length() <= 48
        && value.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
}

inline juce::String makeOscAddressPrefix(const juce::String& instanceName)
{
    return "/ava/" + instanceName.toLowerCase() + "/";
}

inline juce::String makeAssignedOscAddress(const juce::String& instanceName, const juce::String& sourceId)
{
    return makeOscAddressPrefix(instanceName) + getOscAddressName(sourceId);
}

inline juce::String makeAssignedOscAddress(const juce::String& instanceName, const OscParameterInfo& parameter)
{
    return makeAssignedOscAddress(instanceName, parameter.sourceId);
}
