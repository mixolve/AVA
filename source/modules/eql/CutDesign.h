#pragma once

#include "FilterSupport.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <complex>

namespace ava::eql
{
template <typename Cascade>
void designCutOrderFilter(Cascade& filter, const EqlModuleProcessor::FilterType filterType,
                          const int order, const double frequency, const double sampleRate) noexcept
{
    filter.setIdentity();

    if (sampleRate <= 0.0 || order <= 0)
        return;

    const auto clampedFrequency = juce::jlimit(2.0,
                                               (sampleRate * 0.5) * nyquistSafetyFactor,
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

        const auto coefficients = filterType == EqlModuleProcessor::FilterType::lowCut
            ? juce::dsp::IIR::ArrayCoefficients<double>::makeHighPass(sampleRate,
                                                                      clampedFrequency,
                                                                      stageQ)
            : juce::dsp::IIR::ArrayCoefficients<double>::makeLowPass(sampleRate,
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
        const auto g = std::tan(juce::MathConstants<double>::pi * clampedFrequency / sampleRate);
        const auto denominator = std::max(1.0e-12, 1.0 + g);

        if (filterType == EqlModuleProcessor::FilterType::lowCut)
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

    const auto referenceFrequency = filterType == EqlModuleProcessor::FilterType::highCut
        ? minimumDesignFilterFrequency
        : (sampleRate * 0.5 * nyquistSafetyFactor);
    const auto omega = 2.0 * juce::MathConstants<double>::pi * referenceFrequency / sampleRate;
    const std::complex<double> z1(std::cos(omega), -std::sin(omega));
    const auto z2 = z1 * z1;
    std::complex<double> response(1.0, 0.0);
    for (int index = 0; index < filter.stageCount; ++index)
    {
        const auto& section = filter.sections[static_cast<size_t>(index)];
        response *= (section.b[0] + section.b[1] * z1 + section.b[2] * z2)
            / (1.0 + section.a[0] * z1 + section.a[1] * z2);
    }
    const auto magnitude = std::abs(response);

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
}
