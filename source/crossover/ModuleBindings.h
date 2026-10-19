#pragma once

#include "Editor.h"

#include <JuceHeader.h>

#include <memory>

class AvaAudioProcessor;
class AvaAudioProcessorEditor;
class DynModuleProcessor;
class TlsModuleProcessor;

namespace crossover_configs
{
bool isRangeControlEnabled(const CrossoverModuleComponent::CrossoverControlSpec& control,
                           const juce::AudioProcessorValueTreeState& state,
                           size_t rangeIndex, const juce::String& moduleKey);
CrossoverModuleComponent::Config makeCrossoverConfig(AvaAudioProcessor& processor);
std::vector<CrossoverModuleComponent::CrossoverControlSpec> makeTlsRangeControls();
std::vector<CrossoverModuleComponent::CrossoverControlSpec> makeDynRangeControls(
    const DynModuleProcessor& processor, size_t rangeIndex);
CrossoverModuleComponent::Config makeTlsCrossoverConfig(TlsModuleProcessor& processor);
CrossoverModuleComponent::Config makeDynCrossoverConfig(DynModuleProcessor& processor,
                                                        AvaAudioProcessorEditor& editor,
                                                        std::unique_ptr<juce::Component>& editorHolder,
                                                        size_t rangeIndex = 0);
}
