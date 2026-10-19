#include "../shell/Editor.h"
#include "../shell/EdgeResizeHandle.h"
#include "../shell/WindowDragHandle.h"

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
    activeInstanceEditor = std::make_unique<AvaAudioProcessorEditor>(*activeInstanceProcessor, instanceId);
    activeInstanceEditor->setAnimationSettings(animationSettings);
    if (auto* handle = static_cast<EdgeResizeHandle*>(activeInstanceEditor->verticalResizeHandle.get()))
        handle->setResizeOwner(*this);
    if (auto* handle = static_cast<EdgeResizeHandle*>(activeInstanceEditor->horizontalResizeHandle.get()))
        handle->setResizeOwner(*this);
    if (auto* handle = static_cast<EdgeResizeHandle*>(activeInstanceEditor->topResizeHandle.get()))
        handle->setResizeOwner(*this);
    if (auto* handle = static_cast<EdgeResizeHandle*>(activeInstanceEditor->leftResizeHandle.get()))
        handle->setResizeOwner(*this);
    if (auto* handle = static_cast<EdgeResizeHandle*>(activeInstanceEditor->cornerResizeHandle.get()))
        handle->setResizeOwner(*this);
    if (auto* handle = dynamic_cast<WindowDragHandle*>(activeInstanceEditor->globalSpacer.get()))
        handle->setMoveOwner(*this);
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
