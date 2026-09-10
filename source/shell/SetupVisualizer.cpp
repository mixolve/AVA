#include "SetupSupport.h"
#include "Editor.h"
#include "UiStyle.h"
#include "../modules/fft/Processor.h"

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
        phaseCorrMode = processor.isPhaseCorrMode();
        reductionDisplayFloor = processor.getReductionDisplayFloor();
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

        paintReduction(g, bounds, leftReductionData, uiWhite,
                       juce::jmax(0.01f, -reductionDisplayFloor));

        if (! phaseCorrMode)
            paintReduction(g, bounds, rightReductionData, uiWhite,
                           juce::jmax(0.01f, -reductionDisplayFloor));
    }

private:
    static void paintReduction(juce::Graphics& g,
                               const juce::Rectangle<float> bounds,
                               const std::array<float, FftModuleProcessor::analyserScopeSize>& reduction,
                               const juce::Colour colour,
                               const float maximumReduction)
    {
        juce::Path path;
        path.startNewSubPath(bounds.getX(), bounds.getY());

        for (auto index = 0; index < static_cast<int>(FftModuleProcessor::analyserScopeSize); ++index)
        {
            const auto proportion = static_cast<float>(index)
                                  / static_cast<float>(FftModuleProcessor::analyserScopeSize - 1);
            const auto x = bounds.getX() + (proportion * bounds.getWidth());
            const auto amount = juce::jlimit(0.0f, maximumReduction,
                                             reduction[static_cast<size_t>(index)]);
            const auto y = bounds.getY() + ((amount / maximumReduction) * bounds.getHeight());
            path.lineTo(x, y);
        }

        path.lineTo(bounds.getRight(), bounds.getY());
        path.closeSubPath();
        g.setColour(uiGreyDark);
        g.fillPath(path);
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(1.0f));
    }

    FftModuleProcessor& processor;
    std::array<float, FftModuleProcessor::analyserScopeSize> leftReductionData {};
    std::array<float, FftModuleProcessor::analyserScopeSize> rightReductionData {};
    bool phaseCorrMode = false;
    float reductionDisplayFloor = -24.0f;
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

void AvaAudioProcessorEditor::refreshFftAnalyserResponse()
{
    if (fftModuleLoaded)
    {
        if (auto* processor = audioProcessor.getFftModuleProcessor())
            rebindFftModeControls(*processor);

        shell_setup_support::refreshFftAnalyserComponent(fftAnalyserComponent.get());
    }
}
