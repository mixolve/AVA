#pragma once
#include <JuceHeader.h>
#include <memory>

class HostTrackContext final : public juce::VST3ClientExtensions
{
public:
    struct Location
    {
        juce::String text;
        bool onItem = false;
        juce::String itemName;
    };
    HostTrackContext();
    ~HostTrackContext() override;
    void setIHostApplication(Steinberg::FUnknown*) override;
    void updateTrackProperties(const juce::AudioProcessor::TrackProperties&);
    Location refresh(); // Host API calls belong on the message thread.
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
