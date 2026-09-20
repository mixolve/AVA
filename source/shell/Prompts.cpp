#include "Editor.h"
#include "Style.h"
#include "PromptComponents.h"

#include <utility>

void AvaAudioProcessorEditor::showTextPrompt(const juce::String& currentText,
                                             std::function<bool(const juce::String&)> onCommit,
                                             juce::Rectangle<int> anchorBounds,
                                             std::function<void()> onClose,
                                             std::function<void()> onDismiss)
{
    dismissTextPrompt();

    const auto preservedFilterScrollY = filterViewport.getViewPositionY();

    auto prompt = makeTextPrompt(currentText,
                                 std::move(onCommit),
                                 anchorBounds,
                                 std::move(onDismiss),
                                 [safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this),
                                  closeCallback = std::move(onClose)]
                                 {
                                     if (safeEditor == nullptr)
                                         return;

                                     const auto closePreservedFilterScrollY = safeEditor->filterViewport.getViewPositionY();
                                     safeEditor->dismissTextPrompt();

                                     if (safeEditor->filterViewport.isVisible())
                                     {
                                         const auto maxOffset = juce::jmax(0, safeEditor->getActiveFilterContentHeight() - safeEditor->filterViewport.getHeight());
                                         safeEditor->filterViewport.setViewPosition(0, juce::jlimit(0, maxOffset, closePreservedFilterScrollY));
                                     }

                                     if (closeCallback)
                                         closeCallback();
                                 });
    auto* promptComponent = prompt.get();
    textPromptOverlay = std::move(prompt);
    addAndMakeVisible(*textPromptOverlay);
    resized();

    if (filterViewport.isVisible())
    {
        const auto maxOffset = juce::jmax(0, getActiveFilterContentHeight() - filterViewport.getHeight());
        filterViewport.setViewPosition(0, juce::jlimit(0, maxOffset, preservedFilterScrollY));
    }

    juce::MessageManager::callAsync([safePrompt = juce::Component::SafePointer<PromptComponent>(promptComponent)]
                                    {
                                        if (safePrompt != nullptr)
                                        {
                                            safePrompt->toFront(false);
                                            safePrompt->focusEditor();
                                        }
                                    });

    juce::Timer::callAfterDelay(25, [safePrompt = juce::Component::SafePointer<PromptComponent>(promptComponent)]
                                    {
                                        if (safePrompt != nullptr)
                                            safePrompt->focusEditor();
                                    });
}

void AvaAudioProcessorEditor::showChoicePrompt(const juce::Rectangle<int>& anchorBounds,
                                               const juce::StringArray& choices,
                                               int selectedIndex,
                                               std::vector<bool> itemEnabledStates,
                                               const juce::Justification itemJustification,
                                               std::function<void(int)> onSelect,
                                               std::function<void()> onClose,
                                               std::function<void()> onDismiss)
{
    const auto preservedFilterScrollY = filterViewport.getViewPositionY();
    dismissTextPrompt();

    auto prompt = makeChoicePrompt(anchorBounds,
                                   choices,
                                   selectedIndex,
                                   std::move(itemEnabledStates),
                                   itemJustification,
                                   std::move(onSelect),
                                   std::move(onDismiss),
                                   [safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this),
                                    closeCallback = std::move(onClose)]
                                   {
                                       if (safeEditor == nullptr)
                                           return;

                                       const auto closePreservedFilterScrollY = safeEditor->filterViewport.getViewPositionY();
                                       safeEditor->dismissTextPrompt();

                                       if (safeEditor->filterViewport.isVisible())
                                       {
                                           const auto maxOffset = juce::jmax(0, safeEditor->getActiveFilterContentHeight() - safeEditor->filterViewport.getHeight());
                                           safeEditor->filterViewport.setViewPosition(0, juce::jlimit(0, maxOffset, closePreservedFilterScrollY));
                                       }

                                       if (closeCallback)
                                           closeCallback();
                                   });
    auto* promptComponent = prompt.get();
    textPromptOverlay = std::move(prompt);
    addAndMakeVisible(*textPromptOverlay);
    resized();

    if (filterViewport.isVisible())
    {
        const auto maxOffset = juce::jmax(0, getActiveFilterContentHeight() - filterViewport.getHeight());
        filterViewport.setViewPosition(0, juce::jlimit(0, maxOffset, preservedFilterScrollY));
    }

    promptComponent->toFront(true);
}

void AvaAudioProcessorEditor::showInfoPrompt(const juce::String& markdownText)
{
    dismissTextPrompt();

    auto prompt = makeInfoPrompt(markdownText,
                                 [safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)]
                                 {
                                     if (safeEditor != nullptr)
                                         safeEditor->dismissTextPrompt();
                                 });
    auto* promptComponent = prompt.get();
    textPromptOverlay = std::move(prompt);
    addAndMakeVisible(*textPromptOverlay);
    resized();
    promptComponent->focusEditor();
    promptComponent->toFront(true);
}

void AvaAudioProcessorEditor::dismissTextPrompt()
{
    if (textPromptOverlay == nullptr)
        return;

    removeChildComponent(textPromptOverlay.get());
    textPromptOverlay.reset();
    clearKeyboardFocus(*this);
    repaint();
}
