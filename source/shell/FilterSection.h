#pragma once

#include "Editor.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"
#include "../modules/eql/Processor.h"

struct AvaAudioProcessorEditor::FilterSection
{

    using FilterType = EqlModuleProcessor::FilterType;

    explicit FilterSection(juce::AudioProcessorValueTreeState& state, int filterIndex);

    void detach() noexcept;
    void rebind(juce::AudioProcessorValueTreeState& state);
    void cacheParameterPointers(juce::AudioProcessorValueTreeState& state);
    FilterType getFilterType() const noexcept;
    bool isPhaseEnabled() const noexcept;
    double getFrequency() const noexcept;
    void updateFrequencyRangeForType();
    void setGainDisplaysDegrees(bool shouldDisplayDegrees);
    void updatePhaseForType(bool normalizeSelection);
    void refreshTypeDependentControls(bool normalizePhaseSelection = true);

    BackgroundClickComponent body;
    std::unique_ptr<PushControl> header;
    std::unique_ptr<ChoiceControl> typeControl;
    std::unique_ptr<AccessoryButton> phaseIndicator;
    std::unique_ptr<ChoiceControl> orderControl;
    std::unique_ptr<ParameterControl> frequencyControl;
    std::unique_ptr<ParameterControl> bandwidthControl;
    std::unique_ptr<ParameterControl> gainControl;
    juce::AudioParameterChoice* typeParameter = nullptr;
    juce::AudioParameterBool* phaseParameter = nullptr;
    juce::AudioParameterFloat* frequencyParameter = nullptr;
    juce::AudioParameterChoice* orderParameter = nullptr;
    int filterIndex = 0;
    bool expanded = false;
    bool gainDisplaysDegrees = false;

};
