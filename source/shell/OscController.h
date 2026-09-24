#pragma once

#include "OscSettings.h"

#include <JuceHeader.h>

#include <map>

class AvaAudioProcessor;

class OscController final : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
                            private juce::Timer,
                            private juce::AsyncUpdater
{
public:
    explicit OscController(AvaAudioProcessor& owner);
    ~OscController() override;

    bool applySettings(const OscSettings& settings);
    void applySettingsAsync(const OscSettings& settings);
    bool isInputConnected() const noexcept { return inputConnected; }
    bool isOutputConnected() const noexcept { return outputConnected; }
    bool isInputPortBusy() const noexcept;

private:
    void oscMessageReceived(const juce::OSCMessage& message) override;
    void oscBundleReceived(const juce::OSCBundle& bundle) override;
    void timerCallback() override;
    void handleAsyncUpdate() override;
    void processBundle(const juce::OSCBundle& bundle);
    void sendCurrentState();
    void sendInstanceState();
    void sendParameters(juce::AudioProcessorValueTreeState& state,
                        const juce::String& parameterIdPrefix = {});
    void sendCrossoverActions();
    void sendParameterValue(const juce::String& parameterId,
                            float normalizedValue,
                            const juce::RangedAudioParameter* parameter = nullptr);
    juce::StringArray getExclusiveListenGroup(const juce::String& parameterId) const;
    juce::RangedAudioParameter* findParameter(const juce::String& parameterId) noexcept;
    bool applyParameterMessage(const juce::String& parameterId,
                               const juce::OSCArgument& value);
    bool applyModuleMessage(const juce::OSCArgument& value);
    static bool readNumericArgument(const juce::OSCArgument& argument, float& value) noexcept;
    AvaAudioProcessor& targetProcessor() noexcept { return *currentTarget; }
    const AvaAudioProcessor& targetProcessor() const noexcept { return *currentTarget; }

    AvaAudioProcessor& processor;
    juce::OSCReceiver receiver { "AVA OSC receiver" };
    juce::OSCSender sender;
    OscSettings currentSettings;
    AvaAudioProcessor* currentTarget = nullptr;
    juce::String currentInstanceName;
    juce::CriticalSection pendingSettingsLock;
    OscSettings pendingSettings;
    std::map<juce::String, float> lastSentParameterValues;
    std::map<juce::String, juce::String> lastSentModules;
    std::map<juce::String, int> lastSentSplitCounts;
    juce::String lastRoutingSignature;
    bool inputConnected = false;
    bool outputConnected = false;
    mutable std::atomic<bool> inputPortBusy { false };
    mutable std::atomic<uint32_t> lastInputPortProbeTimeMs { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscController)
};
