#pragma once

#include "Component.h"

#include <JuceHeader.h>

#include <memory>

class AvaAudioProcessor;
class AvaAudioProcessorEditor;
class DynModuleProcessor;
class TlsModuleProcessor;
class TrsModuleProcessor;

namespace crossover_configs
{
CrossoverModuleComponent::Config makeCrossoverConfig(AvaAudioProcessor& processor);
CrossoverModuleComponent::Config makeTlsCrossoverConfig(TlsModuleProcessor& processor);
CrossoverModuleComponent::Config makeDynCrossoverConfig(DynModuleProcessor& processor);
CrossoverModuleComponent::Config makeTrsCrossoverConfig(TrsModuleProcessor& processor,
                                                        AvaAudioProcessorEditor& editor,
                                                        std::unique_ptr<juce::Component>& editorHolder);
}
