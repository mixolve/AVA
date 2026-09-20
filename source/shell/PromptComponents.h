#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <vector>

class PromptComponent : public juce::Component
{
public:
    ~PromptComponent() override = default;

    virtual void focusEditor() { grabKeyboardFocus(); }
};

std::unique_ptr<PromptComponent> makeTextPrompt(juce::String currentText,
                                                std::function<bool(const juce::String&)> onCommit,
                                                juce::Rectangle<int> anchorBounds,
                                                std::function<void()> onDismiss,
                                                std::function<void()> onClose);

std::unique_ptr<PromptComponent> makeChoicePrompt(juce::Rectangle<int> anchorBounds,
                                                  juce::StringArray choices,
                                                  int selectedIndex,
                                                  std::vector<bool> itemEnabledStates,
                                                  juce::Justification itemJustification,
                                                  std::function<void(int)> onSelect,
                                                  std::function<void()> onDismiss,
                                                  std::function<void()> onClose);

std::unique_ptr<PromptComponent> makeInfoPrompt(juce::String markdownText,
                                                std::function<void()> onClose);
