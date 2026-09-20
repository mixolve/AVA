#include "FilterSupport.h"

#include <cmath>

bool isShelfFilterType(const EqlModuleProcessor::FilterType type) noexcept
{
    return type == EqlModuleProcessor::FilterType::lowShelf
        || type == EqlModuleProcessor::FilterType::highShelf;
}

bool isCutFilterType(const EqlModuleProcessor::FilterType type) noexcept
{
    return type == EqlModuleProcessor::FilterType::lowCut
        || type == EqlModuleProcessor::FilterType::highCut;
}

bool isTiltFilterType(const EqlModuleProcessor::FilterType type) noexcept
{
    return type == EqlModuleProcessor::FilterType::tilt;
}

bool isVolumeFilterType(const EqlModuleProcessor::FilterType type) noexcept
{
    return type == EqlModuleProcessor::FilterType::volume;
}

bool isPhasePlaceChoice(const int choiceIndex) noexcept
{
    return choiceIndex >= 5 && choiceIndex <= 7;
}

constexpr int brickwallBellOrder = 128;
constexpr int brickwallShelfOrder = 128;
constexpr int brickwallLowCutOrder = 64;
constexpr int brickwallHighCutOrder = 96;

static ShelfSlopeBlend mapDiscreteSlopeToBlend(const double slope,
                                               const double orderDivisor,
                                               const int defaultOrder,
                                               const int brickwallOrder) noexcept
{
    if (! std::isfinite(slope))
        return { defaultOrder, defaultOrder, 0.0 };

    if (slope > 96.0)
        return { brickwallOrder, brickwallOrder, 0.0 };

    const auto mappedOrder = juce::jmax(0.0, slope / orderDivisor);
    const auto lowerOrder = juce::jlimit(0, brickwallOrder, static_cast<int>(std::floor(mappedOrder)));
    const auto upperOrder = juce::jlimit(0, brickwallOrder, static_cast<int>(std::ceil(mappedOrder)));
    const auto blend = juce::jlimit(0.0, 1.0, mappedOrder - std::floor(mappedOrder));

    return { lowerOrder, upperOrder, blend };
}

ShelfSlopeBlend mapBellSlopeToBlend(const double slope) noexcept
{
    return mapDiscreteSlopeToBlend(slope,
                                   6.0,
                                   2,
                                   brickwallBellOrder);
}

ShelfSlopeBlend mapShelfSlopeToBlend(const double slope) noexcept
{
    return mapDiscreteSlopeToBlend(slope,
                                   6.0,
                                   4,
                                   brickwallShelfOrder);
}

ShelfSlopeBlend mapCutSlopeToBlend(const EqlModuleProcessor::FilterType type, const double slope) noexcept
{
    return mapDiscreteSlopeToBlend(slope,
                                   6.0,
                                   4,
                                   type == EqlModuleProcessor::FilterType::lowCut
                                       ? brickwallLowCutOrder
                                       : brickwallHighCutOrder);
}

double computeButterworthStageQ(const int biquadStageIndex, const int order) noexcept
{
    if (order <= 0)
        return 0.7071067811865476;

    const auto stage = static_cast<double>(biquadStageIndex) + 1.0;
    const auto denominator = 2.0 * std::sin(((2.0 * stage) - 1.0) * juce::MathConstants<double>::pi
                                             / (2.0 * static_cast<double>(order)));

    if (std::abs(denominator) < 1.0e-12)
        return 0.7071067811865476;

    return 1.0 / denominator;
}

double computeDesignFrequency(const double displayedFrequency, const double sampleRate) noexcept
{
    const auto visibleFrequency = juce::jlimit(static_cast<double>(minimumVisibleFilterFrequency),
                                               static_cast<double>(maximumVisibleFilterFrequency),
                                               displayedFrequency);

    if (sampleRate <= 0.0)
        return visibleFrequency;

    const auto maximumDesignFrequency = (sampleRate * 0.5) * nyquistSafetyFactor;

    if (maximumDesignFrequency <= minimumDesignFilterFrequency)
        return minimumDesignFilterFrequency;

    if (visibleFrequency <= lowFrequencyExtensionEnd)
    {
        const auto normalised = juce::jlimit(0.0,
                                             1.0,
                                             (visibleFrequency - static_cast<double>(minimumVisibleFilterFrequency))
                                                 / (lowFrequencyExtensionEnd - static_cast<double>(minimumVisibleFilterFrequency)));
        const auto mappedFrequency = std::exp(juce::jmap(normalised,
                                                         std::log(minimumDesignFilterFrequency),
                                                         std::log(lowFrequencyExtensionEnd)));
        return juce::jlimit(minimumDesignFilterFrequency, maximumDesignFrequency, mappedFrequency);
    }

    if (maximumDesignFrequency <= static_cast<double>(maximumVisibleFilterFrequency)
        || visibleFrequency <= highFrequencyExtensionStart)
    {
        return juce::jlimit(minimumDesignFilterFrequency, maximumDesignFrequency, visibleFrequency);
    }

    const auto normalised = juce::jlimit(0.0,
                                         1.0,
                                         std::log(visibleFrequency / highFrequencyExtensionStart)
                                             / std::log(static_cast<double>(maximumVisibleFilterFrequency) / highFrequencyExtensionStart));
    const auto mappedFrequency = std::exp(juce::jmap(normalised,
                                                     std::log(highFrequencyExtensionStart),
                                                     std::log(maximumDesignFrequency)));
    return juce::jlimit(minimumDesignFilterFrequency, maximumDesignFrequency, mappedFrequency);
}

double mapBandwidthToShelfShape(const double octaveBandwidth) noexcept
{
    const auto bandwidth = juce::jlimit(static_cast<double>(minimumBellBandwidth),
                                        static_cast<double>(maximumBellBandwidth),
                                        octaveBandwidth);
    return juce::jlimit(static_cast<double>(minimumBiquadQ),
                        static_cast<double>(maximumBiquadQ),
                        1.0 / (juce::MathConstants<double>::sqrt2 * bandwidth));
}
