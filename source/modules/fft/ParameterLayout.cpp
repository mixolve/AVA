#include "Processor.h"
#include "Constants.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace
{
struct ParameterOrderEntry
{
    const char* key;
    const char* label;
};

inline constexpr auto fftDeltaOrder = std::to_array<ParameterOrderEntry>({
    { "delta", "DELTA" },
});

inline constexpr auto fftMainOrder = std::to_array<ParameterOrderEntry>({
    { "dynamic_mode", "MODE" },
    { "correlation_type", "TYPE" },
    { "dynamic_direction", "DIRECTION" },
    { "attack", "ATTACK" },
    { "release", "RELEASE" },
    { "knee", "KNEE" },
    { "ratio", "RATIO" },
    { "floor", "FLOOR" },
    { "window_size", "WIN-SIZE" },
    { "overlap", "OVERLAP" },
    { "slope", "SLOPE" },
    { "l_threshold", "L-THRESHOLD" },
    { "l_adaptive", "L-ADAPTIVE" },
    { "r_threshold", "R-THRESHOLD" },
    { "r_adaptive", "R-ADAPTIVE" },
    { "link_lr", "LINK-LR (STEREO)" },
    { "dynamic_bypass", "BYPASS" },
});

juce::String formatDecibelValue(const float value)
{
    return juce::String::formatted("%.0f dB", static_cast<double>(value));
}

juce::String formatFloorValue(const float value)
{
    return value <= -99.995f
        ? "FULL"
        : juce::String::formatted("%.2f dB", static_cast<double>(value));
}

juce::String formatSlopeValue(const float value)
{
    return juce::String::formatted("%.2f dB/oct", static_cast<double>(value));
}

juce::String formatRatioValue(const float value)
{
    return juce::String::formatted("%.2f:1", static_cast<double>(value));
}

juce::String formatTimeValue(const float value)
{
    return juce::String::formatted("%.0f ms", static_cast<double>(value));
}

juce::String formatCorrelationValue(const float value)
{
    return juce::String::formatted("%.2f", static_cast<double>(value));
}

juce::String formatImpactValue(const float value)
{
    if (value <= -99.995f)
        return "LEFT";
    if (value >= 99.995f)
        return "RIGHT";
    if (std::abs(value) <= 0.005f)
        return "BOTH";

    return formatCorrelationValue(value);
}


using ParameterList = std::vector<std::unique_ptr<juce::RangedAudioParameter>>;

juce::String makeFftName(const juce::String& blockName, const juce::String& parameterName)
{
    if (blockName.isEmpty())
        return "FFT / " + parameterName;

    return "FFT / " + blockName + " / " + parameterName;
}

void appendDeltaParameters(ParameterList& parameterLayout)
{
    for (const auto& entry : fftDeltaOrder)
    {
        const auto key = juce::String(entry.key);
        if (key == "delta")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID { FftModuleProcessor::paramDeltaId, 1 },
                makeFftName({}, entry.label),
                false,
                juce::AudioParameterBoolAttributes()));
        }
    }


}

void appendMainParameters(ParameterList& parameterLayout)
{
    for (const auto& entry : fftMainOrder)
    {
        const auto key = juce::String(entry.key);
        const auto isFftParameter = key == "window_size" || key == "overlap";
        const auto name = makeFftName(isFftParameter ? "MAIN" : "PROCESSOR", entry.label);

        if (key == "dynamic_mode")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID { FftModuleProcessor::paramDynamicModeId, 1 },
                name,
                juce::StringArray { "SPECTRAL", "CORR" },
                0,
                juce::AudioParameterChoiceAttributes()));
            continue;
        }

        if (key == "correlation_type")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID { FftModuleProcessor::paramCorrelationTypeId, 1 },
                name,
                juce::StringArray { "PHASE", "FREQ", "SIGNED" },
                0,
                juce::AudioParameterChoiceAttributes()));
            continue;
        }

        if (key == "dynamic_direction")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID { FftModuleProcessor::paramDynamicDirectionId, 1 },
                name,
                juce::StringArray { "DOWNWARD", "UPWARD" },
                0,
                juce::AudioParameterChoiceAttributes()));
            continue;
        }

        if (key == "attack")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { FftModuleProcessor::paramAttackId, 1 },
                name,
                juce::NormalisableRange<float> { 0.0f, 200.0f, 1.0f },
                0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                    [] (float value, int)
                    {
                        return formatTimeValue(value);
                    })));
            continue;
        }

        if (key == "release")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { FftModuleProcessor::paramReleaseId, 1 },
                name,
                juce::NormalisableRange<float> { 0.0f, 2000.0f, 1.0f },
                0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                    [] (float value, int)
                    {
                        return formatTimeValue(value);
                    })));
            continue;
        }

        if (key == "knee")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { FftModuleProcessor::paramKneeId, 1 },
                name,
                juce::NormalisableRange<float> { 0.0f, 24.0f, 0.01f },
                0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                    [] (float value, int)
                    {
                        return formatDecibelValue(value);
                    })));
            continue;
        }

        if (key == "ratio")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { FftModuleProcessor::paramRatioId, 1 },
                name,
                juce::NormalisableRange<float> { 1.0f, 100.0f, 0.01f },
                100.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                    [] (float value, int)
                    {
                        return formatRatioValue(value);
                    })));
            continue;
        }

        if (key == "floor")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { FftModuleProcessor::paramFloorId, 1 },
                name,
                juce::NormalisableRange<float> { -100.0f, 0.0f, 0.01f },
                -100.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                    [] (float value, int)
                    {
                        return formatFloorValue(value);
                    })));
            continue;
        }

        if (key == "window_size")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID { FftModuleProcessor::paramDspFftSizeId, 1 },
                name,
                juce::StringArray { "1024", "2048", "4096", "8192", "16384" },
                2,
                juce::AudioParameterChoiceAttributes()));
            continue;
        }

        if (key == "overlap")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID { FftModuleProcessor::paramDspOverlapId, 1 },
                name,
                juce::StringArray { "2", "4", "8", "16", "32" },
                4,
                juce::AudioParameterChoiceAttributes()));
            continue;
        }

        if (key == "slope")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { FftModuleProcessor::paramDspSlopeId, 1 },
                name,
                juce::NormalisableRange<float> { -9.0f, 9.0f, 0.01f },
                4.5f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                    [] (float value, int)
                    {
                        return formatSlopeValue(value);
                    })));
            continue;
        }

        if (key == "l_threshold")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { FftModuleProcessor::paramDualMonoLeftThresholdId, 1 },
                name,
                juce::NormalisableRange<float> { -99.0f, 12.0f, 0.01f },
                0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                    [] (float value, int)
                    {
                        return formatDecibelValue(value);
                    })));
            continue;
        }

        if (key == "l_adaptive")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { FftModuleProcessor::paramDualMonoLeftAdaptiveId, 1 },
                name,
                juce::NormalisableRange<float> { 0.0f, 100.0f, 1.0f },
                0.0f,
                juce::AudioParameterFloatAttributes()));
            continue;
        }

        if (key == "r_threshold")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { FftModuleProcessor::paramDualMonoRightThresholdId, 1 },
                name,
                juce::NormalisableRange<float> { -99.0f, 12.0f, 0.01f },
                0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                    [] (float value, int)
                    {
                        return formatDecibelValue(value);
                    })));
            continue;
        }

        if (key == "r_adaptive")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID { FftModuleProcessor::paramDualMonoRightAdaptiveId, 1 },
                name,
                juce::NormalisableRange<float> { 0.0f, 100.0f, 1.0f },
                0.0f,
                juce::AudioParameterFloatAttributes()));
            continue;
        }

        if (key == "link_lr")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID { FftModuleProcessor::paramDualMonoLinkId, 1 },
                name,
                true,
                juce::AudioParameterBoolAttributes()));
            continue;
        }

        if (key == "dynamic_bypass")
        {
            parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID { FftModuleProcessor::paramDynamicBypassId, 1 },
                name,
                false,
                juce::AudioParameterBoolAttributes()));
            continue;
        }

    }


}

void appendAuxiliaryParameters(ParameterList& parameterLayout)
{
    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { FftModuleProcessor::paramSpectralAdaptiveOffsetId, 1 },
        makeFftName("ADAPTIVE", "OFFSET"),
        juce::NormalisableRange<float> { 0.0f, 48.0f, 0.01f },
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatDecibelValue(value);
            })));

    const auto addAdaptiveTimeParameter = [&parameterLayout] (const char* parameterId,
                                                                            const char* parameterName,
                                                                            const float maximum,
                                                                            const float defaultValue)
    {
        parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { parameterId, 1 },
            makeFftName("ADAPTIVE", parameterName),
            juce::NormalisableRange<float> { 0.0f, maximum, 1.0f },
            defaultValue,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [] (float value, int)
                {
                    return formatTimeValue(value);
                })));
    };

    addAdaptiveTimeParameter(FftModuleProcessor::paramAdaptiveAttackId, "ATTACK", 200.0f, 30.0f);
    addAdaptiveTimeParameter(FftModuleProcessor::paramAdaptiveHoldId, "HOLD", 2000.0f, 0.0f);
    addAdaptiveTimeParameter(FftModuleProcessor::paramAdaptiveReleaseId, "RELEASE", 2000.0f, 300.0f);

    const auto addCorrelationParameter = [&parameterLayout] (const char* parameterId,
                                                                     const char* parameterName,
                                                                     const float minimum,
                                                                     const float maximum,
                                                                     const float defaultValue,
                                                                     const bool reversed = false,
                                                                     const float interval = 0.01f,
                                                                     const char* blockName = "PROCESSOR")
    {
        auto range = juce::NormalisableRange<float> { minimum, maximum, interval };

        if (reversed)
        {
            range = juce::NormalisableRange<float> {
                minimum,
                maximum,
                [] (const float start, const float end, const float normalised)
                {
                    return end - (normalised * (end - start));
                },
                [] (const float start, const float end, const float value)
                {
                    return (end - juce::jlimit(start, end, value)) / (end - start);
                },
                [] (const float start, const float end, const float value)
                {
                    const auto clamped = juce::jlimit(start, end, value);
                    return std::round(clamped * 100.0f) * 0.01f;
                }
            };
            range.interval = 0.01f;
        }

        parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { parameterId, 1 },
            makeFftName(blockName, parameterName),
            range,
            defaultValue,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [] (float value, int)
                {
                    return formatCorrelationValue(value);
                })));
    };

    addCorrelationParameter(FftModuleProcessor::paramCorrelationThresholdId, "THRESHOLD", 0.0f, 100.0f, 0.0f, true);
    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { FftModuleProcessor::paramCorrelationSmoothingId, 1 },
        makeFftName("PROCESSOR", "SMOOTHING"),
        juce::NormalisableRange<float> { 0.0f, 100.0f, 1.0f },
        30.0f,
        juce::AudioParameterFloatAttributes()));
    addCorrelationParameter(FftModuleProcessor::paramCorrelationAdaptiveId, "ADAPTIVE", 0.0f, 100.0f, 0.0f, false, 1.0f);
    addCorrelationParameter(FftModuleProcessor::paramCorrelationAdaptiveOffsetId, "OFFSET", -1.0f, 1.0f, 0.0f, false, 0.01f, "ADAPTIVE");
    const auto addDetectorCut = [&parameterLayout] (const char* id,
                                                               const char* name,
                                                               const float defaultFrequency)
    {
        auto range = juce::NormalisableRange<float> { analyserMinFrequency, analyserMaxFrequency, 0.01f };
        range.setSkewForCentre(1000.0f);
        parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 },
            makeFftName("RANGE", name),
            range,
            defaultFrequency,
            juce::AudioParameterFloatAttributes().withLabel("Hz")
                .withStringFromValueFunction([] (float value, int)
                {
                    return juce::String::formatted("%.2f", static_cast<double>(value));
                })));
    };
    addDetectorCut(FftModuleProcessor::paramDetectorLowCutId, "LOW-CUT", analyserMinFrequency);
    addDetectorCut(FftModuleProcessor::paramDetectorHighCutId, "HIGH-CUT", analyserMaxFrequency);

    addCorrelationParameter(FftModuleProcessor::paramCorrelationSlopeId, "SLOPE", -9.0f, 9.0f, 0.0f);
    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { FftModuleProcessor::paramCorrelationImpactId, 1 },
        makeFftName("PROCESSOR", "IMPACT"),
        juce::NormalisableRange<float> { -100.0f, 100.0f, 0.01f },
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([] (float value, int)
            {
                return formatImpactValue(value);
            })
            .withValueFromStringFunction([] (const juce::String& text)
            {
                const auto valueText = text.trim();

                if (valueText.equalsIgnoreCase("LEFT"))
                    return -100.0f;
                if (valueText.equalsIgnoreCase("BOTH"))
                    return 0.0f;
                if (valueText.equalsIgnoreCase("RIGHT"))
                    return 100.0f;

                return valueText.getFloatValue();
            })));


}
}

juce::AudioProcessorValueTreeState::ParameterLayout FftModuleProcessor::createParameterLayout()
{
    ParameterList parameterLayout;
    appendDeltaParameters(parameterLayout);
    appendMainParameters(parameterLayout);
    appendAuxiliaryParameters(parameterLayout);
    return { parameterLayout.begin(), parameterLayout.end() };
}
