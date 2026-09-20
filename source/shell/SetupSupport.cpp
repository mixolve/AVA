#include "SetupSupport.h"
#include "Style.h"
#include "../modules/fft/Processor.h"

#include <cmath>

namespace
{
class FftSpectrumAnalyserComponent final : public juce::Component
{
public:
    explicit FftSpectrumAnalyserComponent(FftModuleProcessor& processorIn)
        : processor(processorIn)
    {
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }

    void refreshResponse()
    {
        correlationMode = processor.isCorrelationMode();
        upwardMode = processor.isUpwardMode();
        processor.getReductionDisplayBounds(reductionDisplayMinimum, reductionDisplayMaximum);
        processor.copyGainReductionData(leftReductionData, rightReductionData);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(uiGrey800);
        g.fillRect(bounds);

        if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
            return;

        paintReduction(g, bounds, leftReductionData, rightReductionData,
                       reductionDisplayMinimum, reductionDisplayMaximum,
                       upwardMode, correlationMode);
    }

private:
    static void paintReduction(juce::Graphics& g,
                               const juce::Rectangle<float> bounds,
                               const std::array<float, FftModuleProcessor::analyserScopeSize>& leftReduction,
                               const std::array<float, FftModuleProcessor::analyserScopeSize>& rightReduction,
                               const float minimumReduction,
                               const float maximumReduction,
                               const bool upward,
                               const bool correlationMode)
    {
        const auto baselineY = upward ? bounds.getBottom() : bounds.getY();
        const auto verticalDirection = upward ? -1.0f : 1.0f;
        juce::Path path;
        path.startNewSubPath(bounds.getX(), baselineY);

        for (auto index = 0; index < static_cast<int>(FftModuleProcessor::analyserScopeSize); ++index)
        {
            const auto proportion = static_cast<float>(index)
                                  / static_cast<float>(FftModuleProcessor::analyserScopeSize - 1);
            const auto x = bounds.getX() + (proportion * bounds.getWidth());
            const auto dataIndex = static_cast<size_t>(index);
            const auto channelReduction = correlationMode
                ? std::abs(leftReduction[dataIndex])
                : juce::jmax(std::abs(leftReduction[dataIndex]), std::abs(rightReduction[dataIndex]));
            const auto amount = juce::jlimit(minimumReduction, maximumReduction, channelReduction);
            const auto normalizedAmount = (amount - minimumReduction)
                                        / juce::jmax(0.01f, maximumReduction - minimumReduction);
            const auto y = baselineY + (verticalDirection * normalizedAmount * bounds.getHeight());
            path.lineTo(x, y);
        }

        path.lineTo(bounds.getRight(), baselineY);
        path.closeSubPath();
        g.setColour(uiGreyDark);
        g.fillPath(path);
        g.setColour(uiWhite);
        g.strokePath(path, juce::PathStrokeType(1.0f));
    }

    FftModuleProcessor& processor;
    std::array<float, FftModuleProcessor::analyserScopeSize> leftReductionData {};
    std::array<float, FftModuleProcessor::analyserScopeSize> rightReductionData {};
    bool correlationMode = false;
    bool upwardMode = false;
    float reductionDisplayMinimum = 0.0f;
    float reductionDisplayMaximum = 36.0f;
};

FftSpectrumAnalyserComponent* getFftAnalyserComponent(juce::Component* component) noexcept
{
    return dynamic_cast<FftSpectrumAnalyserComponent*>(component);
}
}

namespace shell_setup_support
{
juce::String getMixolveInfoMarkdown()
{
    return juce::String::fromUTF8(BinaryData::about_md, BinaryData::about_mdSize);
}

std::unique_ptr<juce::Component> createFftAnalyserComponent(FftModuleProcessor& processor)
{
    return std::make_unique<FftSpectrumAnalyserComponent>(processor);
}

void refreshFftAnalyserComponent(juce::Component* component)
{
    if (auto* analyser = getFftAnalyserComponent(component))
        analyser->refreshResponse();
}

void removeOwnedChild(juce::Component& owner, std::unique_ptr<juce::Component>& child)
{
    if (child == nullptr)
        return;

    owner.removeChildComponent(child.get());
    child.reset();
}
}
