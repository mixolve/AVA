#pragma once
#include "Style.h"

#include <utility>

namespace
{
struct MarkdownLink
{
    juce::String label;
    juce::String url;
};

juce::URL createOfflineDocumentUrl(const char* fileName, const void* data, const size_t dataSize)
{
    const auto documentDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                       .getChildFile("mixolve-ava");

    if (documentDirectory.createDirectory().failed())
        return {};

    const auto documentFile = documentDirectory.getChildFile(fileName);
    documentFile.setReadOnly(false);

    if (data == nullptr && dataSize != 0)
        return {};

    if (dataSize == 0)
    {
        // JUCE replaceWithData with 0 bytes deletes the target instead of
        // writing; an empty document must still open as an empty file.
        if (! documentFile.replaceWithText({}))
            return {};
    }
    else if (! documentFile.replaceWithData(data, dataSize))
        return {};

    documentFile.setReadOnly(true);
    return juce::URL(documentFile);
}

juce::String getDisplayNameFromUrl(const juce::String& urlText)
{
    auto displayName = urlText.trim();

    if (displayName.startsWithIgnoreCase("https://"))
        displayName = displayName.substring(8);
    else if (displayName.startsWithIgnoreCase("http://"))
        displayName = displayName.substring(7);

    return displayName.upToFirstOccurrenceOf("/", false, false).trim();
}

MarkdownLink parseMarkdownLinkLine(const juce::String& line)
{
    MarkdownLink link;
    const auto trimmed = line.trim();

    if (trimmed.startsWithChar('['))
    {
        const auto labelEnd = trimmed.indexOfChar(']');
        const auto openParen = trimmed.indexOfChar('(');
        const auto closeParen = trimmed.lastIndexOfChar(')');

        if (labelEnd > 1 && openParen > labelEnd && closeParen > openParen)
        {
            link.label = trimmed.substring(1, labelEnd).trim();
            link.url = trimmed.substring(openParen + 1, closeParen).trim();
            return link;
        }
    }

    if (trimmed.startsWithIgnoreCase("https://") || trimmed.startsWithIgnoreCase("http://"))
    {
        link.label = getDisplayNameFromUrl(trimmed);
        link.url = trimmed;
    }

    return link;
}

class MarkdownLinkRow final : public juce::Component
{
public:
    MarkdownLinkRow(juce::String text, juce::String urlText, juce::Font rowFont = makeUiFont())
        : linkButton(std::move(text),
                     urlText.startsWithIgnoreCase("ava-manual://")
                         || urlText.startsWithIgnoreCase("ava-licenses://")
                         ? juce::URL {}
                         : juce::URL(urlText))
    {
        setOpaque(false);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setInterceptsMouseClicks(false, true);

        const auto linkFont = std::move(rowFont);
        linkButton.setFont(linkFont, false);
        linkButton.setJustificationType(juce::Justification::centred);
        linkButton.setColour(juce::HyperlinkButton::textColourId, uiAccent);
        linkButton.setMouseClickGrabsKeyboardFocus(false);
        linkButton.setWantsKeyboardFocus(false);
        linkButton.setSize(1, rowHeight);
        linkButton.changeWidthToFitText();

        if (urlText.startsWithIgnoreCase("ava-manual://"))
        {
            linkButton.onClick = []
            {
                const auto manualUrl = createOfflineDocumentUrl(
                    "manual.md", BinaryData::manual_md,
                    static_cast<size_t>(BinaryData::manual_mdSize));

                if (manualUrl.isWellFormed())
                    manualUrl.launchInDefaultBrowser();
            };
        }
        else if (urlText.startsWithIgnoreCase("ava-licenses://"))
        {
            linkButton.onClick = []
            {
                const auto licensesUrl = createOfflineDocumentUrl(
                    "licenses.md", BinaryData::licenses_md,
                    static_cast<size_t>(BinaryData::licenses_mdSize));

                if (licensesUrl.isWellFormed())
                    licensesUrl.launchInDefaultBrowser();
            };
        }

        addAndMakeVisible(linkButton);
    }

    void resized() override
    {
        const auto buttonBounds = linkButton.getBounds().withCentre(getLocalBounds().getCentre());
        linkButton.setBounds(buttonBounds);
    }

private:
    juce::HyperlinkButton linkButton;
};

}
