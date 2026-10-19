#include "Editor.h"
#include "Style.h"
#include "Prompt.h"
#include "BlockArrangePrompt.h"

#include <utility>

void AvaAudioProcessorEditor::showTextPrompt(const juce::String& currentText,
                                             std::function<bool(const juce::String&)> onCommit,
                                             juce::Rectangle<int> anchorBounds,
                                             std::function<void()> onClose,
                                             std::function<void()> onDismiss,
                                             const bool uppercaseInput)
{
    dismissTextPrompt();

    const auto preservedFilterScrollY = filterViewport.getViewPositionY();

    auto prompt = makeTextPrompt(currentText,
                                 std::move(onCommit),
                                 anchorBounds,
                                 std::move(onDismiss),
                                 makeScrollPreservingPromptClose(*this, std::move(onClose)),
                                 uppercaseInput);
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
                                               std::function<void()> onDismiss,
                                               const bool commitBeforeFade)
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
                                   makeScrollPreservingPromptClose(*this, std::move(onClose)),
                                   commitBeforeFade);
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
    promptComponent->toFront(true);
    resized();
    promptComponent->focusEditor();
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

std::function<void()> AvaAudioProcessorEditor::makeScrollPreservingPromptClose(AvaAudioProcessorEditor& editor,
                                                                               std::function<void()> closeHandler)
{
    return [safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(&editor),
            closeCallback = std::move(closeHandler)]
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
    };
}

void AvaAudioProcessorEditor::showBlockArrangePrompt(juce::AudioProcessorValueTreeState& state, size_t range, tls::blocks::Order order,
    std::function<void(const tls::blocks::Order&)> preview,
    std::function<void(const tls::blocks::Order&)> commit, std::function<void()> dismiss)
{
    dismissTextPrompt();
    auto prompt=std::make_unique<BlockArrangePrompt>(order,std::move(preview),std::move(commit),
        std::move(dismiss),makeScrollPreservingPromptClose(*this,{}),&state,range);
    auto* component=prompt.get();
    textPromptOverlay=std::move(prompt);
    addAndMakeVisible(*textPromptOverlay);
    resized();
    component->toFront(true);
    component->focusEditor();
}

void AvaAudioProcessorEditor::showGainArrangePrompt(juce::AudioProcessorValueTreeState& state, size_t range, tls::gain::Order order,
    std::function<void(const tls::gain::Order&)> preview,
    std::function<void(const tls::gain::Order&)> commit, std::function<void()> dismiss)
{
    dismissTextPrompt();
    auto prompt=std::make_unique<GainArrangePrompt>(order,std::move(preview),std::move(commit),
        std::move(dismiss),makeScrollPreservingPromptClose(*this,{}),&state,range);
    auto* component=prompt.get();
    textPromptOverlay=std::move(prompt);
    addAndMakeVisible(*textPromptOverlay);
    resized();
    component->toFront(true);
    component->focusEditor();
}
