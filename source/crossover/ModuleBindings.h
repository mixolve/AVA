#pragma once

#include "Editor.h"

#include <JuceHeader.h>

#include <memory>

class AvaAudioProcessor;
class AvaAudioProcessorEditor;
class DynModuleProcessor;
class BlockChainProcessor;

namespace crossover_configs
{
CrossoverModuleComponent::Config makeCrossoverConfig(AvaAudioProcessor& processor);
std::vector<CrossoverModuleComponent::CrossoverControlSpec> makeBlockRangeControls();
std::vector<CrossoverModuleComponent::CrossoverControlSpec> makeDynRangeControls(
    const DynModuleProcessor& processor, size_t rangeIndex);
CrossoverModuleComponent::Config makeBlockChainCrossoverConfig(BlockChainProcessor& processor);
CrossoverModuleComponent::Config makeDynCrossoverConfig(DynModuleProcessor& processor,
                                                        AvaAudioProcessorEditor& editor,
                                                        size_t rangeIndex = 0);
}
