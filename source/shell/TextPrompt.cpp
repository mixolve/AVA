#include "Prompt.h"
#include "Controls.h"
#include "UppercaseText.h"

#include <utility>

namespace
{
constexpr int promptEditorHeight = 30;

class FloatingTextPrompt final : public PromptComponent, private juce::KeyListener
{
public:
    using CommitCallback = std::function<bool(const juce::String&)>;
    using DismissCallback = std::function<void()>;
    using CloseCallback = std::function<void()>;

    FloatingTextPrompt(juce::String currentText,
                       CommitCallback commitCallback,
                     juce::Rectangle<int> anchorBoundsIn,
                       DismissCallback dismissCallback,
                       CloseCallback closeCallback,
                       const bool uppercaseInput, const bool allowEmptyIn)
        : onCommit(std::move(commitCallback)),
            anchorBounds(anchorBoundsIn),
          onDismiss(std::move(dismissCallback)),
          onClose(std::move(closeCallback)), allowEmpty(allowEmptyIn)
    {
        setOpaque(false);
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(false);
        setInterceptsMouseClicks(true, true);

        textEditor.setFont(makeUiFont());
        textEditor.setWantsKeyboardFocus(true);
        textEditor.setMouseClickGrabsKeyboardFocus(true);
        textEditor.setPopupMenuEnabled(true);
        textEditor.setJustification(juce::Justification::centred);
        textEditor.setColour(juce::TextEditor::textColourId, uiWhite);
        textEditor.setColour(juce::CaretComponent::caretColourId, uiWhite);
        textEditor.setColour(juce::TextEditor::backgroundColourId, uiGreyDark);
        textEditor.setColour(juce::TextEditor::outlineColourId, uiGreyLight);
        textEditor.setColour(juce::TextEditor::focusedOutlineColourId, uiAccent);
        if (uppercaseInput)
        {
            textEditor.setInputFilter(new UppercaseEditorInputFilter(), true);
            currentText = uppercaseText(currentText);
        }
        textEditor.setText(std::move(currentText), false);
        textEditor.setReturnKeyStartsNewLine(false);
        textEditor.setEscapeAndReturnKeysConsumed(true);
        textEditor.addKeyListener(this);
        textEditor.onReturnKey = [this] { commit(); };
        textEditor.onEscapeKey = [this] { cancel(); };
        addAndMakeVisible(textEditor);
    }

    ~FloatingTextPrompt() override
    {
        textEditor.removeKeyListener(this);
    }

    void focusEditor() override
    {
        textEditor.grabKeyboardFocus();
        textEditor.selectAll();
    }

    void paint(juce::Graphics& graphics) override
    {
        juce::ignoreUnused(graphics);
    }

    void resized() override
    {
        auto promptBounds = anchorBounds;

        if (! promptBounds.isEmpty())
        {
            promptBounds = promptBounds.getIntersection(getLocalBounds());

            if (! promptBounds.isEmpty())
            {
                const auto minimumHeight = juce::jmin(promptEditorHeight,
                                                      juce::jmax(rowHeight, promptBounds.getHeight()));
                textEditor.setBounds(promptBounds.withHeight(minimumHeight));
                return;
            }
        }

        const auto visibleBounds = getLocalBounds();
        const auto editorInsetX = uiGap;
        const auto editorInsetTop = uiGap;
        const auto editorInsetBottom = uiGap;
        const auto promptWidth = juce::jmax(0, getWidth() - editorInsetX * 2);
        const auto promptHeight = juce::jmin(promptEditorHeight, juce::jmax(0, visibleBounds.getHeight() - editorInsetTop - editorInsetBottom));

        textEditor.setBounds(editorInsetX,
                             visibleBounds.getY() + editorInsetTop,
                             promptWidth,
                             promptHeight);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        textEditor.grabKeyboardFocus();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (handleConfirmationKey(key))
            return true;
        textEditor.grabKeyboardFocus();
        return textEditor.keyPressed(key);
    }

private:
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        return handleConfirmationKey(key);
    }

    bool handleConfirmationKey(const juce::KeyPress& key)
    {
        if (key.isKeyCode(juce::KeyPress::returnKey))
        {
            commit();
            return true;
        }
        if (key.isKeyCode(juce::KeyPress::escapeKey))
        {
            cancel();
            return true;
        }
        return false;
    }

    void requestClose()
    {
        requestPromptClose(*this, closePending, onClose, onDismiss);
    }

    void commit()
    {
        if (closePending)
            return;

        const auto enteredText = textEditor.getText().trim();

        if (enteredText.isEmpty() && !allowEmpty)
        {
            textEditor.selectAll();
            textEditor.grabKeyboardFocus();
            return;
        }

        if (onCommit != nullptr && ! onCommit(enteredText))
        {
            textEditor.selectAll();
            textEditor.grabKeyboardFocus();
            return;
        }

        requestClose();
    }

    void cancel()
    {
        requestClose();
    }

    CopyPasteTextEditor textEditor;
    CommitCallback onCommit;
    juce::Rectangle<int> anchorBounds;
    DismissCallback onDismiss;
    CloseCallback onClose;
    bool closePending = false;
    const bool allowEmpty;
};
}

std::unique_ptr<PromptComponent> makeTextPrompt(juce::String currentText,
                                                std::function<bool(const juce::String&)> onCommit,
                                                const juce::Rectangle<int> anchorBounds,
                                                std::function<void()> onDismiss,
                                                std::function<void()> onClose,
                                                const bool uppercaseInput, const bool allowEmpty)
{
    return std::make_unique<FloatingTextPrompt>(std::move(currentText),
                                                std::move(onCommit),
                                                anchorBounds,
                                                std::move(onDismiss),
                                                std::move(onClose),
                                                uppercaseInput, allowEmpty);
}
