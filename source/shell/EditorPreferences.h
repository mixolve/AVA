#pragma once

#include <JuceHeader.h>
#include <atomic>

class ScopedAtomicFlag {
public:
    ScopedAtomicFlag(std::atomic<bool>& flagIn, bool value) : flag(flagIn), previous(flag.exchange(value)) {}
    ~ScopedAtomicFlag() { flag.store(previous); }
private:
    std::atomic<bool>& flag;
    bool previous;
};

void preserveEditorWindowState(juce::XmlElement& targetStateElement,
                               const juce::ValueTree& sourceState);

// View navigation is persisted in projects, but is not a parameter edit.
bool isEditorViewProperty(const juce::Identifier& property);
bool historySnapshotsMatchIgnoringViews(const juce::MemoryBlock& a, const juce::MemoryBlock& b);
void preserveEditorViewState(juce::XmlElement& target, const juce::XmlElement& current);
