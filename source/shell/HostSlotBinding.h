#pragma once

#include "Processor.h"
#include "../modules/eql/FilterSupport.h"

namespace shell_host_slot
{
// Expose a state-backed boolean through the shared slot/OSC parameter path.
// Routing targets resolve their processor by ID instead of retaining a removable child.
class DelegatedBoolParameter final : public juce::AudioParameterBool
{
public:
    DelegatedBoolParameter(juce::String id, juce::String parameterName, std::function<float()> readIn, std::function<void(bool)> writeIn)
        : AudioParameterBool(juce::ParameterID { id, 1 }, parameterName, false),
          read(std::move(readIn)), write(std::move(writeIn)) {}
    float getValue() const override { return juce::jmax(0.0f, read()); }
    void setValue(float value) override { write(value >= 0.5f); }
private:
    std::function<float()> read;
    std::function<void(bool)> write;
};

}

// A slot observes the actual source parameter, including UI gestures. Values
// still pass through the single synchronizer; listener callbacks never wait on
// the processing lock, including while a module is being detached.
struct AvaAudioProcessor::HostSlotBinding final : juce::AudioProcessorParameter::Listener
{
    HostSlotBinding(AvaAudioProcessor& ownerIn, int slotIn) : owner(ownerIn), slot(slotIn) {}
    ~HostSlotBinding() override { bind(nullptr); }
    void bind(juce::RangedAudioParameter* source)
    {
        if (source == parameter) return;
        if (parameter != nullptr) parameter->removeListener(this);
        pendingGestures.store(0, std::memory_order_release);
        pendingEnd = false;
        if (gestureActive)
        {
            gestureActive = false;
            owner.parameters.getParameter(owner.hostSlotParameterIds[size_t(slot)])->endChangeGesture();
        }
        parameter = source;
        domain = {};
        // Binding a different source must not inherit the old target's echo guard.
        owner.hostDrivenTargetValues[size_t(slot)].store(-1.0f, std::memory_order_release);
        if (parameter != nullptr) parameter->addListener(this);
    }
    float toSlot(float sourceValue) const noexcept {
        const auto view = domain.presentation();
        if (parameter == nullptr || view.limits.isEmpty()) return sourceValue;
        const auto display = view.toDisplay(parameter->convertFrom0to1(sourceValue));
        return float(juce::jlimit(0.0, 1.0, (display - view.limits.getStart()) / view.limits.getLength()));
    }
    float fromSlot(float value) const noexcept {
        const auto view = domain.presentation();
        if (parameter == nullptr || view.limits.isEmpty()) return value;
        return parameter->convertTo0to1(float(view.toPlain(view.limits.getStart() + value * view.limits.getLength())));
    }
    FilterParameterDomain domain;
    void parameterValueChanged(int, float) override { owner.syncHostSlotValues(); }
    void parameterGestureChanged(int, bool active) override
    {
        const juce::ScopedTryLock lock(owner.processingLock);
        if (lock.isLocked() && (owner.synchronisingHostSlots
            || owner.suppressHostStateNotifications.load(std::memory_order_relaxed))) return;
        pendingGestures.fetch_or(active ? 1u : 2u, std::memory_order_acq_rel);
        owner.syncHostSlotValues();
    }
    void beginPendingGesture()
    {
        const auto pending = pendingGestures.exchange(0, std::memory_order_acq_rel);
        if ((pending & 1u) != 0 && !gestureActive)
        {
            gestureActive = true;
            owner.parameters.getParameter(owner.hostSlotParameterIds[size_t(slot)])->beginChangeGesture();
        }
        pendingEnd = (pending & 2u) != 0;
    }
    void endPendingGesture()
    {
        if (!pendingEnd || !gestureActive) return;
        pendingEnd = gestureActive = false;
        owner.parameters.getParameter(owner.hostSlotParameterIds[size_t(slot)])->endChangeGesture();
    }
    AvaAudioProcessor& owner;
    const int slot;
    juce::RangedAudioParameter* parameter = nullptr;
    std::atomic<unsigned> pendingGestures {0};
    bool gestureActive = false, pendingEnd = false;
};
