#include "Processor.h"
#include "Constants.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

void FftModuleProcessor::DynamicProcessor::updateDetectorRange(const ProcessingSettings& settings,
                                                              const int fftSize) noexcept
{
    const auto maximumCutHz = sampleRate * 0.495;
    const auto lowCutHz = static_cast<float>(juce::jlimit(0.001, maximumCutHz, static_cast<double>(settings.detectorLowCutHz)));
    const auto highCutHz = static_cast<float>(juce::jlimit(0.001, maximumCutHz, static_cast<double>(settings.detectorHighCutHz)));

    const auto nearlySameFrequency = [] (const float lhs, const float rhs) noexcept
    {
        const auto scale = std::max({ 1.0f, std::abs(lhs), std::abs(rhs) });
        return std::abs(lhs - rhs) <= 4.0f * std::numeric_limits<float>::epsilon() * scale;
    };

    if (nearlySameFrequency(lowCutHz, currentDetectorLowCutHz)
        && nearlySameFrequency(highCutHz, currentDetectorHighCutHz))
        return;

    currentDetectorLowCutHz = lowCutHz;
    currentDetectorHighCutHz = highCutHz;

    // Two Butterworth biquads per edge give a 24 dB/oct detector response.
    constexpr std::array<double, 2> stageQ { 0.541196100146197, 1.306562964876377 };
    std::array<std::array<double, 6>, 4> coefficients;
    for (size_t stage = 0; stage < stageQ.size(); ++stage)
    {
        coefficients[stage] = juce::dsp::IIR::ArrayCoefficients<double>::makeHighPass(sampleRate, lowCutHz, stageQ[stage]);
        coefficients[stage + stageQ.size()] = juce::dsp::IIR::ArrayCoefficients<double>::makeLowPass(sampleRate, highCutHz, stageQ[stage]);
    }

    for (auto bin = 0; bin <= fftSize / 2; ++bin)
    {
        const auto z = std::polar(1.0, -juce::MathConstants<double>::twoPi * static_cast<double>(bin)
                                      / static_cast<double>(fftSize));
        const auto zSquared = z * z;
        auto magnitude = 1.0;
        for (const auto& c : coefficients)
        {
            const auto numerator = c[0] + c[1] * z + c[2] * zSquared;
            const auto denominator = c[3] + c[4] * z + c[5] * zSquared;
            magnitude *= std::abs(numerator) / juce::jmax(1.0e-30, std::abs(denominator));
        }
        detectorRangeMagnitudes[static_cast<size_t>(bin)] = static_cast<float>(juce::jlimit(0.0, 1.0, magnitude));
    }
}

void FftModuleProcessor::DynamicProcessor::updateCorrelationDetector(
    const ProcessingSettings& settings,
    const int fftSize) noexcept
{
    const auto maximumBin = fftSize / 2;

    for (auto bin = 1; bin < maximumBin; ++bin)
    {
        const auto& left = channelStates[0].frequencyData[static_cast<size_t>(bin)];
        const auto& right = channelStates[1].frequencyData[static_cast<size_t>(bin)];
        const auto leftMagnitude = std::abs(left);
        const auto rightMagnitude = std::abs(right);
        const auto energy = leftMagnitude * leftMagnitude + rightMagnitude * rightMagnitude;
        const auto dot = left.real() * right.real() + left.imag() * right.imag();
        const auto frequencyCorrelationMode = settings.correlationType == CorrelationType::frequency;
        const auto signedCorrelationMode = settings.correlationType == CorrelationType::signedCorrelation;
        const auto denominator = frequencyCorrelationMode || signedCorrelationMode
            ? energy
            : leftMagnitude * rightMagnitude;
        const auto numerator = frequencyCorrelationMode
            ? 2.0f * leftMagnitude * rightMagnitude
            : (signedCorrelationMode ? 2.0f * dot : dot);
        const auto correlation = denominator > 1.0e-12f
            ? juce::jlimit(frequencyCorrelationMode ? 0.0f : -1.0f, 1.0f, numerator / denominator)
            : 1.0f;
        correlationDetector[static_cast<size_t>(bin)] = correlation;
    }

    if (settings.correlationSmoothing < 0.5f)
    {
        detectedCorrelation = correlationDetector;
        return;
    }

    correlationColumnSums.fill(0.0f);
    correlationColumnCounts.fill(0);
    smoothedCorrelationColumns.fill(0.0f);

    constexpr auto columnCount = static_cast<int>(analyserScopeSize);
    const auto maximumFrequency = juce::jmin(analyserMaxFrequency,
                                              static_cast<float>(sampleRate * 0.5));
    const auto minimumLog = std::log(analyserMinFrequency);
    const auto logSpan = juce::jmax(0.0001f, std::log(maximumFrequency) - minimumLog);

    const auto getColumn = [minimumLog, logSpan] (const float frequency)
    {
        const auto normalised = (std::log(frequency) - minimumLog) / logSpan;
        return juce::jlimit(0,
                           columnCount - 1,
                           static_cast<int>(std::floor(normalised * static_cast<float>(columnCount))));
    };

    for (auto bin = 1; bin < maximumBin; ++bin)
    {
        const auto frequency = static_cast<float>(bin) * static_cast<float>(sampleRate)
                             / static_cast<float>(fftSize);

        if (frequency < analyserMinFrequency || frequency > maximumFrequency)
            continue;

        const auto column = getColumn(frequency);
        correlationColumnSums[static_cast<size_t>(column)] += correlationDetector[static_cast<size_t>(bin)];
        ++correlationColumnCounts[static_cast<size_t>(column)];
    }

    const auto smoothingRadius = juce::jlimit(0,
                                              48,
                                              juce::roundToInt(settings.correlationSmoothing * 0.48f));
    const auto sigma = juce::jmax(0.5f, static_cast<float>(smoothingRadius) * 0.5f);

    for (auto column = 0; column < columnCount; ++column)
    {
        auto correlationSum = 0.0f;
        auto weightSum = 0.0f;

        for (auto neighbour = juce::jmax(0, column - smoothingRadius);
             neighbour <= juce::jmin(columnCount - 1, column + smoothingRadius);
             ++neighbour)
        {
            const auto count = correlationColumnCounts[static_cast<size_t>(neighbour)];

            if (count <= 0)
                continue;

            const auto distance = static_cast<float>(std::abs(neighbour - column));
            const auto weight = std::exp(-0.5f * distance * distance / (sigma * sigma));
            correlationSum += correlationColumnSums[static_cast<size_t>(neighbour)] * weight;
            weightSum += static_cast<float>(count) * weight;
        }

        if (weightSum > 0.0f)
            smoothedCorrelationColumns[static_cast<size_t>(column)] = correlationSum / weightSum;
    }

    detectedCorrelation = correlationDetector;

    for (auto bin = 1; bin < maximumBin; ++bin)
    {
        const auto frequency = static_cast<float>(bin) * static_cast<float>(sampleRate)
                             / static_cast<float>(fftSize);

        if (frequency >= analyserMinFrequency && frequency <= maximumFrequency)
            detectedCorrelation[static_cast<size_t>(bin)]
                = smoothedCorrelationColumns[static_cast<size_t>(getColumn(frequency))];
    }
}

