#include "Prompt.h"
#include "Controls.h"
#include "DisclosureAnimation.h"
#include "PopupListStyle.h"

#include <utility>

namespace
{
constexpr int promptItemHeight = popup_list::itemHeight;

class ChoiceContent final : public juce::Component
{
public:
    explicit ChoiceContent(const int itemCountIn) : itemCount(itemCountIn) {}

    void paint(juce::Graphics& graphics) override { graphics.fillAll(uiGreyDark); }

    void paintOverChildren(juce::Graphics& graphics) override
    {
        graphics.setColour(uiWhite);
        popup_list::paintSeparators(graphics,getWidth(),itemCount);
    }

private:
    int itemCount = 0;
};

class FloatingChoicePrompt final : public PromptComponent
{
public:
    using SelectCallback = std::function<void(int)>;
    using DismissCallback = std::function<void()>;
    using CloseCallback = std::function<void()>;

    FloatingChoicePrompt(juce::Rectangle<int> anchorBoundsIn,
                         juce::StringArray choicesIn,
                         int selectedIndexIn,
                         std::vector<bool> itemEnabledStatesIn,
                         juce::Justification itemJustificationIn,
                         SelectCallback selectCallback,
                         DismissCallback dismissCallback,
                         CloseCallback closeCallback,
                         bool commitBeforeFadeIn)
        : anchorBounds(std::move(anchorBoundsIn)),
          choices(std::move(choicesIn)),
          selectedIndex(selectedIndexIn),
          itemEnabledStates(std::move(itemEnabledStatesIn)),
          itemJustification(itemJustificationIn),
          choiceContent(choices.size()),
          onSelect(std::move(selectCallback)),
          onDismiss(std::move(dismissCallback)),
          onClose(std::move(closeCallback)), commitBeforeFade(commitBeforeFadeIn)
    {
        getProperties().set("preserveUnderlyingHover", true);
        setOpaque(false);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setInterceptsMouseClicks(true, true);
        choiceViewport.setViewedComponent(&choiceContent, false);
        choiceViewport.onWheel = [this](const juce::MouseEvent& e, const juce::MouseWheelDetails& w) { mouseWheelMove(e.getEventRelativeTo(this), w); };
        choiceViewport.setScrollBarsShown(false, false);
        choiceViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
        choiceViewport.setWantsKeyboardFocus(false);
        choiceViewport.setMouseClickGrabsKeyboardFocus(false);
        revealClip.addAndMakeVisible(choiceViewport);
        addAndMakeVisible(revealClip);
        revealClip.setInterceptsMouseClicks(false, true);
        reveal.onUpdate = [this]
        {
            layoutReveal();
            repaint();
            if (closing && ! reveal.isRunning())
                completeClose();
        };

        if (itemEnabledStates.size() != static_cast<size_t>(choices.size()))
            itemEnabledStates.assign(static_cast<size_t>(choices.size()), true);

        itemButtons.reserve(static_cast<size_t>(choices.size()));

        for (int index = 0; index < choices.size(); ++index)
        {
            auto button = std::make_unique<BoxTextButton>(uiAccent);
            button->setButtonText(choices[index]);
            button->setTextJustification(itemJustification);
            button->setBorderVisible(false);
            button->setActiveFill(index==selectedIndex);
            const auto isEnabled = itemEnabled(index);
            button->setEnabled(isEnabled);
            button->setAlpha(1.0f);
            button->onClick = [safeThis = juce::Component::SafePointer<FloatingChoicePrompt>(this), index]
            {
                if (safeThis != nullptr)
                    safeThis->choose(index);
            };
            choiceContent.addAndMakeVisible(*button);
            itemButtons.push_back(std::move(button));
        }
    }

    void paintOverChildren(juce::Graphics& graphics) override
    {
        if (reveal.value() <= 0.0) return;
        juce::Graphics::ScopedSaveState saved(graphics);
        graphics.reduceClipRegion(revealClip.getBounds());
        const auto bounds = revealClip.getBounds().toFloat();
        graphics.setColour(uiWhite.withMultipliedAlpha(static_cast<float>(reveal.value())));
        popup_list::paintFrame(graphics,bounds);
    }

    void resized() override
    {
        const auto visibleBounds = getLocalBounds().reduced(uiGap);
        const auto itemCount = static_cast<int>(choices.size());
        const auto itemBlockHeight = itemCount * promptItemHeight;
        const auto availableWidth = visibleBounds.getWidth();
        const auto promptWidth = juce::jmax(1, juce::jmin(availableWidth, anchorBounds.getWidth()));
        const auto visibleRows = juce::jmin(itemCount, juce::jmax(0, visibleBounds.getHeight() / promptItemHeight));
        const auto promptHeight = visibleRows * promptItemHeight;
        const auto alignedItemIndex = juce::isPositiveAndBelow(selectedIndex, itemCount) ? selectedIndex : 0;
        const auto alignedItemCentreY = (alignedItemIndex * promptItemHeight)
            + (promptItemHeight / 2);

        panelBounds = juce::Rectangle<int>(promptWidth, promptHeight);
        panelBounds.setX(anchorBounds.getX());
        panelBounds.setY(anchorBounds.getCentreY() - alignedItemCentreY);
        panelBounds = panelBounds.constrainedWithin(visibleBounds);

        choiceViewport.setBounds(panelBounds.withPosition(0, 0));
        choiceContent.setSize(panelBounds.getWidth(), juce::jmax(panelBounds.getHeight(), itemBlockHeight));

        auto contentBounds = choiceContent.getLocalBounds();
        for (size_t index = 0; index < itemButtons.size(); ++index)
        {
            auto itemBounds = contentBounds.removeFromTop(promptItemHeight);
            itemButtons[index]->setBounds(itemBounds);

        }

        if (! animationStarted && ! visibleBounds.isEmpty())
        {
            animationStarted = true;
            reveal.setExpanded(true);
        }
        layoutReveal();

        if (! initialScrollApplied)
        {
            initialScrollApplied = true;

            if (juce::isPositiveAndBelow(selectedIndex, static_cast<int>(itemButtons.size())))
            {
                const auto itemY = selectedIndex * promptItemHeight;
                const auto maxOffset = juce::jmax(0, choiceContent.getHeight() - choiceViewport.getHeight());
                const auto rowsBefore = juce::jmax(0, visibleRows - 1) / 2;
                const auto targetY = itemY - rowsBefore * promptItemHeight;
                choiceViewport.setViewPosition(0, juce::jlimit(0, maxOffset, targetY));
            }
        }
        choiceViewport.setViewPosition(0, juce::jlimit(0, juce::jmax(0, itemBlockHeight - promptHeight),
            (choiceViewport.getViewPositionY() / promptItemHeight) * promptItemHeight));
        layoutReveal();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (! revealClip.getBounds().contains(event.getPosition()))
            cancel();
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (! panelBounds.contains(event.getPosition()))
            return;

        const auto delta = wheel.isReversed ? wheel.deltaY : -wheel.deltaY;
        scrollRemainder += delta * (wheel.isSmooth ? 220.0f : 48.0f) / promptItemHeight;
        const auto rows = static_cast<int>(scrollRemainder);
        if (rows == 0) return;
        scrollRemainder -= static_cast<float>(rows);
        const auto maximum = juce::jmax(0, choiceContent.getHeight() - choiceViewport.getHeight());
        choiceViewport.setViewPosition(0, juce::jlimit(0, maximum,
            choiceViewport.getViewPositionY() + rows * promptItemHeight));
    }

private:
    void choose(const int index)
    {
        if (closePending || closing)
            return;

        if (! itemEnabled(index))
            return;

        if(commitBeforeFade) {
            // Keep the picker opaque until the replacement UI has been built.
            // The callback may destroy this prompt, so run off the button stack.
            reveal.snap(true);
            layoutReveal();
            closing=true;
            auto selectionCallback=std::move(onSelect);
            juce::MessageManager::callAsync([safeThis=juce::Component::SafePointer<FloatingChoicePrompt>(this),
                                            index, callback=std::move(selectionCallback)]() mutable {
                if(safeThis==nullptr) return;
                if(callback) callback(index);
                if(safeThis==nullptr) return;
                safeThis->toFront(false);
                safeThis->reveal.setExpanded(false);
                if(!safeThis->reveal.isRunning()) safeThis->completeClose();
            });
            return;
        }
        pendingChoice = index;
        requestClose();
    }

    void completeClose()
    {
        if (closePending) return;
        const auto choiceIndex = pendingChoice;
        auto deferredSelection = choiceIndex >= 0 ? std::move(onSelect) : SelectCallback {};
        onSelect = {};
        requestPromptClose(*this, closePending, onClose, onDismiss);
        // Apply only after the closing animation and removal of the old prompt.
        // Applying earlier can rebuild the editor and destroy the animated prompt.
        if (deferredSelection != nullptr)
            juce::MessageManager::callAsync([choiceIndex, callback = std::move(deferredSelection)]() mutable
            {
                callback(choiceIndex);
            });
    }

    void layoutReveal()
    {
        // All choice lists fade in place: keep rows, text and frame stationary.
        revealClip.setBounds(panelBounds);
        revealClip.setAlpha(static_cast<float>(reveal.value()));
        choiceViewport.setBounds(panelBounds.withPosition(0, 0));
    }

    void requestClose()
    {
        if (closing || closePending) return;
        closing = true;
        // Keep covering the controls until the last frame: otherwise their hover
        // state changes underneath the fading list and flashes through it.
        reveal.setExpanded(false);
        if (! reveal.isRunning())
            completeClose();
    }

    void cancel()
    {
        requestClose();
    }

    bool itemEnabled(const int index) const
    {
        if (! juce::isPositiveAndBelow(index, static_cast<int>(itemEnabledStates.size())))
            return true;

        return itemEnabledStates[static_cast<size_t>(index)];
    }

    juce::Rectangle<int> anchorBounds;
    juce::StringArray choices;
    int selectedIndex = -1;
    std::vector<bool> itemEnabledStates;
    juce::Justification itemJustification = juce::Justification::centred;
    juce::Component revealClip;
    class RowViewport final : public juce::Viewport
    {
    public:
        std::function<void(const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        { if (onWheel) onWheel(e, w); }
    } choiceViewport;
    DisclosureAnimation reveal;
    bool animationStarted = false, closing = false;
    ChoiceContent choiceContent;
    std::vector<std::unique_ptr<BoxTextButton>> itemButtons;
    SelectCallback onSelect;
    DismissCallback onDismiss;
    CloseCallback onClose;
    juce::Rectangle<int> panelBounds;
    bool initialScrollApplied = false;
    bool closePending = false;
    bool commitBeforeFade = false;
    int pendingChoice = -1;
    float scrollRemainder = 0.0f;
};
}

std::unique_ptr<PromptComponent> makeChoicePrompt(juce::Rectangle<int> anchorBounds,
                                                  juce::StringArray choices,
                                                  const int selectedIndex,
                                                  std::vector<bool> itemEnabledStates,
                                                  const juce::Justification itemJustification,
                                                  std::function<void(int)> onSelect,
                                                  std::function<void()> onDismiss,
                                                  std::function<void()> onClose,
                                                  const bool commitBeforeFade)
{
    return std::make_unique<FloatingChoicePrompt>(std::move(anchorBounds),
                                                  std::move(choices),
                                                  selectedIndex,
                                                  std::move(itemEnabledStates),
                                                  itemJustification,
                                                  std::move(onSelect),
                                                  std::move(onDismiss),
                                                  std::move(onClose),commitBeforeFade);
}
