#include "Prompt.h"
#include "Controls.h"
#include "DisclosureAnimation.h"
#include "PopupListStyle.h"

#include <cmath>
#include <utility>

namespace
{

class ChoiceContent final : public juce::Component
{
public:
    explicit ChoiceContent(int) { setOpaque(true); }
    void paint(juce::Graphics& graphics) override { paintPluginBackground(graphics); }
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
                         bool commitBeforeFadeIn,
                         std::vector<bool> itemActiveStates,
                         bool compactButtonsIn)
        : anchorBounds(std::move(anchorBoundsIn)),
          choices(std::move(choicesIn)),
          selectedIndex(selectedIndexIn),
          itemEnabledStates(std::move(itemEnabledStatesIn)),
          itemJustification(itemJustificationIn),
          compactButtons(compactButtonsIn),
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
        choiceViewport.setScrollBarsShown(!compactButtons, compactButtons, false, false);
        choiceViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
        choiceViewport.setWantsKeyboardFocus(false);
        choiceViewport.setMouseClickGrabsKeyboardFocus(false);
        revealClip.setOpaque(true);
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
            auto button = std::make_unique<BoxTextButton>();
            button->setButtonText(choices[index]);
            button->getProperties().set("controlHelp", choices[index]
                + (itemEnabled(index) ? ". Selects this option for the open control." : ". This option is unavailable in the current context."));
            button->setTextJustification(itemJustification);
            button->setBorderVisible(true);

            button->setSelectionHighlighted(itemActiveStates.size() == static_cast<size_t>(choices.size())
                ? itemActiveStates[static_cast<size_t>(index)] : index == selectedIndex);
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
        if (reveal.value() <= 0.0 || compactButtons) return;
        juce::Graphics::ScopedSaveState saved(graphics);
        graphics.reduceClipRegion(revealClip.getBounds());
        const auto bounds = revealClip.getBounds().toFloat();
        paintUiFrame(graphics, bounds,
            uiWhite.withMultipliedAlpha(static_cast<float>(reveal.value())), 1.0f);
    }

    void resized() override
    {
        const auto visibleBounds = getLocalBounds().reduced(getUiGap(*this));
        const auto itemCount = static_cast<int>(choices.size());
        if (compactButtons)
        {
            const auto size = getCompactControlSize(*this);
            const auto gap = getUiGap(*this);
            const auto contentWidth = itemCount * size + juce::jmax(0, itemCount - 1) * gap;
            const auto barHeight = contentWidth > visibleBounds.getWidth() ? choiceViewport.getScrollBarThickness() : 0;
            panelBounds = juce::Rectangle<int>(juce::jmin(visibleBounds.getWidth(), contentWidth), size + barHeight);
            panelBounds.setCentre(anchorBounds.getCentre());
            panelBounds = panelBounds.constrainedWithin(visibleBounds);
            choiceViewport.setBounds(panelBounds.withPosition(0, 0));
            choiceContent.setSize(contentWidth, size);
            for (size_t index = 0; index < itemButtons.size(); ++index)
                itemButtons[index]->setBounds(int(index) * (size + gap), 0, size, size);
            if (!animationStarted && !visibleBounds.isEmpty())
            {
                animationStarted = true;
                reveal.setExpanded(true);
            }
            layoutReveal();
            return;
        }
        const auto itemBlockHeight = popup_list::contentHeight(itemCount, *this);
        const auto availableWidth = visibleBounds.getWidth();
        const auto visibleRows = juce::jmin(itemCount, juce::jmax(0, juce::jmax(0, visibleBounds.getHeight() - 2 * getUiGap(*this) + getUiRowGap(*this)) / (getSquareControlSize(*this) + getUiRowGap(*this))));
        const auto barWidth = visibleRows < itemCount ? choiceViewport.getScrollBarThickness() : 0;
        const auto promptWidth = juce::jlimit(1, juce::jmax(1, availableWidth),
            getSquareControlSize(*this) + 2 * getUiGap(*this) + barWidth);
        const auto promptHeight = popup_list::contentHeight(visibleRows, *this) + 2 * getUiGap(*this);
        const auto alignedItemIndex = juce::isPositiveAndBelow(selectedIndex, itemCount) ? selectedIndex : 0;
        const auto alignedItemCentreY = (alignedItemIndex * (getSquareControlSize(*this) + getUiRowGap(*this)))
            + getUiGap(*this) + (getSquareControlSize(*this) / 2);

        panelBounds = juce::Rectangle<int>(promptWidth, promptHeight);
        panelBounds.setX(anchorBounds.getX());
        panelBounds.setY(anchorBounds.getCentreY() - alignedItemCentreY);
        panelBounds = panelBounds.constrainedWithin(visibleBounds);

        choiceViewport.setBounds(panelBounds.withPosition(0, 0).reduced(getUiGap(*this)));
        choiceContent.setSize(juce::jmax(0, choiceViewport.getWidth() - barWidth),
                              juce::jmax(choiceViewport.getHeight(), itemBlockHeight));

        auto contentBounds = choiceContent.getLocalBounds();
        for (size_t index = 0; index < itemButtons.size(); ++index)
        {
            auto itemBounds = contentBounds.removeFromTop(getSquareControlSize(*this));
            itemButtons[index]->setBounds(itemBounds);
            contentBounds.removeFromTop(getUiRowGap(*this));

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
                const auto itemY = selectedIndex * (getSquareControlSize(*this) + getUiRowGap(*this));
                const auto maxOffset = juce::jmax(0, choiceContent.getHeight() - choiceViewport.getHeight());
                const auto rowsBefore = juce::jmax(0, visibleRows - 1) / 2;
                const auto targetY = itemY - rowsBefore * (getSquareControlSize(*this) + getUiRowGap(*this));
                choiceViewport.setViewPosition(0, juce::jlimit(0, maxOffset, targetY));
            }
        }

    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (! revealClip.getBounds().contains(event.getPosition()))
            cancel();
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
        revealClip.setBounds(panelBounds);
        revealClip.setAlpha(static_cast<float>(reveal.value()));
        choiceViewport.setBounds(compactButtons ? panelBounds.withPosition(0, 0)
            : panelBounds.withPosition(0, 0).reduced(getUiGap(*this)));
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
    bool compactButtons = false;
    struct Panel final : juce::Component
    {
        void paint(juce::Graphics& g) override { paintPluginBackground(g); }
    } revealClip;
    UiScrollViewport choiceViewport;
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
                                                  const bool commitBeforeFade,
                                                  std::vector<bool> itemActiveStates,
                                                  const bool compactButtons)
{
    return std::make_unique<FloatingChoicePrompt>(std::move(anchorBounds),
                                                  std::move(choices),
                                                  selectedIndex,
                                                  std::move(itemEnabledStates),
                                                  itemJustification,
                                                  std::move(onSelect),
                                                  std::move(onDismiss),
                                                  std::move(onClose), commitBeforeFade,
                                                  std::move(itemActiveStates), compactButtons);
}
