#include "../shell/Editor.h"
#include "../shell/Controls.h"

void AvaAudioProcessorEditor::setReturnToRoutingAction(std::function<void()> action)
{
    returnToRoutingAction = std::move(action);
}

void AvaAudioProcessorEditor::openRoutingInstance(const int instanceId)
{
    if (audioProcessor.isRoutingInstance())
        return;

    // The selected node can also be clicked from ROUTING while already open.
    // Avoid reconstructing the same editor, which causes an unnecessary flash.
    if (activeInstanceId == instanceId && activeInstanceEditor != nullptr)
        return;

    // Changing the selected editor is presentation-only. The runtime already
    // owns all live routing instances; topology edits and state restoration
    // synchronise it separately. Re-synchronising here rebuilt the render
    // graph on every instance switch, triggering the audio transition's
    // fade-out / warmup / fade-in even though no DSP settings had changed.
    auto instance = audioProcessor.getRoutingInstanceHandle(instanceId);
    if (instance == nullptr)
        return;

    // Keep the current editor visible while constructing and laying out its
    // replacement. Previously the old editor was destroyed first, exposing
    // the root page for a frame and then resizing the newly created page.
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

    // Attach it hidden so it resolves the root UNIT settings and can finish
    // layout at the final window dimensions before any of it is painted.
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

    // Destroy the old component only after the new page is ready to paint.
    previousEditor.reset();
    previousProcessor.reset();
    updateSectionStates();
    resized();
    refreshPinnedRoutingStrip();
    if (pinnedRoutingViewport.isVisible()) pinnedRoutingViewport.toFront(false);
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
