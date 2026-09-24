#pragma once

#include "Controls.h"
#include "OscSettings.h"

#include <functional>
#include <memory>

class OscPanel final : public juce::Component
{
public:
    struct Actions
    {
        std::function<OscSettings()> getSettings;
        std::function<bool(const OscSettings&)> setSettings;
        std::function<bool()> isInputPortBusy;
        std::function<void()> toggleParameterList;
        std::function<bool()> isParameterListVisible;
        std::function<void(juce::Component&, const juce::String&,
                           std::function<bool(const juce::String&)>)> showTextPrompt;
        std::function<void()> clearFocus;
    };

    explicit OscPanel(Actions);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    void editPort(bool inputPort);
    std::unique_ptr<BoxTextButton> makeLabel(const juce::String&);

    Actions actions;
    std::unique_ptr<BoxTextButton> enableButton;
    std::unique_ptr<BoxTextButton> listButton;
    std::unique_ptr<juce::Label> statusLabel;
    std::unique_ptr<BoxTextButton> inputPortLabel;
    std::unique_ptr<BoxTextButton> inputPortButton;
    std::unique_ptr<BoxTextButton> outputPortLabel;
    std::unique_ptr<BoxTextButton> outputPortButton;
    std::unique_ptr<BoxTextButton> outputHostLabel;
    std::unique_ptr<BoxTextButton> outputHostButton;
};
