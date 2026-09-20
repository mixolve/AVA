#include "Editor.h"
#include "Processor.h"

bool AvaAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* AvaAudioProcessor::createEditor()
{
    return new AvaAudioProcessorEditor(*this);
}
