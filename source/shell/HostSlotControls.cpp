#include "Editor.h"
#include "../routing/Panel.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"
#include "ClipboardTransfer.h"
#include "EditorStateSchema.h"
#include "../routing/State.h"
#include "PinnedRoutingOrder.h"

#include <algorithm>
#include <array>
#include <limits>

void AvaAudioProcessorEditor::clearHostSlot(const int slotIndex)
{
    if (hostSlotMoveSourceIndex == slotIndex) cancelHostSlotMove();
    audioProcessor.clearHostSlotAssignment(slotIndex);
    refreshHostSlotButtons();
}

void AvaAudioProcessorEditor::moveHostSlotAssignment(const int slotIndex, const int offset)
{
    const auto destination = slotIndex + offset;
    if (!juce::isPositiveAndBelow(slotIndex,AvaAudioProcessor::assignableHostSlotCount)
        || !juce::isPositiveAndBelow(destination,AvaAudioProcessor::assignableHostSlotCount)
        || audioProcessor.getHostSlotTargetId(slotIndex).isEmpty()) return;
    audioProcessor.swapHostSlotAssignments(slotIndex, destination);
    refreshHostSlotButtons();
    updateSectionStates();
    scheduleHistorySnapshot();
}

void AvaAudioProcessorEditor::armHostSlotMove(const int slotIndex)
{
    if (! juce::isPositiveAndBelow(slotIndex,AvaAudioProcessor::assignableHostSlotCount)
        || audioProcessor.getHostSlotTargetId(slotIndex).isEmpty())
    {
        cancelHostSlotMove();
        return;
    }
    hostSlotMoveSourceIndex = slotIndex;
    juce::Component::beginDragAutoRepeat(30);
    grabKeyboardFocus();
    updateHostSlotMoveTargets();
}

void AvaAudioProcessorEditor::updateHostSlotMoveTargets()
{
    for (int index = 0; index < static_cast<int>(hostSlotButtons.size()); ++index)
    {
        const auto target = hostSlotMoveSourceIndex >= 0 && index != hostSlotMoveSourceIndex;
        const auto source = index == hostSlotMoveSourceIndex;
        const auto assigned = audioProcessor.getHostSlotTargetId(index).isNotEmpty();
        for(auto* cell:{hostSlotNameFields[size_t(index)].get(),hostSlotButtons[size_t(index)].get()})
        {
            if(cell==nullptr)continue;
            cell->setEnabled(assigned || target);
            const auto highlighted=source || (target && bool(cell->getProperties().getWithDefault("osc.move.hovered",false)));
            if(!target)cell->getProperties().set("osc.move.hovered",false);
            cell->getProperties().set("osc.move.highlighted", highlighted);
            cell->setDragTargetHighlighted(highlighted);
            cell->setMoveTargetAvailable(target);
            cell->setInteractionOutlineEnabled(target || source);
            cell->setState(highlighted?juce::Button::buttonOver:juce::Button::buttonNormal);
        }
    }
}

void AvaAudioProcessorEditor::cancelHostSlotMove()
{
    hostSlotMoveSourceIndex = -1;
    juce::Component::beginDragAutoRepeat(0);
    updateHostSlotMoveTargets();
}

void AvaAudioProcessorEditor::applyHostSlotMove(const int destinationIndex)
{
    const auto sourceIndex = hostSlotMoveSourceIndex;
    if (sourceIndex < 0) return;
    cancelHostSlotMove();
    if (sourceIndex != destinationIndex)
        moveHostSlotAssignment(sourceIndex, destinationIndex - sourceIndex);
    clearKeyboardFocus(*this);
}

void AvaAudioProcessorEditor::handleHostSlotControlDrag(const juce::MouseEvent& event, const bool released)
{
    if (hostSlotMoveSourceIndex < 0) return;
    // The captured source moves with the scrolled content. Use the pointer's
    // screen position so repeated drag events do not drift with that component.
    const auto screenPosition=event.source.getScreenPosition();
    const auto viewportPosition=filterViewport.getLocalPoint(nullptr,screenPosition);
    if(!released)
    {
        const auto edge=getUiGap(*this)*2;
        filterViewport.autoScroll(juce::roundToInt(viewportPosition.x),juce::roundToInt(viewportPosition.y),
            edge,getUiGap(*this));
    }
    const auto position=slotsContent.getLocalPoint(nullptr,screenPosition);
    const auto inViewport=filterViewport.getLocalBounds().toFloat().contains(viewportPosition);
    auto destination = -1;
    for (int index = 0; index < static_cast<int>(hostSlotButtons.size()); ++index)
    {
        auto* field = hostSlotNameFields[static_cast<size_t>(index)].get();
        auto* button = hostSlotButtons[static_cast<size_t>(index)].get();
        if (field == nullptr || button == nullptr) continue;
        const auto hovered = inViewport && index != hostSlotMoveSourceIndex
            && field->getBounds().getUnion(button->getBounds()).toFloat().contains(position);
        const auto highlighted=index == hostSlotMoveSourceIndex || hovered;
        for(auto* cell:{field,button})
        {
            cell->getProperties().set("osc.move.hovered",hovered);
            cell->getProperties().set("osc.move.highlighted", highlighted);
            cell->setDragTargetHighlighted(highlighted);
            cell->setState(highlighted?juce::Button::buttonOver:juce::Button::buttonNormal);
        }
        if (hovered) destination = index;
    }
    if (released)
    {
        if (destination >= 0) applyHostSlotMove(destination);
        else cancelHostSlotMove();
    }
}

void AvaAudioProcessorEditor::refreshHostSlotButtons()
{
    for (int slotIndex = 0; slotIndex < AvaAudioProcessor::hostAutomationSlotCount; ++slotIndex)
    {
        auto* slotNameField = hostSlotNameFields[static_cast<size_t>(slotIndex)].get();
        auto* slotButton = hostSlotButtons[static_cast<size_t>(slotIndex)].get();

        if (slotNameField == nullptr || slotButton == nullptr)
            continue;

        const auto assignment = audioProcessor.getHostSlotTargetId(slotIndex);
        slotNameField->setTooltip(audioProcessor.getHostSlotOscAddress(slotIndex));

        slotButton->setAlpha(1.0f);

        if (assignment.isEmpty())
            slotButton->setButtonText({});
        else
        {
            auto parameterName = audioProcessor.getHostSlotDisplayName(slotIndex);

            if (parameterName.isEmpty())
                parameterName = assignment;

            slotButton->setButtonText(parameterName.toUpperCase());
        }
    }
    updateHostSlotMoveTargets();
    repaint();
}

bool AvaAudioProcessorEditor::isHostSlotAssigned(const juce::String& parameterId) const
{
    // UNIT BYPASS always belongs to the root processor, including inside a
    // nested routing-instance editor. Other parameters use their own instance.
    if (parameterId == AvaAudioProcessor::paramGlobalBypassId)
        return audioProcessor.getRoutingOwner().isHostParameterAssigned(parameterId);
    return audioProcessor.isHostParameterAssigned(parameterId);
}

bool AvaAudioProcessorEditor::handleHostSlotAssignRequest(const juce::String& parameterId)
{
    const auto assigned = parameterId == AvaAudioProcessor::paramGlobalBypassId
        ? audioProcessor.getRoutingOwner().toggleHostSlotAssignment(parameterId)
        : audioProcessor.toggleHostSlotAssignment(parameterId);
    if (!assigned) return false;
    refreshHostSlotButtons();
    updateSectionStates();
    resized();
    return true;
}

