#include "Prompt.h"
#include "Controls.h"
#include "AboutLinks.h"

#include <utility>

namespace
{

class AboutTextRow final : public juce::Component,
                               public juce::SettableTooltipClient
{
public:
    explicit AboutTextRow(juce::String textIn) : text(std::move(textIn))
    {
        setOpaque(false);
        setPaintingIsUnclipped(true);
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
        paintAboutText(graphics, *this, layout, text);
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
        // The parent document already provides horizontal padding.  Do not
        // subtract a second one: long email / language labels were wrapping
        // to fragments even with unused space to their sides.
        const auto contentWidth = juce::jmax(1, width);

        if (cachedWidth == contentWidth)
            return;

        juce::AttributedString attributed(text);
        attributed.setFont(makeUiFont());
        attributed.setColour(juce::Colour(0xff222222));
        attributed.setJustification(juce::Justification::centred);
        attributed.setWordWrap(juce::AttributedString::byWord);

        layout.createLayout(attributed, static_cast<float>(contentWidth));
        cachedWidth = contentWidth;
    }

    juce::String text;
    mutable juce::TextLayout layout;
    mutable int cachedWidth = -1;
};

class AboutDocument final : public juce::Component
{
public:
    explicit AboutDocument(juce::String markdownText, std::function<void()> blankClick)
        : onBackgroundClick(std::move(blankClick))
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

    }

    int getPreferredHeight(int width) const
    {
        int height = getUiGap(*this) * 2;
        const auto contentWidth = juce::jmax(1, width - getUiGap(*this) * 2);
        for (auto* row : contentRows)
        {
            const auto* text = dynamic_cast<AboutTextRow*>(row);
            height += (text != nullptr ? text->getPreferredHeight(contentWidth) : rowHeight) + getUiGap(*this);
        }
        return height - (contentRows.empty() ? 0 : getUiGap(*this));
    }

    void paint(juce::Graphics& graphics) override
    {
        paintPluginBackground(graphics);
    }

    // Text and link rows continue receiving their own clicks. Only the
    // document's empty background returns from ABOUT to the main viewport.
    void mouseUp(const juce::MouseEvent&) override
    {
        if (onBackgroundClick) onBackgroundClick();
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(getUiGap(*this));
        for (auto* row : contentRows)
        {
            const auto* text = dynamic_cast<AboutTextRow*>(row);
            row->setBounds(bounds.removeFromTop(text != nullptr ? text->getPreferredHeight(bounds.getWidth()) : rowHeight));
            bounds.removeFromTop(getUiGap(*this));
        }
    }

private:
    std::function<void()> onBackgroundClick;
    std::vector<std::unique_ptr<AboutTextRow>> textRows;
    std::vector<std::unique_ptr<MarkdownLinkRow>> linkRows;
    std::vector<juce::Component*> contentRows;
};

class AboutPanel final : public juce::Component
{
public:
    explicit AboutPanel(juce::String text, std::function<void()> blankClick)
        : document(std::move(text), blankClick), onBackgroundClick(std::move(blankClick))
    {
        viewport.setName("about-viewport");
        viewport.setViewedComponent(&document, false);
        viewport.setScrollBarsShown(false, false, true, false);
        viewport.setScrollBarThickness(getUiGap(*this));
        viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
        viewport.setWantsKeyboardFocus(false);
        addAndMakeVisible(viewport);
    }
    ~AboutPanel() override { viewport.setViewedComponent(nullptr, false); }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (onBackgroundClick) onBackgroundClick();
    }
    void resized() override
    {
        // INF is now text-only; no footer is reserved for COPY/PASTE/ZOOM.
        viewport.setBounds(getLocalBounds().reduced(getUiGap(*this), 0));
        for (int pass = 0; pass < 2; ++pass)
        {
            const auto width = juce::jmax(1, viewport.getMaximumVisibleWidth());
            document.setSize(width, juce::jmax(viewport.getHeight(), document.getPreferredHeight(width)));
        }
    }
private:
    AboutDocument document;
    std::function<void()> onBackgroundClick;
    juce::Viewport viewport;
};
}

std::unique_ptr<juce::Component> makeAboutPanel(juce::String markdownText,
                                                std::function<void()> onBackgroundClick)
{
    return std::make_unique<AboutPanel>(std::move(markdownText), std::move(onBackgroundClick));
}
