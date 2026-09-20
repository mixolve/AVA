#include "PromptComponents.h"
#include "Controls.h"
#include "Editor.h"
#include "MarkdownContent.h"

#include <utility>

namespace
{
class FloatingInfoPrompt final : public PromptComponent
{
public:
    using CloseCallback = std::function<void()>;

    FloatingInfoPrompt(juce::String markdownText,
                       CloseCallback closeCallback)
        : markdownContent(std::move(markdownText)),
          okButton(uiAccent),
          onClose(std::move(closeCallback))
    {
        setOpaque(false);
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(false);
        setInterceptsMouseClicks(true, true);

        markdownViewport.setViewedComponent(&markdownContent, false);
        markdownViewport.setScrollBarsShown(false, false);
        markdownViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
        markdownViewport.setWantsKeyboardFocus(false);
        markdownViewport.setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(markdownViewport);

        okButton.setButtonText("OK");
        okButton.setTextJustification(juce::Justification::centred);
        okButton.onClick = [safeThis = juce::Component::SafePointer<FloatingInfoPrompt>(this)]
        {
            if (safeThis != nullptr)
                safeThis->requestClose();
        };
        addAndMakeVisible(okButton);
    }

    ~FloatingInfoPrompt() override
    {
        markdownViewport.setViewedComponent(nullptr, false);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.setColour(juce::Colours::black);
        graphics.fillAll();
    }

    void resized() override
    {
        const auto visibleBounds = getVisibleBounds();
        auto anchorBounds = juce::Rectangle<int>();
        auto panelVisibleBounds = visibleBounds;

        if (auto* owner = findParentComponentOfClass<AvaAudioProcessorEditor>())
        {
            const auto ownerVisibleBounds = owner->getInfoPromptVisibleBounds().getIntersection(visibleBounds);
            const auto ownerAnchorBounds = owner->getInfoPromptAnchorBounds().getIntersection(visibleBounds);

            if (! ownerVisibleBounds.isEmpty())
                panelVisibleBounds = ownerVisibleBounds;

            if (! ownerAnchorBounds.isEmpty())
                anchorBounds = ownerAnchorBounds;
        }

        if (panelVisibleBounds.isEmpty())
            panelVisibleBounds = visibleBounds.reduced(promptPanelPadding);

        if (anchorBounds.isEmpty())
            anchorBounds = panelVisibleBounds;

        const auto desiredWidth = juce::jmax(1, panelVisibleBounds.getWidth());
        const auto desiredHeight = juce::jmax(1, panelVisibleBounds.getHeight());

        panelBounds = juce::Rectangle<int>(desiredWidth, desiredHeight);
        panelBounds.setX(panelVisibleBounds.getX());
        panelBounds.setY(panelVisibleBounds.getY());
        panelBounds = panelBounds.constrainedWithin(panelVisibleBounds);

        auto contentBounds = panelBounds.reduced(promptPanelPadding);
        auto okBounds = contentBounds.removeFromBottom(rowHeight);

        if (! contentBounds.isEmpty())
            contentBounds.removeFromBottom(promptPanelPadding);

        markdownViewport.setBounds(contentBounds);
        markdownContent.setSize(contentBounds.getWidth(),
                                juce::jmax(contentBounds.getHeight(),
                                           markdownContent.getContentHeight(contentBounds.getWidth())));
        okButton.setBounds(okBounds);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (! panelBounds.contains(event.getPosition()))
            cancel();
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (! markdownViewport.getBounds().contains(event.getPosition()))
            return;

        const auto directionalDelta = wheel.isReversed ? wheel.deltaY : -wheel.deltaY;
        const auto scrollAmount = wheel.isSmooth
            ? static_cast<int>(std::round(directionalDelta * 220.0f))
            : static_cast<int>(std::round((directionalDelta < 0.0f ? -1.0f : 1.0f) * 48.0f));

        if (scrollAmount == 0)
            return;

        const auto maxOffset = juce::jmax(0, markdownContent.getHeight() - markdownViewport.getHeight());

        if (maxOffset <= 0)
            return;

        markdownViewport.setViewPosition(0, juce::jlimit(0, maxOffset, markdownViewport.getViewPositionY() + scrollAmount));
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            cancel();
            return true;
        }

        return false;
    }

private:
    juce::Rectangle<int> getVisibleBounds() const
    {
        auto bounds = getLocalBounds();


        return bounds;
    }

    void requestClose()
    {
        if (closePending)
            return;

        closePending = true;
        auto deferredCloseCallback = std::move(onClose);
        onClose = {};

        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<FloatingInfoPrompt>(this),
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

    juce::Rectangle<int> panelBounds;
    MarkdownContentView markdownContent;
    juce::Viewport markdownViewport;
    BoxTextButton okButton;
    CloseCallback onClose;
    bool closePending = false;
};
}

std::unique_ptr<PromptComponent> makeInfoPrompt(juce::String markdownText,
                                                std::function<void()> onClose)
{
    return std::make_unique<FloatingInfoPrompt>(std::move(markdownText), std::move(onClose));
}
