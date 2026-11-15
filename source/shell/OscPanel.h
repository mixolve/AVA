#pragma once

#include "Controls.h"
#include "ChoiceControl.h"
#include "TextControl.h"
#include "OscSettings.h"

#include <functional>
#include <memory>

class OscPanel final : public ControlBlockComponent
{
public:
    struct Actions
    {
        std::function<OscSettings()> getSettings;
        std::function<bool(const OscSettings&)> setSettings;
        std::function<bool()> isInputPortBusy;
    };

    explicit OscPanel(Actions);
    void resized() override;
    void refresh();
    int getPreferredHeight() const noexcept;

private:
    Actions actions;
    std::unique_ptr<LocalChoiceControl> enableControl;
    std::unique_ptr<juce::Label> statusLabel;
    std::unique_ptr<TextControl> inputPortControl, outputPortControl;
    std::unique_ptr<TextControl> outputHostControl;
};
