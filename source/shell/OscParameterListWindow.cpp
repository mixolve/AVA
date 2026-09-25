#include "Editor.h"
#include "OscListWindowAttachment.h"

#include "Style.h"
#include "Controls.h"
#include "PromptComponents.h"
#include "../crossover/Component.h"
#include "OscPanel.h"
#include "../routing/State.h"

#include <algorithm>
#include <utility>

namespace
{
class OscParameterListContent final : public juce::Component,
                                      private juce::TableListBoxModel,
                                      private juce::Timer
{
    class CentredHeaderLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawTableHeaderBackground(juce::Graphics& graphics,
                                       juce::TableHeaderComponent& header) override
        {
            graphics.fillAll(header.findColour(juce::TableHeaderComponent::backgroundColourId));
        }

        void drawTableHeaderColumn(juce::Graphics& graphics,
                                   juce::TableHeaderComponent& header,
                                   const juce::String& columnName,
                                   int columnId,
                                   int width,
                                   int height,
                                   bool,
                                   bool,
                                   int) override
        {
            graphics.setColour(header.findColour(juce::TableHeaderComponent::textColourId));
            graphics.setFont(makeUiFont());
            graphics.drawFittedText(columnName,
                                    juce::Rectangle<int> { 4, 0, juce::jmax(0, width - 8), height },
                                    juce::Justification::centred,
                                    1);
            graphics.setColour(uiGreyLight);
            if (columnId == nameColumn)
                graphics.fillRect(width - 1, 0, 1, height);
            graphics.fillRect(0, height - 1, width, 1);
        }
    };

public:
    OscParameterListContent(AvaAudioProcessor& processorIn, juce::LookAndFeel& ownerLookAndFeel)
        : processor(processorIn),
          inputListener(*this)
    {
        setOpaque(true);
        addMouseListener(&inputListener, true);

        instanceSelector.setLookAndFeel(&ownerLookAndFeel);
        instanceSelector.setEditableText(false);
        instanceSelector.setJustificationType(juce::Justification::centred);
        instanceSelector.setPopupMenuTextJustification(juce::Justification::centred);
        instanceSelector.setColour(juce::ComboBox::backgroundColourId, uiGreyDark);
        instanceSelector.setColour(juce::ComboBox::outlineColourId, uiGreyLight);
        instanceSelector.setColour(juce::ComboBox::textColourId, uiWhite);
        instanceSelector.setColour(juce::ComboBox::arrowColourId, uiWhite);
        instanceSelector.setColour(juce::ComboBox::buttonColourId, uiGreyDark);
        instanceSelector.setWantsKeyboardFocus(false);
        instanceSelector.setMouseClickGrabsKeyboardFocus(false);
        instanceSelector.setPromptStylePopupEnabled(true);
        instanceSelector.setChoicePromptPresenter(
            [this](const juce::StringArray& choices,
                   int selectedIndex,
                   std::vector<bool> itemEnabledStates,
                   juce::Justification itemJustification,
                   std::function<void(int)> onSelect)
            {
                choicePromptOverlay.reset();
                choicePromptOverlay = makeChoicePrompt(instanceSelector.getBounds(),
                                                       choices,
                                                       selectedIndex,
                                                       std::move(itemEnabledStates),
                                                       itemJustification,
                                                       std::move(onSelect),
                                                       {},
                                                       [safeThis = juce::Component::SafePointer<OscParameterListContent>(this)]
                                                       {
                                                           if (safeThis != nullptr)
                                                               safeThis->choicePromptOverlay.reset();
                                                       });
                addAndMakeVisible(*choicePromptOverlay);
                choicePromptOverlay->setBounds(getLocalBounds());
                choicePromptOverlay->toFront(false);
            });
        instanceSelector.onChange = [this] { refreshParameters(); };
        addAndMakeVisible(instanceSelector);

        table.setModel(this);
        table.setRowHeight(rowHeight);
        table.setOutlineThickness(frameLineThickness);
        table.setMultipleSelectionEnabled(false);
        table.setColour(juce::ListBox::backgroundColourId, uiBlack);
        table.setColour(juce::ListBox::outlineColourId, uiGrey500);
        table.getViewport()->setScrollBarsShown(false, false, true, false);

        auto& header = table.getHeader();
        header.setColour(juce::TableHeaderComponent::backgroundColourId, uiBlack);
        header.setColour(juce::TableHeaderComponent::textColourId, uiWhite);
        header.setColour(juce::TableHeaderComponent::outlineColourId, uiGrey500);
        header.setColour(juce::TableHeaderComponent::highlightColourId, uiBlack);
        header.setLookAndFeel(&headerLookAndFeel);
        header.setInterceptsMouseClicks(true, true);
        constexpr int columnFlags = juce::TableHeaderComponent::visible;
        header.addColumn("NAME", nameColumn, 1, 1, 10000, columnFlags);
        header.addColumn("VALUES", acceptedValuesColumn, 1, 1, 10000, columnFlags);

        addAndMakeVisible(table);

        refreshParameters();
        startTimerHz(4);
    }

    ~OscParameterListContent() override
    {
        stopTimer();
        choicePromptOverlay.reset();
        instanceSelector.setLookAndFeel(nullptr);
        removeMouseListener(&inputListener);
        table.getHeader().setLookAndFeel(nullptr);
        table.setModel(nullptr);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(uiBlack);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(contentInset);
        instanceSelector.setBounds(bounds.removeFromTop(rowHeight));
        bounds.removeFromTop(uiGap);
        table.setBounds(bounds);
        if (choicePromptOverlay != nullptr)
            choicePromptOverlay->setBounds(getLocalBounds());
    }

    int getPreferredWindowWidth() const noexcept
    {
        return preferredWindowWidth;
    }

    void paintOverChildren(juce::Graphics& graphics) override
    {
        graphics.setColour(uiGreyLight);
        graphics.drawRect(getLocalBounds(), frameLineThickness);
    }

private:
    class InputListener final : public juce::MouseListener
    {
    public:
        explicit InputListener(OscParameterListContent& ownerIn) : owner(ownerIn) {}

        void mouseDown(const juce::MouseEvent& event) override { owner.handleMouseDown(event); }
        void mouseDrag(const juce::MouseEvent& event) override { owner.handleMouseDrag(event); }
        void mouseUp(const juce::MouseEvent& event) override { owner.handleMouseUp(event); }

    private:
        OscParameterListContent& owner;
    };

    void timerCallback() override
    {
        refreshParameters();
    }

    void refreshParameters()
    {
        const auto routing = ava::routing::readState(processor.getValueTreeState().state);
        std::vector<int> currentIds;
        juce::StringArray currentLabels;
        for (const auto& node : routing.nodes)
        {
            currentIds.push_back(node.id);
            const auto handle = node.id == routing.rootInstanceId
                ? std::shared_ptr<AvaAudioProcessor> {}
                : processor.getRoutingInstanceHandle(node.id);
            const auto* instance = node.id == routing.rootInstanceId ? &processor : handle.get();
            const auto moduleId = instance == nullptr
                ? juce::String()
                : juce::String(AvaAudioProcessor::stateIdForModule(instance->getActiveModule()));
            const auto moduleName = moduleId.isEmpty() ? juce::String("CROSSOVER") : moduleId.toUpperCase();
            currentLabels.add(ava::routing::getDisplayName(routing, node.id)
                              + " " + juce::String::charToString(0x2014) + " " + moduleName);
        }

        const auto selectorChanged = currentIds != instanceIds || currentLabels != instanceLabels;
        if (selectorChanged)
        {
            const auto selectedId = instanceSelector.getSelectedId();
            instanceIds = std::move(currentIds);
            instanceLabels = std::move(currentLabels);
            instanceSelector.clear(juce::dontSendNotification);
            for (size_t index = 0; index < instanceIds.size(); ++index)
                instanceSelector.addItem(instanceLabels[static_cast<int>(index)], instanceIds[index]);
            const auto selected = std::find(instanceIds.begin(), instanceIds.end(), selectedId);
            instanceSelector.setSelectedId(selected == instanceIds.end() ? routing.rootInstanceId : selectedId,
                                           juce::dontSendNotification);
        }

        const auto id = instanceSelector.getSelectedId();
        if (id == 0)
            return;
        const auto handle = id == routing.rootInstanceId
            ? std::shared_ptr<AvaAudioProcessor> {}
            : processor.getRoutingInstanceHandle(id);
        const auto* instance = id == routing.rootInstanceId ? &processor : handle.get();
        if (instance == nullptr)
            return;

        auto refreshed = instance->getVisibleOscParameters();
        const auto prefix = makeOscAddressPrefix(ava::routing::getDisplayName(routing, id));
        for (auto& parameter : refreshed)
            parameter.address = prefix + parameter.internalName;

        if (refreshed == parameters)
        {
            if (selectorChanged)
                updateColumnWidths();
            return;
        }

        parameters = std::move(refreshed);
        updateColumnWidths();
        pressedRow = -1;
        copiedRow = -1;
        ++longPressGeneration;
        ++copiedGeneration;
        table.updateContent();
        table.repaint();
    }

    int getNumRows() override
    {
        return static_cast<int>(parameters.size());
    }

    void updateColumnWidths()
    {
        const auto font = makeUiFont();
        const auto measure = [&font] (const juce::String& text)
        {
            return juce::GlyphArrangement::getStringWidthInt(font, text) + cellHorizontalPadding;
        };

        auto nameWidth = measure("NAME");
        auto acceptedValuesWidth = measure("VALUES");

        for (const auto& parameter : parameters)
        {
            nameWidth = juce::jmax(nameWidth, measure(parameter.internalName));
            acceptedValuesWidth = juce::jmax(acceptedValuesWidth, measure(parameter.acceptedValues));
        }
        for (const auto& label : instanceLabels)
            nameWidth = juce::jmax(nameWidth, measure(label) - acceptedValuesWidth);

        auto& header = table.getHeader();
        header.setColumnWidth(nameColumn, nameWidth);
        header.setColumnWidth(acceptedValuesColumn, acceptedValuesWidth);

        preferredWindowWidth = nameWidth + acceptedValuesWidth
            + (contentInset * 2) + (frameLineThickness * 2);
    }

    void paintRowBackground(juce::Graphics& graphics,
                            int rowNumber,
                            int width,
                            int height,
                            bool rowIsSelected) override
    {
        juce::ignoreUnused(rowNumber, rowIsSelected);
        graphics.fillAll(uiBlack);
        graphics.setColour(uiGreyLight);
        graphics.fillRect(0, height - 1, width, 1);
    }

    void paintCell(juce::Graphics& graphics,
                   int rowNumber,
                   int columnId,
                   int width,
                   int height,
                   bool) override
    {
        if (! juce::isPositiveAndBelow(rowNumber, static_cast<int>(parameters.size())))
            return;

        const auto& parameter = parameters[static_cast<size_t>(rowNumber)];
        const auto text = rowNumber == copiedRow && columnId == nameColumn ? juce::String("copied")
                        : columnId == nameColumn ? parameter.internalName
                        : parameter.acceptedValues;
        graphics.setColour(uiWhite);
        graphics.setFont(makeUiFont());
        graphics.drawFittedText(text,
                                juce::Rectangle<int> { uiGap, 0, juce::jmax(0, width - uiGapDouble), height },
                                juce::Justification::centredLeft,
                                1,
                                1.0f);
        if (columnId == nameColumn)
        {
            graphics.setColour(uiGreyLight);
            graphics.fillRect(width - 1, 0, 1, height);
        }
    }

    int getRowAt(const juce::MouseEvent& event)
    {
        if (choicePromptOverlay != nullptr
            && (event.eventComponent == choicePromptOverlay.get()
                || choicePromptOverlay->isParentOf(event.eventComponent)))
            return -1;

        const auto relative = event.getEventRelativeTo(&table);

        if (! table.getLocalBounds().contains(relative.getPosition()))
            return -1;

        if (table.getHeader().getBounds().contains(relative.getPosition()))
            return -1;

        return table.getRowContainingPosition(relative.x, relative.y);
    }

    void handleMouseDown(const juce::MouseEvent& event)
    {
        if (! event.mods.isLeftButtonDown())
            return;

        if (auto* topLevel = getTopLevelComponent())
            topLevel->toFront(false);

        pressedRow = getRowAt(event);
        ++longPressGeneration;

        if (pressedRow >= 0)
        {
            const auto generation = longPressGeneration;
            juce::Timer::callAfterDelay(longPressDelayMs,
                                        [safeThis = juce::Component::SafePointer<OscParameterListContent>(this), generation]
                                        {
                                            if (safeThis != nullptr)
                                                safeThis->performLongPress(generation);
                                        });
        }
    }

    void handleMouseDrag(const juce::MouseEvent& event)
    {
        if (pressedRow >= 0 && event.getDistanceFromDragStart() >= longPressDragTolerance)
        {
            pressedRow = -1;
            ++longPressGeneration;
        }
    }

    void handleMouseUp(const juce::MouseEvent&)
    {
        pressedRow = -1;
        ++longPressGeneration;
    }

    void performLongPress(const uint32_t generation)
    {
        if (generation != longPressGeneration
            || ! juce::isPositiveAndBelow(pressedRow, static_cast<int>(parameters.size())))
            return;

        const auto row = pressedRow;
        const auto& parameter = parameters[static_cast<size_t>(row)];

        if (parameter.address.isEmpty())
            return;

        juce::SystemClipboard::copyTextToClipboard(parameter.address);
        copiedRow = row;
        table.repaintRow(row);

        const auto copiedToken = ++copiedGeneration;
        juce::Timer::callAfterDelay(copiedDisplayMs,
                                    [safeThis = juce::Component::SafePointer<OscParameterListContent>(this), copiedToken, row]
                                    {
                                        if (safeThis == nullptr || copiedToken != safeThis->copiedGeneration)
                                            return;

                                        if (safeThis->copiedRow == row)
                                        {
                                            safeThis->copiedRow = -1;
                                            safeThis->table.repaintRow(row);
                                        }
                                    });
    }

    static constexpr int cellHorizontalPadding = uiGapDouble;
    static constexpr int contentInset = uiGap + frameLineThickness;
    static constexpr int nameColumn = 1;
    static constexpr int acceptedValuesColumn = 2;
    static constexpr int longPressDelayMs = 500;
    static constexpr int copiedDisplayMs = 500;
    static constexpr int longPressDragTolerance = 4;

    AvaAudioProcessor& processor;
    NoTickComboBox instanceSelector;
    std::unique_ptr<PromptComponent> choicePromptOverlay;
    std::vector<int> instanceIds;
    juce::StringArray instanceLabels;
    std::vector<OscParameterInfo> parameters;
    CentredHeaderLookAndFeel headerLookAndFeel;
    juce::TableListBox table;
    InputListener inputListener;
    int pressedRow = -1;
    int copiedRow = -1;
    uint32_t longPressGeneration = 0;
    uint32_t copiedGeneration = 0;
    int preferredWindowWidth = 900;
};


class OscParameterListWindow final : public juce::DocumentWindow,
                                     private juce::Timer
{
public:
    OscParameterListWindow(AvaAudioProcessor& processor,
                           juce::LookAndFeel& ownerLookAndFeel,
                           std::function<void()> closeCallbackIn)
        : DocumentWindow({}, uiBlack, 0, false),
          closeCallback(std::move(closeCallbackIn))
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setDropShadowEnabled(false);
        setResizable(false, false);
        auto* content = new OscParameterListContent(processor, ownerLookAndFeel);
        setContentOwned(content, true);
        setSize(content->getPreferredWindowWidth(), 1);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setBroughtToFrontOnMouseClick(true);
        addToDesktop();
    }

    ~OscParameterListWindow() override
    {
        stopTimer();
        osc_list_window::detachFromOwner(*this);
    }

    juce::BorderSize<int> getBorderThickness() const override
    {
        return {};
    }

    juce::BorderSize<int> getContentComponentBorder() const override
    {
        return {};
    }

    int getDesktopWindowStyleFlags() const override
    {
        return (juce::DocumentWindow::getDesktopWindowStyleFlags()
                & ~juce::ComponentPeer::windowAppearsOnTaskbar)
            | juce::ComponentPeer::windowIgnoresKeyPresses;
    }

    void showFor(juce::Component& owner)
    {
        ownerComponent = &owner;
        const auto ownerBounds = owner.getScreenBounds();
        auto targetBounds = getBounds();
        targetBounds.setHeight(ownerBounds.getHeight());
        targetBounds.setPosition(ownerBounds.getRight() + uiGap, ownerBounds.getY());

        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(ownerBounds))
            targetBounds = targetBounds.constrainedWithin(display->userArea);
        targetBounds.setHeight(ownerBounds.getHeight());

        ownerOffset = targetBounds.getPosition() - ownerBounds.getPosition();
        setBounds(targetBounds);
        osc_list_window::attachToOwner(*this, owner);
        setVisible(true);
        toFront(false);
        startTimerHz(sizeRefreshRateHz);
    }

    void closeButtonPressed() override
    {
        auto deferredClose = std::move(closeCallback);
        juce::MessageManager::callAsync([callback = std::move(deferredClose)]() mutable
        {
            if (callback != nullptr)
                callback();
        });
    }

private:
    void timerCallback() override
    {
        syncWindowSizeToOwner();
    }

    void syncWindowSizeToOwner()
    {
        const auto* content = dynamic_cast<OscParameterListContent*>(getContentComponent());
        if (content == nullptr || ownerComponent == nullptr)
            return;

        const auto targetWidth = content->getPreferredWindowWidth();
        const auto targetHeight = ownerComponent->getHeight();
        auto targetBounds = getBounds().withSize(targetWidth, targetHeight);
        targetBounds.setPosition(ownerComponent->getScreenBounds().getPosition() + ownerOffset);
        if (getBounds() != targetBounds)
            setBounds(targetBounds);
    }

    static constexpr int sizeRefreshRateHz = 60;

    juce::Component::SafePointer<juce::Component> ownerComponent;
    juce::Point<int> ownerOffset;
    std::function<void()> closeCallback;
};
}

void AvaAudioProcessorEditor::showOscParameterList()
{
    if (oscParameterListWindow != nullptr)
    {
        oscParameterListWindow.reset();

        if (oscPanel != nullptr)
            oscPanel->refresh();

        return;
    }

    auto window = std::make_unique<OscParameterListWindow>(
        audioProcessor.getOscOwner(),
        getLookAndFeel(),
        [safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)]
        {
            if (safeEditor == nullptr)
                return;

            safeEditor->oscParameterListWindow.reset();

            if (safeEditor->oscPanel != nullptr)
                safeEditor->oscPanel->refresh();
        });
    window->showFor(*this);
    oscParameterListWindow = std::move(window);

    if (oscPanel != nullptr)
        oscPanel->refresh();
}
