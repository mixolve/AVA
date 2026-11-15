#pragma once

#include <JuceHeader.h>

inline juce::String uppercaseText(const juce::String& text)
{
    auto upper = text.toUpperCase();
    juce::String result;
    for (auto characters = upper.getCharPointer(); ! characters.isEmpty();)
    {
        auto character = characters.getAndAdvance();
        // Cyrillic casing must also work when the host uses the C locale.
        if (character >= 0x0430 && character <= 0x044f) character -= 0x20;
        else if (character >= 0x0450 && character <= 0x045f) character -= 0x50;
        else if (character == 0x04cf) character = 0x04c0;
        else if (character >= 0x04c2 && character <= 0x04ce && (character & 1) == 0) --character;
        else if (((character >= 0x0460 && character <= 0x0481)
                  || (character >= 0x048a && character <= 0x04bf)
                  || (character >= 0x04d0 && character <= 0x052f)) && (character & 1) != 0) --character;
        result += juce::String::charToString(character);
    }
    return result;
}

class UppercaseEditorInputFilter final : public juce::TextEditor::InputFilter
{
public:
    juce::String filterNewText(juce::TextEditor&, const juce::String& text) override
    { return uppercaseText(text); }
};

// Physical-key equivalence for Russian and English keyboard layouts.
// This is not transliteration: ЕРКУЫРЩДВ uses the keys of THRESHOLD.
// Bidirectional matching lets the user search without switching input language.
inline juce::String swapKeyboardLayout(const juce::String& text)
{
    const juce::String russian = juce::String::fromUTF8("ЁЙЦУКЕНГШЩЗХЪФЫВАПРОЛДЖЭЯЧСМИТЬБЮ");
    const juce::String english = "`QWERTYUIOP[]ASDFGHJKL;'ZXCVBNM,.";
    const auto upper = uppercaseText(text);
    juce::String swapped;
    for (auto chars = upper.getCharPointer(); !chars.isEmpty();)
    {
        const auto ch = chars.getAndAdvance();
        const auto ru = russian.indexOfChar(ch);
        const auto en = english.indexOfChar(ch);
        if (ru >= 0) swapped += juce::String::charToString(english[ru]);
        else if (en >= 0) swapped += juce::String::charToString(russian[en]);
        else swapped += juce::String::charToString(ch);
    }
    return swapped;
}

// Convert Cyrillic keystrokes to their English physical-key equivalents in
// ordinary value/name editors. This keeps decimal punctuation usable without
// changing the OS keyboard layout (for example: Ю/ю -> '.', Б/б -> ',').
// Search intentionally keeps Cyrillic text, and uses swapKeyboardLayout for
// matching instead of rewriting the query.
inline juce::String normalisePhysicalKeyboardInput(const juce::String& text)
{
    const juce::String russian = juce::String::fromUTF8("ЁЙЦУКЕНГШЩЗХЪФЫВАПРОЛДЖЭЯЧСМИТЬБЮ");
    const juce::String english = "`QWERTYUIOP[]ASDFGHJKL;'ZXCVBNM,.";
    const auto upper = uppercaseText(text);
    juce::String converted;
    for (auto chars = upper.getCharPointer(); !chars.isEmpty();)
    {
        const auto character = chars.getAndAdvance();
        const auto index = russian.indexOfChar(character);
        converted += index >= 0 ? juce::String::charToString(english[index])
                                : juce::String::charToString(character);
    }
    return converted;
}

class KeyboardLayoutEditorInputFilter final : public juce::TextEditor::InputFilter
{
public:
    juce::String filterNewText(juce::TextEditor&, const juce::String& text) override
    { return normalisePhysicalKeyboardInput(text); }
};

inline juce::String normaliseControlSearchKey(const juce::String& text)
{
    // Search only the exact visible naming style. Do not treat hyphens,
    // underscores and spaces as interchangeable aliases.
    return uppercaseText(text.trim());
}
