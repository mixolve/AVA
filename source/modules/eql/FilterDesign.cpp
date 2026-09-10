#include "ProcessorSupport.h"

#include <algorithm>
#include <cmath>
#include <complex>

#include <juce_dsp/juce_dsp.h>

namespace
{
template <typename Section>
void writeButterworthParametricEqSection(Section& section,
                                         const int sectionIndex,
                                         const int prototypeOrder,
                                         const double centreRadians,
                                         const double bandwidthRadians,
                                         const double gain) noexcept
{
    const auto centreCosine = std::cos(centreRadians);
    const auto bandwidthGain = std::sqrt(gain);
    const auto prototypeGain = std::pow(gain, 1.0 / static_cast<double>(prototypeOrder));
    const auto prototypeGainSquared = prototypeGain * prototypeGain;
    const auto beta = std::pow(bandwidthGain, -1.0 / static_cast<double>(prototypeOrder))
        * std::tan(0.5 * bandwidthRadians);
    const auto betaSquared = beta * beta;

    if ((prototypeOrder & 1) != 0 && sectionIndex == 0)
    {
        const auto denominator = 1.0 + beta;

        if (! std::isfinite(denominator) || std::abs(denominator) < 1.0e-12)
        {
            section.setIdentity();
            return;
        }

        section.b = {
            (1.0 + (prototypeGain * beta)) / denominator,
            -2.0 * centreCosine / denominator,
            (1.0 - (prototypeGain * beta)) / denominator,
            0.0,
            0.0
        };
        section.a = {
            -2.0 * centreCosine / denominator,
            (1.0 - beta) / denominator,
            0.0,
            0.0
        };
        return;
    }

    const auto secondOrderSectionIndex = sectionIndex + ((prototypeOrder & 1) != 0 ? 0 : 1);
    const auto ui = ((2.0 * static_cast<double>(secondOrderSectionIndex)) - 1.0)
        / static_cast<double>(prototypeOrder);
    const auto si = std::sin(0.5 * juce::MathConstants<double>::pi * ui);
    const auto denominator = betaSquared + (2.0 * si * beta) + 1.0;

    if (! std::isfinite(denominator) || std::abs(denominator) < 1.0e-12)
    {
        section.setIdentity();
        return;
    }

    section.b = {
        ((prototypeGainSquared * betaSquared) + (2.0 * prototypeGain * si * beta) + 1.0) / denominator,
        -4.0 * centreCosine * (1.0 + (prototypeGain * si * beta)) / denominator,
        2.0 * ((1.0 + (2.0 * centreCosine * centreCosine)) - (prototypeGainSquared * betaSquared)) / denominator,
        -4.0 * centreCosine * (1.0 - (prototypeGain * si * beta)) / denominator,
        ((prototypeGainSquared * betaSquared) - (2.0 * prototypeGain * si * beta) + 1.0) / denominator
    };
    section.a = {
        -4.0 * centreCosine * (1.0 + (si * beta)) / denominator,
        2.0 * (1.0 + (2.0 * centreCosine * centreCosine) - betaSquared) / denominator,
        -4.0 * centreCosine * (1.0 - (si * beta)) / denominator,
        (betaSquared - (2.0 * si * beta) + 1.0) / denominator
    };
}

float phaseBrickwallAwareOrder(const float slope) noexcept
{
    return slope > 96.0f ? 128.0f
                         : juce::jmax(1.0f, slope / 6.0f);
}

float logisticShelfShape(const float octaveOffset, const float steepness) noexcept
{
    return 1.0f / (1.0f + std::exp(juce::jlimit(-80.0f, 80.0f, octaveOffset * steepness)));
}

float phaseShapeForType(const EqlModuleProcessor::FilterType filterType,
                        const float binFrequency,
                        const float centreFrequency,
                        const float bandwidth,
                        const float slope) noexcept
{
    const auto safeFrequency = juce::jlimit(static_cast<float>(minimumVisibleFilterFrequency),
                                            static_cast<float>(maximumVisibleFilterFrequency),
                                            centreFrequency);
    const auto safeBandwidth = juce::jlimit(0.05f, 8.0f, bandwidth);
    const auto octaveOffset = std::log2(juce::jmax(static_cast<float>(minimumVisibleFilterFrequency), binFrequency) / safeFrequency);
    const auto phaseOrder = phaseBrickwallAwareOrder(slope);

    switch (filterType)
    {
        case EqlModuleProcessor::FilterType::lowShelf:
            return logisticShelfShape(octaveOffset, phaseOrder);

        case EqlModuleProcessor::FilterType::bell:
        {
            const auto sigma = juce::jmax(0.025f, safeBandwidth);
            const auto exponent = juce::jlimit(1.0f, 128.0f, phaseOrder);
            return std::exp(-0.5f * std::pow(std::abs(octaveOffset) / sigma, exponent));
        }

        case EqlModuleProcessor::FilterType::tilt:
        {
            const auto lowOctaveOffset = std::log2(static_cast<float>(minimumVisibleFilterFrequency) / safeFrequency);
            const auto highOctaveOffset = std::log2(static_cast<float>(maximumVisibleFilterFrequency) / safeFrequency);
            const auto octaveRange = juce::jmax(1.0e-6f, highOctaveOffset - lowOctaveOffset);
            return juce::jlimit(0.0f, 1.0f, (octaveOffset - lowOctaveOffset) / octaveRange);
        }

        case EqlModuleProcessor::FilterType::highShelf:
            return logisticShelfShape(-octaveOffset, phaseOrder);

        case EqlModuleProcessor::FilterType::lowCut:
        case EqlModuleProcessor::FilterType::highCut:
        case EqlModuleProcessor::FilterType::volume:
            break;
    }

    return 0.0f;
}

double phaseAmountRadiansForType(const EqlModuleProcessor::FilterType filterType,
                                 const double gainDb) noexcept
{
    if (filterType == EqlModuleProcessor::FilterType::lowCut
        || filterType == EqlModuleProcessor::FilterType::highCut)
    {
        return 0.0;
    }

    static constexpr auto phaseWrapGuardRadians = 0.025;
    const auto phaseLimit = juce::MathConstants<double>::pi - phaseWrapGuardRadians;
    return juce::jlimit(-phaseLimit,
                        phaseLimit,
                        (gainDb / 12.0) * (juce::MathConstants<double>::pi * 0.5));
}
}

void EqlModuleProcessor::setBellIdentityResponse(const size_t filterIndex) noexcept
{
    for (auto& filter : bellOrderFilters[filterIndex])
        filter.setIdentity();
}

void EqlModuleProcessor::setShelfIdentityResponse(const size_t filterIndex) noexcept
{
    for (auto& filter : shelfOrderFilters[filterIndex])
        filter.setIdentity();
}

void EqlModuleProcessor::setCutIdentityResponse(const size_t filterIndex) noexcept
{
    cutBlendFilters[filterIndex].setIdentity();
}

void EqlModuleProcessor::setTiltIdentityResponse(const size_t filterIndex) noexcept
{
    tiltFilters[filterIndex].setIdentity();
}

void EqlModuleProcessor::setPhaseIdentityResponse(const size_t filterIndex) noexcept
{
    phaseFirFilters[filterIndex].setIdentity();
}

double EqlModuleProcessor::evaluateCascadeMagnitudeAt(const BiquadCascade& filter, const double frequency) const noexcept
{
    return std::abs(evaluateCascadeResponseAt(filter, frequency));
}

std::complex<double> EqlModuleProcessor::evaluateCascadeResponseAt(const BiquadCascade& filter, const double frequency) const noexcept
{
    if (filter.stageCount <= 0 || currentSampleRate <= 0.0)
        return { 1.0, 0.0 };

    const auto clampedFrequency = juce::jlimit(2.0,
                                               (currentSampleRate * 0.5) * nyquistSafetyFactor,
                                               frequency);
    const auto omega = 2.0 * juce::MathConstants<double>::pi * clampedFrequency / currentSampleRate;
    const std::complex<double> z1(std::cos(omega), -std::sin(omega));
    const std::complex<double> z2(std::cos(2.0 * omega), -std::sin(2.0 * omega));
    std::complex<double> response(1.0, 0.0);

    for (int sectionIndex = 0; sectionIndex < filter.stageCount; ++sectionIndex)
    {
        const auto& section = filter.sections[static_cast<size_t>(sectionIndex)];
        const std::complex<double> numerator = section.b[0] + (section.b[1] * z1) + (section.b[2] * z2);
        const std::complex<double> denominator = 1.0 + (section.a[0] * z1) + (section.a[1] * z2);
        response *= numerator / denominator;
    }

    return response;
}

void EqlModuleProcessor::updateBellOrderFilter(BellOrderFilter& filter,
                                              const int order,
                                              const double frequency,
                                              const double octaveBandwidth,
                                              const double gain) noexcept
{
    if (currentSampleRate <= 0.0 || order <= 0 || std::abs(gain - 1.0) < 1.0e-9)
    {
        filter.sectionCount = 0;
        return;
    }

    const auto minimumFrequency = minimumDesignFilterFrequency;
    const auto maximumFrequency = (currentSampleRate * 0.5) * nyquistSafetyFactor;
    const auto centreFrequency = juce::jlimit(minimumFrequency, maximumFrequency, frequency);
    const auto bandwidth = juce::jlimit(static_cast<double>(minimumBellBandwidth),
                                        static_cast<double>(maximumBellBandwidth),
                                        octaveBandwidth);
    const auto halfBandwidthRatio = std::pow(2.0, 0.5 * bandwidth);
    const auto lowerEdge = centreFrequency / halfBandwidthRatio;
    const auto upperEdge = centreFrequency * halfBandwidthRatio;
    const auto digitalOrder = juce::jmax(2, order + (order & 1));
    const auto prototypeOrder = juce::jmax(1, digitalOrder / 2);
    const auto lowerFrequencyEdge = juce::jlimit(minimumFrequency, maximumFrequency, lowerEdge);

    if (upperEdge <= lowerEdge)
        return;

    const auto bandwidthRadians = juce::jlimit(1.0e-6,
                                               juce::MathConstants<double>::pi * nyquistSafetyFactor,
                                               2.0 * juce::MathConstants<double>::pi
                                                   * (upperEdge - lowerFrequencyEdge) / currentSampleRate);
    const auto centreRadians = 2.0 * juce::MathConstants<double>::pi * centreFrequency / currentSampleRate;
    const auto sectionCount = juce::jlimit(1,
                                           static_cast<int>(filter.sections.size()),
                                           (prototypeOrder + 1) / 2);
    filter.sectionCount = sectionCount;

    for (int sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
    {
        writeButterworthParametricEqSection(filter.sections[static_cast<size_t>(sectionIndex)],
                                            sectionIndex,
                                            prototypeOrder,
                                            centreRadians,
                                            bandwidthRadians,
                                            gain);
    }

    for (int sectionIndex = filter.sectionCount; sectionIndex < static_cast<int>(filter.sections.size()); ++sectionIndex)
        filter.sections[static_cast<size_t>(sectionIndex)].setIdentity();
}

void EqlModuleProcessor::updateShelfOrderFilterRaw(BiquadCascade& filter,
                                                  const FilterType filterType,
                                                  const int order,
                                                  const double frequency,
                                                  const double octaveBandwidth,
                                                  const double gain) noexcept
{
    const auto clampedFrequency = juce::jlimit(2.0,
                                               (currentSampleRate * 0.5) * nyquistSafetyFactor,
                                               frequency);
    const auto shapeScale = juce::jmax(0.001,
                                       mapBandwidthToShelfShape(octaveBandwidth)
                                           / (1.0 / juce::MathConstants<double>::sqrt2));
    const auto hasFirstOrderSection = (order & 1) != 0;
    const auto biquadStageCount = order / 2;
    filter.stageCount = biquadStageCount + (hasFirstOrderSection ? 1 : 0);
    const auto stageGain = std::pow(gain, 1.0 / static_cast<double>(juce::jmax(1, filter.stageCount)));
    auto nextSectionIndex = 0;

    if (hasFirstOrderSection)
    {
        auto& firstOrderSection = filter.sections[static_cast<size_t>(nextSectionIndex++)];
        const auto g = std::tan(juce::MathConstants<double>::pi * clampedFrequency / currentSampleRate);
        const auto denominator = std::max(1.0e-12, 1.0 + g);

        if (filterType == FilterType::lowShelf)
        {
            firstOrderSection.b = { ((stageGain * g) + 1.0) / denominator,
                                    ((stageGain * g) - 1.0) / denominator,
                                    0.0 };
        }
        else
        {
            firstOrderSection.b = { (g + stageGain) / denominator,
                                    (g - stageGain) / denominator,
                                    0.0 };
        }

        firstOrderSection.a = { (g - 1.0) / denominator, 0.0 };
    }

    for (int biquadStageIndex = 0; biquadStageIndex < biquadStageCount; ++biquadStageIndex)
    {
        auto& section = filter.sections[static_cast<size_t>(nextSectionIndex++)];
        const auto butterworthStageIndex = biquadStageCount - 1 - biquadStageIndex;
        const auto stageQ = juce::jlimit(1.0e-6,
                                         static_cast<double>(maximumBiquadQ),
                                         computeButterworthStageQ(butterworthStageIndex, order) * shapeScale);
        const auto coefficients = filterType == FilterType::lowShelf
            ? juce::dsp::IIR::ArrayCoefficients<double>::makeLowShelf(currentSampleRate,
                                                                      clampedFrequency,
                                                                      stageQ,
                                                                      stageGain)
            : juce::dsp::IIR::ArrayCoefficients<double>::makeHighShelf(currentSampleRate,
                                                                       clampedFrequency,
                                                                       stageQ,
                                                                       stageGain);
        const auto a0 = std::max(1.0e-12, static_cast<double>(coefficients[3]));

        section.b = { static_cast<double>(coefficients[0]) / a0,
                      static_cast<double>(coefficients[1]) / a0,
                      static_cast<double>(coefficients[2]) / a0 };
        section.a = { static_cast<double>(coefficients[4]) / a0,
                      static_cast<double>(coefficients[5]) / a0 };
    }

    for (int sectionIndex = filter.stageCount; sectionIndex < static_cast<int>(filter.sections.size()); ++sectionIndex)
        filter.sections[static_cast<size_t>(sectionIndex)].setIdentity();

}

void EqlModuleProcessor::updateCutOrderFilterRaw(BiquadCascade& filter,
                                                const FilterType filterType,
                                                const int order,
                                                const double frequency) noexcept
{
    filter.setIdentity();

    if (currentSampleRate <= 0.0 || order <= 0)
        return;

    const auto clampedFrequency = juce::jlimit(2.0,
                                               (currentSampleRate * 0.5) * nyquistSafetyFactor,
                                               frequency);
    const auto hasFirstOrderSection = (order & 1) != 0;
    const auto biquadStageCount = order / 2;
    filter.stageCount = biquadStageCount + (hasFirstOrderSection ? 1 : 0);
    auto nextSectionIndex = 0;

    for (int biquadStageIndex = 0; biquadStageIndex < biquadStageCount; ++biquadStageIndex)
    {
        auto& section = filter.sections[static_cast<size_t>(nextSectionIndex++)];
        const auto butterworthStageIndex = biquadStageCount - 1 - biquadStageIndex;
        const auto stageQ = juce::jlimit(1.0e-6,
                                         static_cast<double>(maximumBiquadQ),
                                         computeButterworthStageQ(butterworthStageIndex, order));

        const auto coefficients = filterType == FilterType::lowCut
            ? juce::dsp::IIR::ArrayCoefficients<double>::makeHighPass(currentSampleRate,
                                                                      clampedFrequency,
                                                                      stageQ)
            : juce::dsp::IIR::ArrayCoefficients<double>::makeLowPass(currentSampleRate,
                                                                     clampedFrequency,
                                                                     stageQ);
        const auto a0 = std::max(1.0e-12, static_cast<double>(coefficients[3]));

        section.b = { static_cast<double>(coefficients[0]) / a0,
                      static_cast<double>(coefficients[1]) / a0,
                      static_cast<double>(coefficients[2]) / a0 };
        section.a = { static_cast<double>(coefficients[4]) / a0,
                      static_cast<double>(coefficients[5]) / a0 };
    }

    if (hasFirstOrderSection)
    {
        auto& firstOrderSection = filter.sections[static_cast<size_t>(nextSectionIndex++)];
        const auto g = std::tan(juce::MathConstants<double>::pi * clampedFrequency / currentSampleRate);
        const auto denominator = std::max(1.0e-12, 1.0 + g);

        if (filterType == FilterType::lowCut)
        {
            firstOrderSection.b = { 1.0 / denominator,
                                    -1.0 / denominator,
                                    0.0 };
        }
        else
        {
            firstOrderSection.b = { g / denominator,
                                    g / denominator,
                                    0.0 };
        }

        firstOrderSection.a = { (g - 1.0) / denominator, 0.0 };
    }

    for (int sectionIndex = filter.stageCount; sectionIndex < static_cast<int>(filter.sections.size()); ++sectionIndex)
        filter.sections[static_cast<size_t>(sectionIndex)].setIdentity();

    const auto referenceFrequency = filterType == FilterType::highCut
        ? minimumDesignFilterFrequency
        : (currentSampleRate * 0.5 * nyquistSafetyFactor);
    const auto magnitude = evaluateCascadeMagnitudeAt(filter, referenceFrequency);

    if (magnitude > 1.0e-9 && filter.stageCount > 0)
    {
        auto& firstSection = filter.sections[0];
        const auto normalisation = 1.0 / magnitude;
        firstSection.b[0] *= normalisation;
        firstSection.b[1] *= normalisation;
        firstSection.b[2] *= normalisation;
    }

    filter.reset();
}

void EqlModuleProcessor::updateTiltFilter(BiquadCascade& filter,
                                         const double frequency,
                                         const double gainDb) noexcept
{
    if (currentSampleRate <= 0.0 || std::abs(gainDb) < 1.0e-6)
    {
        filter.stageCount = 0;
        return;
    }

    const auto minimumFrequency = minimumDesignFilterFrequency;
    const auto maximumFrequency = (currentSampleRate * 0.5) * nyquistSafetyFactor;
    const auto clampedPivotFrequency = juce::jlimit(minimumFrequency,
                                                    maximumFrequency,
                                                    frequency);
    const auto totalOctaveRange = std::max(1.0e-6, std::log2(maximumFrequency / minimumFrequency));
    const auto halfOctaveRange = totalOctaveRange * 0.5;
    const auto octaveSpacing = totalOctaveRange / static_cast<double>(flatTiltStageCount);
    const auto stepGainDb = (gainDb / std::max(1.0e-6, halfOctaveRange)) * octaveSpacing;
    const auto stageGain = juce::Decibels::decibelsToGain(stepGainDb);
    filter.stageCount = flatTiltStageCount;

    for (int sectionIndex = 0; sectionIndex < flatTiltStageCount; ++sectionIndex)
    {
        const auto octavePosition = (static_cast<double>(sectionIndex) + 0.5) * octaveSpacing;
        const auto sectionFrequency = minimumFrequency * std::pow(2.0, octavePosition);
        const auto clampedSectionFrequency = juce::jlimit(minimumFrequency,
                                                          maximumFrequency,
                                                          sectionFrequency);
        const auto g = std::tan(juce::MathConstants<double>::pi * clampedSectionFrequency / currentSampleRate);
        const auto denominator = std::max(1.0e-12, 1.0 + g);
        auto& section = filter.sections[static_cast<size_t>(sectionIndex)];

        section.b = { (g + stageGain) / denominator,
                      (g - stageGain) / denominator,
                      0.0 };
        section.a = { (g - 1.0) / denominator, 0.0 };
    }

    for (int sectionIndex = filter.stageCount; sectionIndex < static_cast<int>(filter.sections.size()); ++sectionIndex)
        filter.sections[static_cast<size_t>(sectionIndex)].setIdentity();

    const auto magnitudeAtPivot = evaluateCascadeMagnitudeAt(filter, clampedPivotFrequency);

    if (magnitudeAtPivot > 1.0e-9)
    {
        const auto normalisation = 1.0 / magnitudeAtPivot;
        auto& firstSection = filter.sections[0];
        firstSection.b[0] *= normalisation;
        firstSection.b[1] *= normalisation;
        firstSection.b[2] *= normalisation;
    }

}

void EqlModuleProcessor::updatePhaseFirFilter(PhaseFirFilter& target,
                                             const FilterType filterType,
                                             const double frequency,
                                             const double octaveBandwidth,
                                             const double slope,
                                             const double gainDb) noexcept
{
    target.setIdentity();

    if (currentSampleRate <= 0.0 || isVolumeFilterType(filterType) || isCutFilterType(filterType))
        return;

    const auto phaseAmountRadians = phaseAmountRadiansForType(filterType, gainDb);

    if (std::abs(phaseAmountRadians) <= 1.0e-6)
        return;

    std::array<juce::dsp::Complex<float>, phaseFirSize> spectrum {};

    const auto nyquistFrequency = currentSampleRate * 0.5;
    const auto highShelfTaperStart = juce::jmin(20000.0, nyquistFrequency * 0.95);

    for (int bin = 0; bin <= phaseFirSize / 2; ++bin)
    {
        const auto binFrequency = static_cast<float>((static_cast<double>(bin) * currentSampleRate)
                                                     / static_cast<double>(phaseFirSize));
        const auto shape = phaseShapeForType(filterType,
                                             binFrequency,
                                             static_cast<float>(frequency),
                                             static_cast<float>(octaveBandwidth),
                                             static_cast<float>(slope));
        auto phaseRadians = static_cast<float>(phaseAmountRadians) * shape;

        if (filterType == FilterType::highShelf && binFrequency > highShelfTaperStart)
        {
            const auto progress = juce::jlimit(0.0f,
                                               1.0f,
                                               static_cast<float>((binFrequency - highShelfTaperStart)
                                                                  / (nyquistFrequency - highShelfTaperStart)));
            const auto smoothProgress = progress * progress * (3.0f - (2.0f * progress));
            phaseRadians *= 1.0f - smoothProgress;
        }

        spectrum[static_cast<size_t>(bin)] = { std::cos(phaseRadians), std::sin(phaseRadians) };

        if (bin > 0 && bin < phaseFirSize / 2)
            spectrum[static_cast<size_t>(phaseFirSize - bin)] = std::conj(spectrum[static_cast<size_t>(bin)]);
    }

    phaseFirFft.perform(spectrum.data(), spectrum.data(), true);

    for (int tapIndex = 0; tapIndex < phaseFirSize; ++tapIndex)
    {
        const auto sourceIndex = (tapIndex - phaseFirLatencySamples + phaseFirSize) % phaseFirSize;
        target.taps[static_cast<size_t>(tapIndex)] = spectrum[static_cast<size_t>(sourceIndex)].real();
    }

    target.active = true;
    target.reset();
}

void EqlModuleProcessor::updateInterpolatedCascadeFilter(BiquadCascade& target,
                                                        const BiquadCascade& lower,
                                                        const BiquadCascade& upper,
                                                        const double blend) noexcept
{
    const auto clampedBlend = juce::jlimit(0.0, 1.0, blend);
    target.stageCount = juce::jmax(lower.stageCount, upper.stageCount);

    for (int sectionIndex = 0; sectionIndex < target.stageCount; ++sectionIndex)
    {
        auto& targetSection = target.sections[static_cast<size_t>(sectionIndex)];
        const auto& lowerSection = lower.sections[static_cast<size_t>(sectionIndex)];
        const auto& upperSection = upper.sections[static_cast<size_t>(sectionIndex)];

        for (size_t coefficientIndex = 0; coefficientIndex < targetSection.b.size(); ++coefficientIndex)
            targetSection.b[coefficientIndex] = juce::jmap(clampedBlend,
                                                           lowerSection.b[coefficientIndex],
                                                           upperSection.b[coefficientIndex]);

        for (size_t coefficientIndex = 0; coefficientIndex < targetSection.a.size(); ++coefficientIndex)
            targetSection.a[coefficientIndex] = juce::jmap(clampedBlend,
                                                           lowerSection.a[coefficientIndex],
                                                           upperSection.a[coefficientIndex]);
    }

    for (int sectionIndex = target.stageCount; sectionIndex < static_cast<int>(target.sections.size()); ++sectionIndex)
    {
        auto& section = target.sections[static_cast<size_t>(sectionIndex)];
        section.b = { 1.0, 0.0, 0.0 };
        section.a = { 0.0, 0.0 };
    }
}

void EqlModuleProcessor::rebuildCutBlendFilter(const size_t filterIndex,
                                              const FilterType filterType,
                                              const double frequency,
                                              const double slope) noexcept
{
    BiquadCascade target;
    buildCutBlendFilter(target, filterType, frequency, slope);
    const auto& cachedState = cachedFilterStates[filterIndex];
    const auto isContinuousUpdate = cachedState.valid
        && cachedState.active
        && cachedState.type == filterType;
    const auto transitionSamples = isContinuousUpdate
        ? juce::jmax(1, juce::roundToInt(currentSampleRate * coefficientTransitionSeconds))
        : 0;
    cutBlendFilters[filterIndex].transitionTo(target, transitionSamples);
}

void EqlModuleProcessor::buildCutBlendFilter(BiquadCascade& baseFilter,
                                            const FilterType filterType,
                                            const double frequency,
                                            const double slope) noexcept
{
    baseFilter.setIdentity();

    if (! isCutFilterType(filterType) || currentSampleRate <= 0.0)
        return;

    if (slope <= 1.0e-4)
        return;

    const auto clampedFrequency = juce::jlimit(2.0,
                                               (currentSampleRate * 0.5) * nyquistSafetyFactor,
                                               frequency);

    const auto cutSlopeBlend = mapCutSlopeToBlend(filterType, slope);

    if (cutSlopeBlend.lowerOrder == cutSlopeBlend.upperOrder || cutSlopeBlend.blend < 1.0e-6)
    {
        updateCutOrderFilterRaw(baseFilter,
                                filterType,
                                cutSlopeBlend.lowerOrder,
                                clampedFrequency);
        return;
    }

    auto lowFrequency = juce::jlimit(2.0,
                                     (currentSampleRate * 0.5) * nyquistSafetyFactor,
                                     clampedFrequency * 0.125);
    auto highFrequency = juce::jlimit(2.0,
                                      (currentSampleRate * 0.5) * nyquistSafetyFactor,
                                      clampedFrequency * 8.0);

    if (lowFrequency >= highFrequency)
        std::swap(lowFrequency, highFrequency);

    const auto targetCutoffMagnitude = 1.0 / std::sqrt(2.0);
    BiquadCascade candidateLowerFilter;
    BiquadCascade candidateUpperFilter;
    BiquadCascade candidateMixedFilter;

    for (int iteration = 0; iteration < 12; ++iteration)
    {
        const auto designFrequency = std::sqrt(lowFrequency * highFrequency);
        updateCutOrderFilterRaw(candidateLowerFilter,
                                filterType,
                                cutSlopeBlend.lowerOrder,
                                designFrequency);
        updateCutOrderFilterRaw(candidateUpperFilter,
                                filterType,
                                cutSlopeBlend.upperOrder,
                                designFrequency);
        updateInterpolatedCascadeFilter(candidateMixedFilter,
                                        candidateLowerFilter,
                                        candidateUpperFilter,
                                        cutSlopeBlend.blend);

        const auto magnitudeAtUserFrequency = evaluateCascadeMagnitudeAt(candidateMixedFilter, clampedFrequency);
        const auto magnitudeTooHigh = magnitudeAtUserFrequency > targetCutoffMagnitude;

        if (filterType == FilterType::highCut)
        {
            if (magnitudeTooHigh)
                highFrequency = designFrequency;
            else
                lowFrequency = designFrequency;
        }
        else
        {
            if (magnitudeTooHigh)
                lowFrequency = designFrequency;
            else
                highFrequency = designFrequency;
        }
    }

    const auto compensatedFrequency = std::sqrt(lowFrequency * highFrequency);
    BiquadCascade lowerFilter;
    BiquadCascade upperFilter;

    updateCutOrderFilterRaw(lowerFilter,
                            filterType,
                            cutSlopeBlend.lowerOrder,
                            compensatedFrequency);
    updateCutOrderFilterRaw(upperFilter,
                            filterType,
                            cutSlopeBlend.upperOrder,
                            compensatedFrequency);

    updateInterpolatedCascadeFilter(baseFilter,
                                    lowerFilter,
                                    upperFilter,
                                    cutSlopeBlend.blend);
}
