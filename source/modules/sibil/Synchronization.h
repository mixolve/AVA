#pragma once
#include <JuceHeader.h>
#include <memory>
#include <cstdint>
class AvaAudioProcessor;
class BlockChainProcessor;
namespace ava::modules::sibil {
juce::Identifier linkProperty(size_t band);
std::unique_ptr<juce::Component> makeLinkControl(BlockChainProcessor&,size_t band);
void syncRouting(AvaAudioProcessor& root);
void forgetRouting(uint64_t root);
}
