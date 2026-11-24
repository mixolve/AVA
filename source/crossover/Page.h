#pragma once

#include <JuceHeader.h>
#include "../shell/Controls.h"

#include <array>
#include <memory>
#include <vector>

class CrossoverModuleComponent;

struct InstanceMainRows
{
    std::array<juce::Component*, 3> secondary {};  // CHANNEL INPUT, OUT MUTE, CHANNEL SOLO
    std::array<juce::Component*, 6> splits {};     // XOVERS, SPLIT 1..5
    std::array<juce::Component*, 4> actions {};    // AUTO SOLO, SOLO MODE, COPY/PASTE ALL
};

class CrossoverModulePage : public ControlBlockComponent
{
public:
    CrossoverModulePage() { getProperties().set("emptyEditorBackground",true); }
    ~CrossoverModulePage() override = default;

    virtual void refreshExternalState() = 0;
    virtual int getPreferredHeight() const = 0;
    virtual std::array<juce::Component*, 2> getMainSyncControls() const { return {}; }
    virtual std::vector<juce::Component*> getInstanceControls() const { return {}; }
    virtual InstanceMainRows getInstanceMainRows() const { return {}; }
};

std::unique_ptr<CrossoverModulePage> makeCrossoverRangePage(CrossoverModuleComponent& owner,
                                                            size_t rangeIndex);
std::unique_ptr<CrossoverModulePage> makeCrossoverSettingsPage(CrossoverModuleComponent& owner);
