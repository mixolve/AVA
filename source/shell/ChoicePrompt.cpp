#include "PromptComponents.h"
#include "Controls.h"

#include <utility>

namespace
{
constexpr int promptItemHeight = 30;
constexpr int promptItemGap = 0;
constexpr float promptLineThickness = 1.5f;

void fillOpaqueLine(juce::Graphics& graphics, float left, float top, float right, float bottom)
{
    const auto scale = juce::jmax(1.0f, graphics.getInternalContext().getPhysicalPixelScaleFactor());
    const auto snap = [scale](float position)
    {
        return static_cast<float>(juce::roundToInt(position * scale)) / scale;
    };
    left = snap(left);
    top = snap(top);
    right = snap(right);
    bottom = snap(bottom);
    graphics.fillRect(juce::Rectangle<float>(left, top, right - left, bottom - top));
}

float opaqueLineThickness(juce::Graphics& graphics)
{
    const auto scale = juce::jmax(1.0f, graphics.getInternalContext().getPhysicalPixelScaleFactor());
    return static_cast<float>(juce::jmax(1, juce::roundToInt(promptLineThickness * scale))) / scale;
}

class ChoiceContent final : public juce::Component
{
public:
    explicit ChoiceContent(const int itemCountIn) : itemCount(itemCountIn) {}

    void paintOverChildren(juce::Graphics& graphics) override
    {
        graphics.setColour(uiWhite);
        const auto thickness = opaqueLineThickness(graphics);
        for (int index = 1; index < itemCount; ++index)
        {
            const auto y = static_cast<float>(index * (promptItemHeight + promptItemGap));
            fillOpaqueLine(graphics, 0.0f, y - thickness, static_cast<float>(getWidth()), y);
        }
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
                         CloseCallback closeCallback)
        : anchorBounds(std::move(anchorBoundsIn)),
          choices(std::move(choicesIn)),
          selectedIndex(selectedIndexIn),
          itemEnabledStates(std::move(itemEnabledStatesIn)),
          itemJustification(itemJustificationIn),
          choiceContent(choices.size()),
          onSelect(std::move(selectCallback)),
          onDismiss(std::move(dismissCallback)),
          onClose(std::move(closeCallback))
    {
        setOpaque(false);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setInterceptsMouseClicks(true, true);
        choiceViewport.setViewedComponent(&choiceContent, false);
        choiceViewport.setScrollBarsShown(false, false);
        choiceViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
        choiceViewport.setWantsKeyboardFocus(false);
        choiceViewport.setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(choiceViewport);

        if (itemEnabledStates.size() != static_cast<size_t>(choices.size()))
            itemEnabledStates.assign(static_cast<size_t>(choices.size()), true);

        itemButtons.reserve(static_cast<size_t>(choices.size()));

        for (int index = 0; index < choices.size(); ++index)
        {
            auto button = std::make_unique<BoxTextButton>(uiAccent);
            button->setButtonText(choices[index]);
            button->setTextJustification(itemJustification);
            button->setBorderVisible(false);
            button->setFillColour(index == selectedIndex ? uiGreyLight : uiGreyDark);
            button->setInteractionFillColour(uiGreyLight);
            button->setTextColourOverride(index == selectedIndex ? uiBlack : uiWhite);
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
        const auto bounds = choiceViewport.getBounds().toFloat();
        const auto thickness = opaqueLineThickness(graphics);
        graphics.setColour(uiWhite);
        fillOpaqueLine(graphics, bounds.getX(), bounds.getY(), bounds.getRight(), bounds.getY() + thickness);
        fillOpaqueLine(graphics, bounds.getX(), bounds.getBottom() - thickness,
                       bounds.getRight(), bounds.getBottom());
        fillOpaqueLine(graphics, bounds.getX(), bounds.getY(), bounds.getX() + thickness, bounds.getBottom());
        fillOpaqueLine(graphics, bounds.getRight() - thickness, bounds.getY(),
                       bounds.getRight(), bounds.getBottom());
    }

    void resized() override
    {
        const auto visibleBounds = getVisibleBounds();
        const auto itemCount = static_cast<int>(choices.size());
        const auto itemBlockHeight = (itemCount * promptItemHeight)
            + (juce::jmax(0, itemCount - 1) * promptItemGap);
        const auto availableWidth = visibleBounds.getWidth();
        const auto promptWidth = juce::jmax(1, juce::jmin(availableWidth, anchorBounds.getWidth()));
        const auto promptHeight = juce::jmin(juce::jmax(anchorBounds.getHeight(), itemBlockHeight),
                                             visibleBounds.getHeight());
        const auto alignedItemIndex = juce::isPositiveAndBelow(selectedIndex, itemCount) ? selectedIndex : 0;
        const auto alignedItemCentreY = (alignedItemIndex * (promptItemHeight + promptItemGap))
            + (promptItemHeight / 2);

        panelBounds = juce::Rectangle<int>(promptWidth, promptHeight);
        panelBounds.setX(anchorBounds.getX());
        panelBounds.setY(anchorBounds.getCentreY() - alignedItemCentreY);
        panelBounds = panelBounds.constrainedWithin(visibleBounds);

        choiceViewport.setBounds(panelBounds);
        choiceContent.setSize(panelBounds.getWidth(), juce::jmax(panelBounds.getHeight(), itemBlockHeight));

        auto contentBounds = choiceContent.getLocalBounds();
        for (size_t index = 0; index < itemButtons.size(); ++index)
        {
            auto itemBounds = contentBounds.removeFromTop(promptItemHeight);
            itemButtons[index]->setBounds(itemBounds);

            if (index + 1 < itemButtons.size())
                contentBounds.removeFromTop(promptItemGap);
        }

        if (! initialScrollApplied)
        {
            initialScrollApplied = true;

            if (juce::isPositiveAndBelow(selectedIndex, static_cast<int>(itemButtons.size())))
            {
                const auto itemY = selectedIndex * (promptItemHeight + promptItemGap);
                const auto maxOffset = juce::jmax(0, choiceContent.getHeight() - choiceViewport.getHeight());
                const auto targetY = itemY - juce::jmax(0, choiceViewport.getHeight() - promptItemHeight) / 2;
                choiceViewport.setViewPosition(0, juce::jlimit(0, maxOffset, targetY));
            }
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (! panelBounds.contains(event.getPosition()))
            cancel();
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (! panelBounds.contains(event.getPosition()))
            return;

        const auto directionalDelta = wheel.isReversed ? wheel.deltaY : -wheel.deltaY;
        const auto scrollAmount = wheel.isSmooth
            ? static_cast<int>(std::round(directionalDelta * 220.0f))
            : static_cast<int>(std::round((directionalDelta < 0.0f ? -1.0f : 1.0f) * 48.0f));

        if (scrollAmount == 0)
            return;

        const auto maxOffset = juce::jmax(0, choiceContent.getHeight() - choiceViewport.getHeight());

        if (maxOffset <= 0)
            return;

        choiceViewport.setViewPosition(0, juce::jlimit(0, maxOffset, choiceViewport.getViewPositionY() + scrollAmount));
    }

private:
    juce::Rectangle<int> getVisibleBounds() const
    {
        auto bounds = getLocalBounds();


        return bounds;
    }

    void choose(const int index)
    {
        if (closePending)
            return;

        if (! itemEnabled(index))
            return;

        auto deferredSelectCallback = std::move(onSelect);
        const auto choiceIndex = index;
        requestClose();

        juce::MessageManager::callAsync([choiceIndex,
                                         callback = std::move(deferredSelectCallback)]() mutable
                                        {
                                            if (callback != nullptr)
                                                callback(choiceIndex);
                                        });
    }

    void requestClose()
    {
        if (closePending)
            return;

        closePending = true;
        auto deferredCloseCallback = std::move(onClose);
        onClose = {};

        if (onDismiss != nullptr)
            onDismiss();

        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<FloatingChoicePrompt>(this),
                                         callback = std::move(deferredCloseCallback)]() mutable
                                        {
                                            if (safeThis == nullptr || callback == nullptr)
                                                return;

                                            callback();
                                        });
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
    juce::Viewport choiceViewport;
    ChoiceContent choiceContent;
    std::vector<std::unique_ptr<BoxTextButton>> itemButtons;
    SelectCallback onSelect;
    DismissCallback onDismiss;
    CloseCallback onClose;
    juce::Rectangle<int> panelBounds;
    bool initialScrollApplied = false;
    bool closePending = false;
};
}

std::unique_ptr<PromptComponent> makeChoicePrompt(juce::Rectangle<int> anchorBounds,
                                                  juce::StringArray choices,
                                                  const int selectedIndex,
                                                  std::vector<bool> itemEnabledStates,
                                                  const juce::Justification itemJustification,
                                                  std::function<void(int)> onSelect,
                                                  std::function<void()> onDismiss,
                                                  std::function<void()> onClose)
{
    return std::make_unique<FloatingChoicePrompt>(std::move(anchorBounds),
                                                  std::move(choices),
                                                  selectedIndex,
                                                  std::move(itemEnabledStates),
                                                  itemJustification,
                                                  std::move(onSelect),
                                                  std::move(onDismiss),
                                                  std::move(onClose));
}
