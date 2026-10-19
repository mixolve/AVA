#include "Prompt.h"
#include "Controls.h"
#include "Editor.h"
#include "AboutLinks.h"

#include <utility>

namespace
{
constexpr int promptPanelPadding = uiGap;
constexpr int promptItemGap = uiGap;

class AboutTextRow final : public juce::Component,
                               public juce::SettableTooltipClient
{
public:
    explicit AboutTextRow(juce::String textIn) : text(std::move(textIn))
    {
        setOpaque(false);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }

    void setText(const juce::String& newText)
    {
        text = newText;
        cachedWidth = -1;
        repaint();
    }

    int getPreferredHeight(const int width) const
    {
        updateLayout(width);
        return juce::jmax(rowHeight, juce::roundToInt(layout.getHeight()));
    }

    void paint(juce::Graphics& graphics) override
    {
        updateLayout(getWidth());
        graphics.setColour(uiWhite);
        layout.draw(graphics, getLocalBounds().toFloat());
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (onClick != nullptr)
            onClick();
    }

    std::function<void()> onClick;

private:
    void updateLayout(const int width) const
    {
        const auto contentWidth = juce::jmax(1, width - uiGap * 2);

        if (cachedWidth == contentWidth)
            return;

        juce::AttributedString attributed(text);
        attributed.setFont(makeUiFont());
        attributed.setColour(uiWhite);
        attributed.setJustification(juce::Justification::centred);
        attributed.setWordWrap(juce::AttributedString::byWord);

        layout.createLayout(attributed, static_cast<float>(contentWidth));
        cachedWidth = contentWidth;
    }

    juce::String text;
    mutable juce::TextLayout layout;
    mutable int cachedWidth = -1;
};

class FloatingInfoPrompt final : public PromptComponent
{
public:
    using CloseCallback = std::function<void()>;

    FloatingInfoPrompt(juce::String markdownText,
                       CloseCallback closeCallback)
        : onClose(std::move(closeCallback))
    {
        setOpaque(true);
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(false);
        setInterceptsMouseClicks(true, true);

        for (const auto& line : juce::StringArray::fromLines(markdownText))
        {
            const auto trimmed = line.trim();

            if (trimmed.isEmpty())
                continue;

            const auto link = parseMarkdownLinkLine(trimmed);

            if (link.url.isNotEmpty())
            {
                auto linkRow = std::make_unique<MarkdownLinkRow>(link.label, link.url, makeUiFont());
                contentRows.push_back(linkRow.get());
                linkRows.push_back(std::move(linkRow));
                continue;
            }

            auto label = std::make_unique<AboutTextRow>(trimmed);

            if (trimmed.containsChar('@'))
            {
                juce::String address;

                for (const auto& token : juce::StringArray::fromTokens(trimmed, " \t", ""))
                {
                    if (! token.containsChar('@'))
                        continue;

                    address = token.trimCharactersAtStart("(").trimCharactersAtEnd("),.;:");
                    break;
                }

                if (address.isNotEmpty())
                {
                    label->setTooltip("CLICK TO COPY");
                    label->setMouseCursor(juce::MouseCursor::PointingHandCursor);
                    juce::Component::SafePointer<AboutTextRow> safeLabel(label.get());
                    label->onClick = [safeLabel, email = address.toLowerCase(),
                                      original = trimmed, token = address]
                    {
                        juce::SystemClipboard::copyTextToClipboard(email);

                        if (safeLabel == nullptr)
                            return;

                        // Monospace font: centre COPIED in the address slot so the line stays put.
                        const auto padTotal = juce::jmax(0, token.length() - juce::String("COPIED").length());
                        const auto leftPad = padTotal / 2;
                        const auto copied = juce::String::repeatedString(" ", leftPad) + "COPIED"
                            + juce::String::repeatedString(" ", padTotal - leftPad);
                        safeLabel->setText(original.replace(token, copied));
                        juce::Timer::callAfterDelay(1200, [safeLabel, original]
                        {
                            if (safeLabel != nullptr)
                                safeLabel->setText(original);
                        });
                    };
                }
            }

            contentRows.push_back(label.get());
            textRows.push_back(std::move(label));
        }

        for (auto* row : contentRows)
            addAndMakeVisible(row);

        okButton.setButtonText("OK");
        okButton.setTextJustification(juce::Justification::centred);
        okButton.onClick = [safeThis = juce::Component::SafePointer<FloatingInfoPrompt>(this)]
        {
            if (safeThis != nullptr)
                safeThis->requestClose();
        };
        addAndMakeVisible(okButton);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.setColour(uiBackground);
        graphics.fillAll();
    }

    void resized() override
    {
        const auto visibleBounds = getLocalBounds();
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

        for (size_t index = 0; index < contentRows.size(); ++index)
        {
            const auto* textRow = dynamic_cast<AboutTextRow*>(contentRows[index]);
            const auto rowHeightForRow = textRow != nullptr
                ? textRow->getPreferredHeight(contentBounds.getWidth())
                : rowHeight;
            contentRows[index]->setBounds(contentBounds.removeFromTop(rowHeightForRow));

            if (index + 1 < contentRows.size() && ! contentBounds.isEmpty())
                contentBounds.removeFromTop(promptItemGap);
        }

        if (! contentBounds.isEmpty())
            contentBounds.removeFromBottom(promptPanelPadding);

        okButton.setBounds(okBounds);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (! panelBounds.contains(event.getPosition()))
            cancel();
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
    void requestClose()
    {
        requestPromptClose(*this, closePending, onClose);
    }

    void cancel()
    {
        requestClose();
    }

    juce::Rectangle<int> panelBounds;
    std::vector<std::unique_ptr<AboutTextRow>> textRows;
    std::vector<std::unique_ptr<MarkdownLinkRow>> linkRows;
    std::vector<juce::Component*> contentRows;
    BoxTextButton okButton { uiAccent };
    CloseCallback onClose;
    bool closePending = false;
};
}

std::unique_ptr<PromptComponent> makeInfoPrompt(juce::String markdownText,
                                                std::function<void()> onClose)
{
    return std::make_unique<FloatingInfoPrompt>(std::move(markdownText), std::move(onClose));
}
