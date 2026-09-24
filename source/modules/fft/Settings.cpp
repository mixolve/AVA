#include "Processor.h"
#include "Constants.h"

#include <array>
#include <cmath>
#include <limits>

float FftModuleProcessor::thresholdToCorrelation(const float threshold,
                                                       const bool upward,
                                                       const CorrelationType type) noexcept
{
    const auto depth = juce::jlimit(0.0f, 100.0f, threshold) * 0.01f;

    if (type == CorrelationType::frequency)
        return upward ? 1.0f - depth : depth;

    return upward ? 1.0f - (2.0f * depth)
                  : -1.0f + (2.0f * depth);
}

FftModuleProcessor::ProcessingSettings FftModuleProcessor::getProcessingSettings() const noexcept
{
    ProcessingSettings settings;
    settings.fftSize = getSelectedDspFftSize();
    settings.overlapFactor = getSelectedDspOverlapFactor();
    settings.reductionDisplayTimeMs = getSelectedAveragingTimeMs();
    settings.correlationMode = dynamicModeParam != nullptr
        && dynamicModeParam->load(std::memory_order_relaxed) >= 0.5f;
    const auto correlationTypeIndex = correlationTypeParam != nullptr
        ? juce::roundToInt(correlationTypeParam->load(std::memory_order_relaxed))
        : 0;
    settings.correlationType = static_cast<CorrelationType>(juce::jlimit(0, 2, correlationTypeIndex));
    settings.upward = dynamicDirectionParam != nullptr
        && dynamicDirectionParam->load(std::memory_order_relaxed) >= 0.5f;
    settings.detectorLowCutHz = juce::jlimit(analyserMinFrequency, analyserMaxFrequency,
        detectorLowCutParam != nullptr ? detectorLowCutParam->load(std::memory_order_relaxed) : analyserMinFrequency);
    settings.detectorHighCutHz = juce::jlimit(analyserMinFrequency, analyserMaxFrequency,
        detectorHighCutParam != nullptr ? detectorHighCutParam->load(std::memory_order_relaxed) : analyserMaxFrequency);
    const auto floorValue = juce::jlimit(-100.0f,
                                         0.0f,
                                         floorParam != nullptr ? floorParam->load(std::memory_order_relaxed) : -100.0f);
    settings.floorDb = floorValue <= -100.0f
        ? -std::numeric_limits<float>::infinity()
        : floorValue;
    settings.leftThresholdDb = juce::jlimit(-99.0f, 12.0f, dualMonoLeftThresholdParam != nullptr ? dualMonoLeftThresholdParam->load(std::memory_order_relaxed) : 0.0f);
    settings.rightThresholdDb = juce::jlimit(-99.0f, 12.0f, dualMonoRightThresholdParam != nullptr ? dualMonoRightThresholdParam->load(std::memory_order_relaxed) : 0.0f);
    const auto correlationThreshold = juce::jlimit(0.0f,
                                             100.0f,
                                             correlationThresholdParam != nullptr
                                                 ? correlationThresholdParam->load(std::memory_order_relaxed)
                                                 : 0.0f);
    const auto correlationAdaptive = juce::jlimit(0.0f,
                                            100.0f,
                                            correlationAdaptiveParam != nullptr
                                                ? correlationAdaptiveParam->load(std::memory_order_relaxed)
                                                : 0.0f);
    const auto correlationSlopePerOctave = (juce::jlimit(-9.0f,
                                                    9.0f,
                                                    correlationSlopeParam != nullptr
                                                        ? correlationSlopeParam->load(std::memory_order_relaxed)
                                                        : 0.0f)
                                      / 9.0f)
        / std::log2(analyserMaxFrequency / analyserMinFrequency);
    settings.correlationThreshold = correlationThreshold;
    settings.correlationSmoothing = juce::jlimit(0.0f,
                                                 100.0f,
                                                 correlationSmoothingParam != nullptr
                                                     ? correlationSmoothingParam->load(std::memory_order_relaxed)
                                                     : 30.0f);
    settings.correlationAdaptiveAmount = correlationAdaptive;
    settings.correlationSlopePerOctave = correlationSlopePerOctave;
    settings.correlationImpact = juce::jlimit(-100.0f,
                                        100.0f,
                                        correlationImpactParam != nullptr
                                            ? correlationImpactParam->load(std::memory_order_relaxed)
                                            : 0.0f);
    settings.leftAdaptiveAmount = juce::jlimit(0.0f, 100.0f, dualMonoLeftAdaptiveParam != nullptr ? dualMonoLeftAdaptiveParam->load(std::memory_order_relaxed) : 0.0f);
    settings.rightAdaptiveAmount = juce::jlimit(0.0f, 100.0f, dualMonoRightAdaptiveParam != nullptr ? dualMonoRightAdaptiveParam->load(std::memory_order_relaxed) : 0.0f);
    const auto* adaptiveOffsetParam = settings.correlationMode ? correlationAdaptiveOffsetParam : spectralAdaptiveOffsetParam;
    settings.adaptiveOffset = juce::jlimit(settings.correlationMode ? -1.0f : 0.0f,
                                           settings.correlationMode ? 1.0f : 48.0f,
                                           adaptiveOffsetParam != nullptr
                                               ? adaptiveOffsetParam->load(std::memory_order_relaxed)
                                               : 0.0f);
    settings.adaptiveAttackMs = juce::jlimit(0.0f,
                                             200.0f,
                                             this->adaptiveAttackParam != nullptr
                                                 ? this->adaptiveAttackParam->load(std::memory_order_relaxed)
                                                 : 30.0f);
    settings.adaptiveHoldMs = juce::jlimit(0.0f,
                                           2000.0f,
                                           this->adaptiveHoldParam != nullptr
                                               ? this->adaptiveHoldParam->load(std::memory_order_relaxed)
                                               : 0.0f);
    settings.adaptiveReleaseMs = juce::jlimit(0.0f,
                                              2000.0f,
                                              this->adaptiveReleaseParam != nullptr
                                                  ? this->adaptiveReleaseParam->load(std::memory_order_relaxed)
                                                  : 300.0f);
    settings.slopeDbPerOct = juce::jlimit(-9.0f, 9.0f, dspSlopeParam != nullptr ? dspSlopeParam->load(std::memory_order_relaxed) : 4.5f);
    settings.attackMs = juce::jlimit(0.0f, 200.0f, attackParam != nullptr ? attackParam->load(std::memory_order_relaxed) : 0.0f);
    settings.releaseMs = juce::jlimit(0.0f, 2000.0f, releaseParam != nullptr ? releaseParam->load(std::memory_order_relaxed) : 0.0f);
    settings.kneeDb = juce::jlimit(0.0f, 24.0f, kneeParam != nullptr ? kneeParam->load(std::memory_order_relaxed) : 0.0f);
    settings.ratio = juce::jlimit(1.0f, 100.0f, ratioParam != nullptr ? ratioParam->load(std::memory_order_relaxed) : 100.0f);
    settings.dynamicBypassed = dynamicBypassParam != nullptr
        && dynamicBypassParam->load(std::memory_order_relaxed) >= 0.5f;
    return settings;
}

bool FftModuleProcessor::isDeltaEnabled() const noexcept
{
    return deltaParam != nullptr
        && juce::roundToInt(deltaParam->load(std::memory_order_relaxed)) != 0;
}

int FftModuleProcessor::getSelectedDspFftSize() const noexcept
{
    static constexpr std::array<int, 5> fftSizes { 1024, 2048, 4096, 8192, 16384 };
    const auto choiceIndex = dspFftSizeParam != nullptr
                           ? juce::jlimit(0, static_cast<int>(fftSizes.size()) - 1,
                                          juce::roundToInt(dspFftSizeParam->load(std::memory_order_relaxed)))
                           : 2;
    return fftSizes[static_cast<size_t>(choiceIndex)];
}

int FftModuleProcessor::getSelectedDspOverlapFactor() const noexcept
{
    static constexpr std::array<int, 5> overlapFactors { 2, 4, 8, 16, 32 };
    const auto choiceIndex = dspOverlapParam != nullptr
                           ? juce::jlimit(0, static_cast<int>(overlapFactors.size()) - 1,
                                          juce::roundToInt(dspOverlapParam->load(std::memory_order_relaxed)))
                           : 4;
    return overlapFactors[static_cast<size_t>(choiceIndex)];
}

float FftModuleProcessor::getSelectedAveragingTimeMs() const noexcept
{
    return juce::jlimit(0.0f, 1000.0f, analyserTimeValue.load(std::memory_order_relaxed));
}
