#include "../shell/Editor.h"
#include "../shell/Controls.h"
#include "../shell/ChoiceControl.h"

void AvaAudioProcessorEditor::setReturnToRoutingAction(std::function<void()> action)
{
    returnToRoutingAction = std::move(action);
}

void AvaAudioProcessorEditor::openRoutingInstance(const int instanceId)
{
    if (audioProcessor.isRoutingInstance())
        return;

    if (activeInstanceId == instanceId && activeInstanceEditor != nullptr)
        return;

    auto instance = audioProcessor.getRoutingInstanceHandle(instanceId);
    if (instance == nullptr)
        return;

    auto replacement = std::make_unique<AvaAudioProcessorEditor>(*instance, instanceId);
    replacement->setReturnToRoutingAction(
        [safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)]
        {
            juce::MessageManager::callAsync([safeEditor]
            {
                if (safeEditor != nullptr)
                    safeEditor->closeRoutingInstance();
            });
        });

    addChildComponent(*replacement);
    replacement->setBounds(getLocalBounds());

    auto previousEditor = std::move(activeInstanceEditor);
    auto previousProcessor = std::move(activeInstanceProcessor);
    activeInstanceProcessor = std::move(instance);
    activeInstanceId = instanceId;
    activeInstanceEditor = std::move(replacement);

    if (previousEditor != nullptr)
        previousEditor->setVisible(false);
    activeInstanceEditor->setVisible(true);
    activeInstanceEditor->toFront(false);

    previousEditor.reset();
    previousProcessor.reset();
    updateSectionStates();
    resized();
    refreshPinnedRoutingStrip();
    if (pinnedRoutingViewport.isVisible())
    {
        pinnedRoutingViewport.toFront(false);
        if (pinnedRoutingScrollControl != nullptr) pinnedRoutingScrollControl->toFront(false);
    }
}

void AvaAudioProcessorEditor::closeRoutingInstance()
{
    activeInstanceEditor.reset();
    activeInstanceProcessor.reset();
    activeInstanceId = 0;
    updateSectionStates();
    resized();
    refreshPinnedRoutingStrip();
}
