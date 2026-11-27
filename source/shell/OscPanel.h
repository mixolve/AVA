#pragma once

#include "Controls.h"
#include "Editor.h"
#include "ChoiceControl.h"
#include "TextControl.h"
#include "OscSettings.h"

#include <array>
#include <functional>
#include <memory>

// Owns the OSC controls; their visible parent is the flat UNIT row.
class OscPanel final : public juce::Component
{
public:
    struct Actions
    {
        std::function<OscSettings()> getSettings;
        std::function<bool(const OscSettings&)> setSettings;
        std::function<bool()> isInputPortBusy;
    };

    explicit OscPanel(Actions);
    void refresh();
    std::array<juce::Component*, 5> getControls() const noexcept;

private:
    Actions actions;
    std::unique_ptr<LocalChoiceControl> enableControl;
    std::unique_ptr<juce::Label> statusLabel;
    std::unique_ptr<TextControl> inputPortControl, outputPortControl;
    std::unique_ptr<TextControl> outputHostControl;
};
