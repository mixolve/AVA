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

inline void requestPromptClose(juce::Component& owner,
                               bool& closePending,
                               std::function<void()>& onClose,
                               const std::function<void()>& onDismiss = {})
{
    if (closePending)
        return;

    closePending = true;
    auto deferredCloseCallback = std::move(onClose);
    onClose = {};

    if (onDismiss != nullptr)
        onDismiss();

    juce::MessageManager::callAsync([safeOwner = juce::Component::SafePointer<juce::Component>(&owner),
                                     callback = std::move(deferredCloseCallback)]() mutable
                                    {
                                        if (safeOwner == nullptr || callback == nullptr)
                                            return;

                                        callback();
                                    });
}

std::unique_ptr<PromptComponent> makeTextPrompt(juce::String currentText,
                                                std::function<bool(const juce::String&)> onCommit,
                                                juce::Rectangle<int> anchorBounds,
                                                std::function<void()> onDismiss,
                                                std::function<void()> onClose,
                                                bool uppercaseInput = true, bool allowEmpty = false);

std::unique_ptr<PromptComponent> makeChoicePrompt(juce::Rectangle<int> anchorBounds,
                                                  juce::StringArray choices,
                                                  int selectedIndex,
                                                  std::vector<bool> itemEnabledStates,
                                                  juce::Justification itemJustification,
                                                  std::function<void(int)> onSelect,
                                                  std::function<void()> onDismiss,
                                                  std::function<void()> onClose,
                                                  bool commitBeforeFade = false);

std::unique_ptr<PromptComponent> makeInfoPrompt(juce::String markdownText,
                                                std::function<void()> onClose);
