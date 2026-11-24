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

void AvaAudioProcessorEditor::setupShellControls()
{
    // This control is plugin-wide even inside an embedded routing-instance editor.
    // Binding it to the instance APVTS would bypass only the currently edited node.
    // Per-instance bypass remains available in the routing panel.
    globalBypassButton = std::make_unique<ParameterControl>(
        audioProcessor.getRoutingOwner().getValueTreeState(),
        AvaAudioProcessor::paramGlobalBypassId, "BYPASS", 0);
    globalBypassButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::paramGlobalBypassId);
    globalBypassButton->setBottomIndicatorsVisible(true, false);
    bypassCompensationControl = std::make_unique<ParameterControl>(
        audioProcessor.getRoutingOwner().getValueTreeState(),
        AvaAudioProcessor::paramBypassCompensationId, "COMPEN\nSATION", 2);
    bypassCompensationControl->getProperties().set(juce::Identifier("oscParameterId"),
        AvaAudioProcessor::paramBypassCompensationId);
    // COMPENSATION is not assignable to an SL slot from UNIT / MAIN.
    // Keep all other control behaviour and indicator policies unchanged.
    bypassCompensationControl->setBottomIndicatorsVisible(false, true);
    unitMainContent.addAndMakeVisible(*bypassCompensationControl);

    globalBypassButton->onValueChanged = [this] {
        const auto bypassActive = globalBypassButton->getValue() >= 0.5;
        globalBypassButton->setSelectionHighlighted(bypassActive);
    };
    globalBypassButton->onValueChanged();
    unitMainContent.addAndMakeVisible(*globalBypassButton);

    undoButton = std::make_unique<PushControl>("UNDO");
    undoButton->setBottomIndicatorsVisible(false, false);
    undoButton->onTrigger = [this]
    {
        performHistoryStep(HistoryAction::undo);
        clearKeyboardFocus(*this);
    };
    unitMainContent.addAndMakeVisible(*undoButton);

    redoButton = std::make_unique<PushControl>("REDO");
    redoButton->setBottomIndicatorsVisible(false, false);
    redoButton->onTrigger = [this]
    {
        performHistoryStep(HistoryAction::redo);
        clearKeyboardFocus(*this);
    };
    unitMainContent.addAndMakeVisible(*redoButton);

    // A/B is a persistent two-state choice, not a momentary action button.
    abControl = std::make_unique<LocalChoiceControl>("A/B", juce::StringArray { "OFF", "ON" },
        audioProcessor.getABCompareActiveSlot());
    abControl->setName("ab");
    abControl->setBottomIndicatorsVisible(false, false);
    abControl->onValueChanged = [this]
    {
        if (abControl->getSelectedChoiceIndex() != audioProcessor.getABCompareActiveSlot())
            switchABState();
    };
    unitMainContent.addAndMakeVisible(*abControl);

    // Copy the current live A/B state into the opposite snapshot. This is a
    // momentary action (commit on release), not a second A/B selection toggle.
    abCopyButton = std::make_unique<PushControl>("COPY\nA TO B");
    abCopyButton->setBottomIndicatorsVisible(false, false);
    abCopyButton->onTrigger = [this]
    {
        captureCurrentABState();
        const auto activeSlot = audioProcessor.getABCompareActiveSlot();
        audioProcessor.setABCompareSnapshot(1 - activeSlot,
                                            audioProcessor.getABCompareSnapshot(activeSlot));
        clearKeyboardFocus(*this);
    };
    unitMainContent.addAndMakeVisible(*abCopyButton);

    unitCopyButton = std::make_unique<PushControl>("COPY");
    unitCopyButton->setBottomIndicatorsVisible(false, false);
    unitCopyButton->onTrigger = [owner = &audioProcessor.getRoutingOwner()]
    {
        ava::clipboard::copyPlugin(*owner);
    };
    unitMainContent.addAndMakeVisible(*unitCopyButton);

    unitPasteButton = std::make_unique<PushControl>("PASTE");
    unitPasteButton->setBottomIndicatorsVisible(false, false);
    unitPasteButton->onTrigger = [owner = &audioProcessor.getRoutingOwner()]
    {
        // Restore may replace editor objects; defer until the click returns.
        juce::MessageManager::callAsync([owner] { ava::clipboard::pastePlugin(*owner); });
    };
    unitMainContent.addAndMakeVisible(*unitPasteButton);

    unitZoomControl = std::make_unique<LocalChoiceControl>("ZOOM", shell_editor_ui::zoomChoices(),
        shell_editor_ui::zoomChoiceIndex(shell_editor_ui::zoomStep(getRootWindowState())));
    unitZoomControl->setBottomIndicatorsVisible(false, false);
    unitZoomControl->onValueChanged = [this]
    {
        getRootWindowState().setProperty(shell_editor_ui::key("zoom_step"),
            shell_editor_ui::zoomStepForChoice(unitZoomControl->getSelectedChoiceIndex()), nullptr);
    };
    unitMainContent.addAndMakeVisible(*unitZoomControl);
    juce::StringArray dockSizes;
    for (int size = 1; size <= 16; ++size) dockSizes.add(juce::String(size));
    unitDockSizeControl = std::make_unique<LocalChoiceControl>("DOCK\nSIZE", dockSizes,
        shell_editor_ui::dockControlCount(getRootWindowState()) - 1);
    unitDockSizeControl->setBottomIndicatorsVisible(false, false);
    unitDockSizeControl->onValueChanged = [this]
    {
        getRootWindowState().setProperty(shell_editor_ui::key("dock_count"),
            unitDockSizeControl->getSelectedChoiceIndex() + 1, nullptr);
    };
    unitMainContent.addAndMakeVisible(*unitDockSizeControl);

    pinRoutingControl = std::make_unique<LocalChoiceControl>(
        "PIN\nINSTS", juce::StringArray { "OFF", "ON" },
        bool(getRootWindowState().getProperty(shell_editor_ui::key("pin_routing"), true)) ? 1 : 0);
    pinRoutingControl->setBottomIndicatorsVisible(false, false);
    pinRoutingControl->onValueChanged = [this]
    {
        const bool enabled = pinRoutingControl->getSelectedChoiceIndex() == 1;
        getRootWindowState().setProperty(shell_editor_ui::key("pin_routing"), enabled, nullptr);
        // PIN INSTS changes only the editor view, not the host's audio state.
        // Only the UNIT-owned strip is visible, including in nested editors.
        if (auto* root = audioProcessor.getRoutingOwner().getContentEditor())
        {
            root->refreshPinnedRoutingStrip();
            root->updatePinnedSearchPosition();
        }
        resized();
    };
    unitMainContent.addAndMakeVisible(*pinRoutingControl);

    unitSearchControl = std::make_unique<LocalChoiceControl>(
        "PIN\nSEARCH", juce::StringArray { "OFF", "ON" },
        bool(getRootWindowState().getProperty(shell_editor_ui::key("search_visible"), true)) ? 1 : 0);
    unitSearchControl->setBottomIndicatorsVisible(false, false);
    unitSearchControl->onValueChanged = [this]
    {
        auto& rootState = getRootWindowState();
        rootState.setProperty(shell_editor_ui::key("search_visible"),
            unitSearchControl->getSelectedChoiceIndex() == 1, nullptr);
        if (auto* root = audioProcessor.getRoutingOwner().getContentEditor())
        {
            root->resized();
            root->updatePinnedSearchPosition();
            if (root->activeInstanceEditor != nullptr)
                root->activeInstanceEditor->resized();
        }
    };
    unitMainContent.addAndMakeVisible(*unitSearchControl);

    unitSampleRateLabel = std::make_unique<SquareTextLabel>();
    unitSampleRateLabel->setName("sample-rate");
    unitSampleRateLabel->setColour(juce::Label::outlineColourId, uiGreyLight);
    unitSampleRateLabel->getProperties().set("multilineStatusText", true);
    unitSampleRateLabel->setJustificationType(juce::Justification::centred);
    unitSampleRateLabel->setText("—", juce::dontSendNotification);
    unitSampleRateLabel->getProperties().set("disableDockSelection", true);
    unitSampleRateLabel->setWantsKeyboardFocus(false);
    unitSampleRateLabel->setInterceptsMouseClicks(false, false);
    unitMainContent.addAndMakeVisible(*unitSampleRateLabel);

    slotsContent.onBackgroundClick = [this](const juce::MouseEvent&) { cancelHostSlotMove(); };

    for (int slotIndex = 0; slotIndex < static_cast<int>(hostSlotButtons.size()); ++slotIndex)
    {
        auto slotNameField = std::make_unique<BoxTextButton>();
        slotNameField->setComponentID("host-slot-number-" + juce::String(slotIndex + 1));
        slotNameField->setButtonText(juce::String::formatted("%02d-",slotIndex+1)+AvaAudioProcessor::getHostSlotLetterLabel(slotIndex));
        slotNameField->setTextJustification(juce::Justification::centred);
        slotNameField->setInteractionOutlineEnabled(false);
        const auto copyOscAddress = [this, slotIndex](juce::ModifierKeys modifiers)
        {
            if (!modifiers.isCtrlDown() || modifiers.isShiftDown() || modifiers.isAltDown()) return;
            const auto address = audioProcessor.getHostSlotOscAddress(slotIndex);
            if (address.isNotEmpty())
            {
                juce::SystemClipboard::copyTextToClipboard(address);
                hostSlotButtons[size_t(slotIndex)]->flashConfirmation();
            }
        };

        auto slotButton = std::make_unique<BoxTextButton>();
        slotButton->setComponentID("host-slot-name-" + juce::String(slotIndex + 1));
        slotButton->getProperties().set("textContentInset", getUiGap(*this));
        slotButton->getProperties().set("singleLineText", true);
        slotButton->setTextJustification(juce::Justification::centredLeft);
        slotButton->setButtonText({});
        slotButton->onClick = [this, slotIndex] { applyHostSlotMove(slotIndex); };
        slotButton->onModifiedClick = [this, slotIndex, copyOscAddress](juce::ModifierKeys modifiers)
        {
            if (modifiers.isCtrlDown() && modifiers.isShiftDown() && !modifiers.isAltDown())
                clearHostSlot(slotIndex);
            else
                copyOscAddress(modifiers);
        };
        slotNameField->onClick=slotButton->onClick;
        slotNameField->onModifiedClick=slotButton->onModifiedClick;
        for (auto* button : { slotNameField.get(), slotButton.get() })
        {
            button->getProperties().set("suppressPressFeedback",true);
            button->getProperties().set("textSurfaceInset",8);
            button->getProperties().set("preserveCtrlClick", true);
            button->setDragRequiresControl(false);
            button->onControlDragStarted = [this, slotIndex] { armHostSlotMove(slotIndex); };
            button->onControlDrag = [this] (const juce::MouseEvent& event, const bool released)
            {
                handleHostSlotControlDrag(event, released);
            };
        }

        slotsContent.addAndMakeVisible(*slotNameField);
        slotsContent.addAndMakeVisible(*slotButton);
        hostSlotNameFields[static_cast<size_t>(slotIndex)] = std::move(slotNameField);
        hostSlotButtons[static_cast<size_t>(slotIndex)] = std::move(slotButton);
    }

    sortFiltersButton = std::make_unique<PushControl>("ALL\nSORT");
    sortFiltersButton->getProperties().set("disableDockSelection", true);
    sortFiltersButton->setBottomIndicatorsVisible(false, false);
    sortFiltersButton->onTrigger = [this]
    {
        sortFilterSections();
        clearKeyboardFocus(*this);
    };
    filterContent.addAndMakeVisible(*sortFiltersButton);

}

