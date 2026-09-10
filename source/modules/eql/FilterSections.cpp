#if defined(__APPLE__)
 #include <Accelerate/Accelerate.h>
#endif

#include "ProcessorSupport.h"

#include <algorithm>

void EqlModuleProcessor::SecondOrderSection::reset() noexcept
{
    for (auto& channelState : state)
        channelState.fill(0.0);
}

void EqlModuleProcessor::SecondOrderSection::setIdentity() noexcept
{
    b = { 1.0, 0.0, 0.0 };
    a = { 0.0, 0.0 };
    targetB = b;
    targetA = a;
    coefficientBStep.fill(0.0);
    coefficientAStep.fill(0.0);
    transitionSamplesRemaining = 0;
    reset();
}

void EqlModuleProcessor::SecondOrderSection::transitionTo(const SecondOrderSection& target,
                                                          const int transitionSamples) noexcept
{
    targetB = target.b;
    targetA = target.a;

    if (transitionSamples <= 0)
    {
        b = targetB;
        a = targetA;
        coefficientBStep.fill(0.0);
        coefficientAStep.fill(0.0);
        transitionSamplesRemaining = 0;
        return;
    }

    transitionSamplesRemaining = transitionSamples;

    for (size_t index = 0; index < b.size(); ++index)
        coefficientBStep[index] = (targetB[index] - b[index]) / static_cast<double>(transitionSamples);

    for (size_t index = 0; index < a.size(); ++index)
        coefficientAStep[index] = (targetA[index] - a[index]) / static_cast<double>(transitionSamples);
}

bool EqlModuleProcessor::SecondOrderSection::isTransitioning() const noexcept
{
    return transitionSamplesRemaining > 0;
}

void EqlModuleProcessor::SecondOrderSection::process(juce::AudioBuffer<float>& buffer, const int numChannels) noexcept
{
    const auto channelLimit = juce::jlimit(0, static_cast<int>(maxSupportedChannels), numChannels);

    for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
    {
        if (transitionSamplesRemaining > 0)
        {
            for (size_t index = 0; index < b.size(); ++index)
                b[index] += coefficientBStep[index];

            for (size_t index = 0; index < a.size(); ++index)
                a[index] += coefficientAStep[index];

            if (--transitionSamplesRemaining == 0)
            {
                b = targetB;
                a = targetA;
            }
        }

        for (int channel = 0; channel < channelLimit; ++channel)
        {
            auto* samples = buffer.getWritePointer(channel);
            auto& s = state[static_cast<size_t>(channel)];
            const auto x = static_cast<double>(samples[sampleIndex]);
            const auto y = (b[0] * x) + s[0];
            s[0] = (b[1] * x) - (a[0] * y) + s[1];
            s[1] = (b[2] * x) - (a[1] * y);
            samples[sampleIndex] = static_cast<float>(y);
        }
    }
}

void EqlModuleProcessor::FourthOrderSection::reset() noexcept
{
    for (auto& channelState : state)
        channelState.fill(0.0);
}

void EqlModuleProcessor::FourthOrderSection::setIdentity() noexcept
{
    b = { 1.0, 0.0, 0.0, 0.0, 0.0 };
    a = { 0.0, 0.0, 0.0, 0.0 };
    reset();
}

void EqlModuleProcessor::FourthOrderSection::process(juce::AudioBuffer<float>& buffer, const int numChannels) noexcept
{
    const auto channelLimit = juce::jlimit(0, static_cast<int>(maxSupportedChannels), numChannels);

    for (int channel = 0; channel < channelLimit; ++channel)
    {
        auto* samples = buffer.getWritePointer(channel);
        auto& s = state[static_cast<size_t>(channel)];
        auto s1 = s[0];
        auto s2 = s[1];
        auto s3 = s[2];
        auto s4 = s[3];

        for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
        {
            const auto x = static_cast<double>(samples[sampleIndex]);
            const auto y = (b[0] * x) + s1;
            s1 = (b[1] * x) - (a[0] * y) + s2;
            s2 = (b[2] * x) - (a[1] * y) + s3;
            s3 = (b[3] * x) - (a[2] * y) + s4;
            s4 = (b[4] * x) - (a[3] * y);
            samples[sampleIndex] = static_cast<float>(y);
        }

        s[0] = s1;
        s[1] = s2;
        s[2] = s3;
        s[3] = s4;
    }
}

void EqlModuleProcessor::BellOrderFilter::reset() noexcept
{
    for (auto& section : sections)
        section.reset();
}

void EqlModuleProcessor::BellOrderFilter::setIdentity() noexcept
{
    sectionCount = 0;

    for (auto& section : sections)
        section.setIdentity();
}

void EqlModuleProcessor::BellOrderFilter::process(juce::AudioBuffer<float>& buffer, const int numChannels) noexcept
{
    if (sectionCount <= 0)
        return;

    for (int sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
        sections[static_cast<size_t>(sectionIndex)].process(buffer, numChannels);
}

void EqlModuleProcessor::BiquadCascade::reset() noexcept
{
    for (auto& section : sections)
        section.reset();
}

void EqlModuleProcessor::BiquadCascade::setIdentity() noexcept
{
    stageCount = 0;
    targetStageCount = 0;

    for (auto& section : sections)
        section.setIdentity();
}

void EqlModuleProcessor::BiquadCascade::transitionTo(const BiquadCascade& target,
                                                     const int transitionSamples) noexcept
{
    targetStageCount = target.stageCount;
    const auto transitionStageCount = juce::jmax(stageCount, targetStageCount);
    SecondOrderSection identitySection;

    for (int sectionIndex = 0; sectionIndex < transitionStageCount; ++sectionIndex)
    {
        const auto& targetSection = sectionIndex < targetStageCount
            ? target.sections[static_cast<size_t>(sectionIndex)]
            : identitySection;
        sections[static_cast<size_t>(sectionIndex)].transitionTo(targetSection, transitionSamples);
    }

    stageCount = transitionStageCount;

    if (transitionSamples <= 0)
        stageCount = targetStageCount;
}

void EqlModuleProcessor::BiquadCascade::process(juce::AudioBuffer<float>& buffer, const int numChannels) noexcept
{
    if (stageCount <= 0)
        return;

    for (int sectionIndex = 0; sectionIndex < stageCount; ++sectionIndex)
        sections[static_cast<size_t>(sectionIndex)].process(buffer, numChannels);

    const auto transitionComplete = std::none_of(sections.begin(),
                                                 sections.begin() + static_cast<std::ptrdiff_t>(stageCount),
                                                 [] (const auto& section) { return section.isTransitioning(); });

    if (transitionComplete && stageCount != targetStageCount)
    {
        for (int sectionIndex = targetStageCount; sectionIndex < stageCount; ++sectionIndex)
            sections[static_cast<size_t>(sectionIndex)].setIdentity();

        stageCount = targetStageCount;
    }
}

void EqlModuleProcessor::PhaseFirFilter::reset() noexcept
{
    for (auto& channelState : state)
        channelState.fill(0.0f);

    writeIndex = 0;
}

void EqlModuleProcessor::PhaseFirFilter::setIdentity() noexcept
{
    active = false;
    taps.fill(0.0f);
    taps[static_cast<size_t>(phaseFirLatencySamples)] = 1.0f;
    reset();
}

void EqlModuleProcessor::PhaseFirFilter::process(juce::AudioBuffer<float>& buffer, const int numChannels) noexcept
{
    if (! active)
        return;

    processWithChannelMask(buffer, numChannels, true, true);
}

void EqlModuleProcessor::PhaseFirFilter::processWithChannelMask(juce::AudioBuffer<float>& buffer,
                                                                const int numChannels,
                                                                const bool processLeft,
                                                                const bool processRight) noexcept
{
    if (! active)
        return;

    const auto channelLimit = juce::jlimit(0, static_cast<int>(maxSupportedChannels), numChannels);

    for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
    {
        if (--writeIndex < 0)
            writeIndex = phaseFirSize - 1;

        for (int channel = 0; channel < channelLimit; ++channel)
        {
            auto& channelState = state[static_cast<size_t>(channel)];
            const auto input = buffer.getSample(channel, sampleIndex);
            channelState[static_cast<size_t>(writeIndex)] = input;
            channelState[static_cast<size_t>(writeIndex + phaseFirSize)] = input;
            const auto applyPhase = channelLimit < 2
                || (channel == 0 ? processLeft : processRight);

            if (applyPhase)
            {
                const auto* history = channelState.data() + writeIndex;
                auto output = 0.0f;

               #if JUCE_MAC || JUCE_IOS
                vDSP_dotpr(taps.data(),
                            1,
                            history,
                            1,
                            &output,
                            static_cast<vDSP_Length>(phaseFirSize));
               #else
                for (int tapIndex = 0; tapIndex < phaseFirSize; ++tapIndex)
                    output += taps[static_cast<size_t>(tapIndex)] * history[tapIndex];
               #endif

                buffer.setSample(channel, sampleIndex, output);
            }
            else
            {
                buffer.setSample(channel, sampleIndex, channelState[static_cast<size_t>(writeIndex + phaseFirLatencySamples)]);
            }
        }
    }
}
