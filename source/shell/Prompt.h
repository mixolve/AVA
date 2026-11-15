#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <vector>

class PromptComponent : public juce::Component
{
public:
    ~PromptComponent() override = default;

    // A modal popup consumes wheel input outside its own scrolling children.
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}
    virtual void focusEditor() { grabKeyboardFocus(); }
    virtual void followAnchor(juce::Component&) {}
    virtual void setResetAction(std::function<void()>) {}

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
                                                bool uppercaseInput = true, bool allowEmpty = false,
                                                juce::Slider* numericControl = nullptr,
                                                std::function<double(const juce::String&)> textToValue = {},
                                                std::function<juce::String()> valueToText = {});

std::unique_ptr<PromptComponent> makeChoicePrompt(juce::Rectangle<int> anchorBounds,
                                                  juce::StringArray choices,
                                                  int selectedIndex,
                                                  std::vector<bool> itemEnabledStates,
                                                  juce::Justification itemJustification,
                                                  std::function<void(int)> onSelect,
                                                  std::function<void()> onDismiss,
                                                  std::function<void()> onClose,
                                                  bool commitBeforeFade = false,
                                                  std::vector<bool> itemActiveStates = {},
                                                  bool compactButtons = false);

std::unique_ptr<juce::Component> makeAboutPanel(juce::String markdownText, std::function<void()> onBackgroundClick = {});
