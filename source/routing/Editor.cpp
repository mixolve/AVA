#include "../shell/Editor.h"
#include "../shell/EdgeResizeHandle.h"

void AvaAudioProcessorEditor::toggleRoutingSection()
{
    routingExpanded = ! routingExpanded;
    if (routingExpanded)
    {
        hostParametersExpanded = false;
        oscExpanded = false;
    }

    storeEditorStateToValueTree();
    updateSectionStates();
    resized();
}

void AvaAudioProcessorEditor::setReturnToRoutingAction(std::function<void()> action)
{
    returnToRoutingAction = std::move(action);
}

void AvaAudioProcessorEditor::openRoutingInstance(const int instanceId)
{
    if (audioProcessor.isRoutingInstance())
        return;

    audioProcessor.synchronizeRouting();
    auto instance = audioProcessor.getRoutingInstanceHandle(instanceId);
    if (instance == nullptr)
        return;

    activeInstanceEditor.reset();
    activeInstanceProcessor = std::move(instance);
    activeInstanceId = instanceId;
    activeInstanceEditor = std::make_unique<AvaAudioProcessorEditor>(*activeInstanceProcessor);
    if (auto* handle = static_cast<EdgeResizeHandle*>(activeInstanceEditor->verticalResizeHandle.get()))
        handle->setResizeOwner(*this);
    activeInstanceEditor->setReturnToRoutingAction(
        [safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)]
        {
            juce::MessageManager::callAsync([safeEditor]
            {
                if (safeEditor != nullptr)
                    safeEditor->closeRoutingInstance();
            });
        });
    addAndMakeVisible(*activeInstanceEditor);
    resized();
}

void AvaAudioProcessorEditor::closeRoutingInstance()
{
    activeInstanceEditor.reset();
    activeInstanceProcessor.reset();
    activeInstanceId = 0;
    resized();
}
