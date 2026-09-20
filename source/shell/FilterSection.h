#pragma once

#include "Editor.h"
#include "ChoiceControl.h"
#include "LocalParameterControl.h"
#include "ParameterControl.h"
#include "../modules/eql/Processor.h"

struct AvaAudioProcessorEditor::FilterSection
{
    using FilterType = EqlModuleProcessor::FilterType;

    explicit FilterSection(juce::AudioProcessorValueTreeState& state, int filterIndex);

    void detach() noexcept;
    void rebind(juce::AudioProcessorValueTreeState& state);
    FilterType getFilterType() const noexcept;
    int getPlace() const noexcept;
    double getFrequency() const noexcept;
    bool isBandwidthInactiveAtCurrentOrder() const noexcept;
    bool isOrderInactive() const noexcept;
    bool isGainInactive() const noexcept;
    void updateFrequencyRangeForType();
    void setGainDisplaysDegrees(bool shouldDisplayDegrees);
    void updatePlaceChoicesForType(bool normalizeSelection);
    void refreshTypeDependentControls(bool normalizePlaceSelection = true);

    std::unique_ptr<BoxTextButton> header;
    std::unique_ptr<ChoiceControl> typeControl;
    std::unique_ptr<ChoiceControl> placeControl;
    std::unique_ptr<ChoiceControl> orderControl;
    std::unique_ptr<ParameterControl> frequencyControl;
    std::unique_ptr<ParameterControl> bandwidthControl;
    std::unique_ptr<ParameterControl> gainControl;
    std::unique_ptr<BoxTextButton> bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    juce::AudioParameterChoice* typeParameter = nullptr;
    juce::AudioParameterChoice* placeParameter = nullptr;
    juce::AudioParameterFloat* frequencyParameter = nullptr;
    juce::AudioParameterFloat* bandwidthParameter = nullptr;
    juce::AudioParameterChoice* orderParameter = nullptr;
    juce::AudioParameterFloat* gainParameter = nullptr;
    int filterIndex = 0;
    bool expanded = false;
    bool gainDisplaysDegrees = false;

};
