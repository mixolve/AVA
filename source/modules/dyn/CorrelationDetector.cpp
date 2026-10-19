#include "SpectralProcessor.h"
#include "SpectralConstants.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

void DynSpectralProcessor::DynamicProcessor::updateCorrelationDetector(const ProcessingSettings& settings,
                                                                       const int fftSize) noexcept
{
    const auto maximumBin = fftSize / 2;
    const auto rmsCoefficient =
        calculateTimeCoefficient(settings.rmsTimeMs, static_cast<float>(currentHopSize / sampleRate));

    for (auto bin = 1; bin < maximumBin; ++bin)
    {
        const auto separate = settings.separateDetector;
        const auto& left = separate ? sidechainFrequencyData[0][static_cast<size_t>(bin)]
                                    : channelStates[0].frequencyData[static_cast<size_t>(bin)];
        const auto& right = separate ? sidechainFrequencyData[1][static_cast<size_t>(bin)]
                                     : channelStates[1].frequencyData[static_cast<size_t>(bin)];
        const auto leftMagnitude = std::abs(left);
        const auto rightMagnitude = std::abs(right);
        const auto energy = leftMagnitude * leftMagnitude + rightMagnitude * rightMagnitude;
        const auto dot = left.real() * right.real() + left.imag() * right.imag();
        const auto numerator = 2.0f * dot;
        auto correlation = energy > 1.0e-12f ? juce::jlimit(-1.0f, 1.0f, numerator / energy) : 1.0f;
        {
            auto& crossPower = correlationRmsCrossPower[static_cast<size_t>(bin)];
            auto& totalPower = correlationRmsPower[static_cast<size_t>(bin)];
            crossPower = rmsCoefficient * crossPower + (1.0f - rmsCoefficient) * numerator;
            totalPower = rmsCoefficient * totalPower + (1.0f - rmsCoefficient) * energy;
            const auto rmsCorrelation =
                totalPower > 1.0e-12f ? juce::jlimit(-1.0f, 1.0f, crossPower / totalPower) : 1.0f;
            correlationRmsDetector[static_cast<size_t>(bin)] = rmsCorrelation;
        }
        correlationDetector[static_cast<size_t>(bin)] = correlation;
    }

    if (settings.correlationSmoothing < 0.5f)
    {
        detectedCorrelation = correlationDetector;
        detectedRmsCorrelation = correlationRmsDetector;
        return;
    }

    const auto smooth = [&] (const auto& input, auto& output)
    {
        correlationColumnSums.fill(0.0f);
        correlationColumnCounts.fill(0);
        smoothedCorrelationColumns.fill(0.0f);

        constexpr auto columnCount = static_cast<int>(correlationColumnCount);
        const auto maximumFrequency = juce::jmin(detectorMaxFrequency, static_cast<float>(sampleRate * 0.5));
        const auto minimumLog = std::log(detectorMinFrequency);
        const auto logSpan = juce::jmax(0.0001f, std::log(maximumFrequency) - minimumLog);

        const auto getColumn = [minimumLog, logSpan](const float frequency)
        {
            const auto normalised = (std::log(frequency) - minimumLog) / logSpan;
            return juce::jlimit(0, columnCount - 1,
                                static_cast<int>(std::floor(normalised * static_cast<float>(columnCount))));
        };

        for (auto bin = 1; bin < maximumBin; ++bin)
        {
            const auto frequency = static_cast<float>(bin) * static_cast<float>(sampleRate) / static_cast<float>(fftSize);

            if (frequency < detectorMinFrequency || frequency > maximumFrequency)
                continue;

            const auto column = getColumn(frequency);
            correlationColumnSums[static_cast<size_t>(column)] += input[static_cast<size_t>(bin)];
            ++correlationColumnCounts[static_cast<size_t>(column)];
        }

        const auto smoothingRadius = juce::jlimit(0, 48, juce::roundToInt(settings.correlationSmoothing * 0.48f));
        const auto sigma = juce::jmax(0.5f, static_cast<float>(smoothingRadius) * 0.5f);

        for (auto column = 0; column < columnCount; ++column)
        {
            auto correlationSum = 0.0f;
            auto weightSum = 0.0f;

            for (auto neighbour = juce::jmax(0, column - smoothingRadius);
                 neighbour <= juce::jmin(columnCount - 1, column + smoothingRadius); ++neighbour)
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

        output = input;

        for (auto bin = 1; bin < maximumBin; ++bin)
        {
            const auto frequency = static_cast<float>(bin) * static_cast<float>(sampleRate) / static_cast<float>(fftSize);

            if (frequency >= detectorMinFrequency && frequency <= maximumFrequency)
                output[static_cast<size_t>(bin)] =
                    smoothedCorrelationColumns[static_cast<size_t>(getColumn(frequency))];
        }
    };
    smooth(correlationDetector, detectedCorrelation);
    smooth(correlationRmsDetector, detectedRmsCorrelation);
}
