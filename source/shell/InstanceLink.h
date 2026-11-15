#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <memory>
#include <vector>

class AvaAudioProcessor;

// Directed synchronisation of host UNITs and internal ROUTING INSTANCEs.
// Peer selections are saved in the owning plugin's state chunk; no audio
// buffers are shared. Only subsequent edits are sent to linked recipients.
class InstanceLinkRegistry final : private juce::Timer
{
public:
    struct Peer { uint64_t id = 0; juce::String name; bool linked = false; juce::String uuid; };

    static InstanceLinkRegistry& instance();
    ~InstanceLinkRegistry() override;
    uint64_t attach(AvaAudioProcessor& processor);
    void detach(uint64_t id);
    std::vector<Peer> peers(uint64_t source);
    bool isSendEnabled(uint64_t unit);
    void setSendEnabled(uint64_t unit, bool enabled);
    struct Activity { uint32_t sentAt = 0, receivedAt = 0; };
    Activity activity(uint64_t unit);
    bool isReceiveEnabled(uint64_t unit);
    void setReceiveEnabled(uint64_t unit, bool enabled);
    static constexpr int maxUnitLinks = 12;
    int unitLinkCount(uint64_t source);
    juce::StringArray unitLinkTargets(uint64_t source);
    void setUnitLinkCount(uint64_t source, int count);
    bool setUnitLinkTarget(uint64_t source, int index, uint64_t target);
    // Internal routing links use stable node IDs within ONE root AVA.
    // They are independent of external UNIT links and the UNIT RECEIVE toggle.
    std::vector<Peer> routingPeers(AvaAudioProcessor& root, int sourceNodeId);
    void setRoutingLinked(AvaAudioProcessor& root, int sourceNodeId, int targetNodeId, bool linked);
    void pollRouting(AvaAudioProcessor& root);
    // Dispatch runs independently of editors. Diff only changes performed
    // after linking; enabling a link never copies a complete preset.
    void poll(uint64_t source);

private:
    InstanceLinkRegistry();
    void timerCallback() override;
    struct Impl;
    // Called with the registry mutex held. The host may restore states in any
    // order; unresolved UUIDs remain stored until their instances appear.
    void reconcileSavedLinks();
    void linkUnlocked(uint64_t source, uint64_t target);
    void refreshRoutingEntries(AvaAudioProcessor& root);
    std::unique_ptr<Impl> impl;
};
