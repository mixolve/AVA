#pragma once

#include <JuceHeader.h>

#include <memory>

class CrossoverModuleComponent;

class CrossoverModulePage : public juce::Component
{
public:
    ~CrossoverModulePage() override = default;

    virtual void refreshExternalState() = 0;
    virtual int getPreferredHeight() const = 0;
    virtual juce::Component* getPinnedHeaderComponent() noexcept { return nullptr; }
    virtual int getPinnedHeaderHeight() const noexcept { return 0; }
    virtual juce::Component* getPinnedTailComponent() noexcept { return nullptr; }
    virtual int getPinnedTailHeight() const noexcept { return 0; }
    virtual void layoutPinnedTail() {}
};

std::unique_ptr<CrossoverModulePage> makeCrossoverRangePage(CrossoverModuleComponent& owner,
                                                            size_t rangeIndex,
                                                            juce::Colour accent);
std::unique_ptr<CrossoverModulePage> makeCrossoverSettingsPage(CrossoverModuleComponent& owner);
