#include "Prompt.h"
#include "ValueEditor.h"
#include "UppercaseText.h"
#include "ParameterEditing.h"

#include <utility>

namespace
{
constexpr int promptEditorHeight = 30;

class FloatingTextPrompt final : public PromptComponent, private juce::KeyListener, private juce::ComponentListener
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
                       const bool uppercaseInput, const bool allowEmptyIn,
                       juce::Slider* numericControl,
                       std::function<double(const juce::String&)> textToValue,
                       std::function<juce::String()> valueToText)
        : onCommit(std::move(commitCallback)),
            anchorBounds(anchorBoundsIn),
          onDismiss(std::move(dismissCallback)),
          onClose(std::move(closeCallback)), allowEmpty(allowEmptyIn)
    {
        setOpaque(false);
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(false);
        setInterceptsMouseClicks(true, true);

        textEditor.setNumericControl(numericControl, std::move(textToValue), std::move(valueToText));
        useValueFieldFrame = numericControl != nullptr;
        textEditor.getProperties().set("controlTextButtonEditor", !anchorBounds.isEmpty());
        textEditor.setFrameVisible(!useValueFieldFrame);
        textEditor.setFont(makeUiFont());
        textEditor.setWantsKeyboardFocus(true);
        textEditor.setMouseClickGrabsKeyboardFocus(true);
        textEditor.setPopupMenuEnabled(true);
        textEditor.setJustification(juce::Justification::centred);
        textEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xff222222));
        textEditor.setColour(juce::CaretComponent::caretColourId, juce::Colour(0xff222222));
        textEditor.setColour(juce::TextEditor::backgroundColourId, uiWhite);
        textEditor.setColour(juce::TextEditor::outlineColourId, uiGreyLight);
        if (uppercaseInput)
        {
            textEditor.setInputFilter(new KeyboardLayoutEditorInputFilter(), true);
            currentText = uppercaseText(currentText);
        }
        textEditor.setText(std::move(currentText), false);
        textEditor.setReturnKeyStartsNewLine(false);
        textEditor.setEscapeAndReturnKeysConsumed(true);
        textEditor.addKeyListener(this);
        textEditor.onReturnKey = [this] { commit(); };
        addAndMakeVisible(editorClip);
        editorClip.addAndMakeVisible(textEditor);
    }

    void setResetAction(std::function<void()> action) override { textEditor.onResetRequested = std::move(action); }

    ~FloatingTextPrompt() override
    {
        textEditor.removeKeyListener(this);
        for (auto& component : anchorAncestors)
            if (component) component->removeComponentListener(this);
        if (onDismiss != nullptr) onDismiss();
    }

    void followAnchor(juce::Component& component) override
    {
        const auto area = component.getLocalBounds();
        const auto text = getControlTextBounds(component, area);
        textEditor.getProperties().set("textTopInset", text.getY() - area.getY());
        textEditor.getProperties().set("textBottomInset", area.getBottom() - text.getBottom());
        textEditor.getProperties().set("textSideInset", text.getX() - area.getX());
        if (dynamic_cast<BoxTextButton*>(&component) != nullptr)
        {
            // Route labels have MT/BP controls beneath the name. The editable
            // text/caret must be vertically centred in the name strip, not in
            // the full height of the component (including those indicators).
            textEditor.setBorder(juce::BorderSize<int>(
                text.getY() - area.getY(), 1, area.getBottom() - text.getBottom(), 1));
        }
        for (auto& previous : anchorAncestors)
            if (previous) previous->removeComponentListener(this);
        anchorAncestors.clear();
        anchorComponent = &component;
        for (auto* current = &component; current != nullptr; current = current->getParentComponent())
        {
            current->addComponentListener(this);
            anchorAncestors.emplace_back(current);
            if (current == getParentComponent()) break;
        }
        updateAnchor();
    }

    void focusEditor() override
    {
        if (closePending) return;
        textEditor.grabKeyboardFocus();
        if (!initialSelectionDone)
        {
            initialSelectionDone = true;
            textEditor.selectAll();
        }
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (!editorClip.isVisible()) return;
        const juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(editorClip.getBounds());
        g.addTransform(juce::AffineTransform::translation(float(editorClip.getX()), float(editorClip.getY())));
        textEditor.paintIndicator(g, textEditor.getBounds());
        if (useValueFieldFrame)
        {
            ControlAppearance style;
            style.frameColour = textEditor.findColour(juce::TextEditor::outlineColourId);
            style.paintFrame(g, textEditor.getBounds());
        }
    }

    void resized() override
    {
        if (!anchorBounds.isEmpty())
        {
            auto visible = anchorComponent != nullptr
                ? getVisibleControlBounds(*anchorComponent, *this)
                : anchorBounds.getIntersection(getLocalBounds());
            if (anchorComponent != nullptr)
                if (auto* scene = getParentComponent())
                {
                    juce::RectangleList<int> regions(visible);
                    for (auto* cover : getForegroundOccluders(*scene))
                        if (cover != anchorComponent && !cover->isParentOf(anchorComponent))
                            regions.subtract(getLocalArea(scene, getVisibleControlBounds(*cover, *scene)));
                    visible = {};
                    for (const auto region : regions)
                        if (region.getWidth() * region.getHeight() > visible.getWidth() * visible.getHeight()) visible = region;
                }
            // The floating editor only covers the value and its upper rail.
            // Keep the bottom indicator strip of the original ParameterField
            // visible, so HOST/BYPASS and embedded actions can still paint and
            // receive hover/click events during text editing.
            if (auto* field = anchorComponent != nullptr
                    ? anchorComponent->findParentComponentOfClass<ParameterField>() : nullptr)
            {
                const auto valueArea = getLocalArea(anchorComponent.getComponent(), anchorComponent->getLocalBounds());
                // Indicator hover outlines extend 1px ABOVE the bottom strip.
                // Leave that extra pixel uncovered by the floating value editor,
                // or its last painted row hides the upper edge of accessory hover.
                const auto upperArea = valueArea.withTrimmedBottom(
                    RangeIndicator::edgeInset + field->getBottomIndicatorSize() + 1);
                visible = visible.getIntersection(upperArea);
            }
            if (auto* button = dynamic_cast<BoxTextButton*>(anchorComponent.getComponent()))
                visible = visible.getIntersection(getLocalArea(button, getControlTextBounds(*button, button->getLocalBounds())));
            // Empty clipping is enough to hide an offscreen field. setVisible(false)
            // would give away keyboard focus and dim its selection on return.
            editorClip.setVisible(anchorComponent == nullptr || anchorComponent->isShowing());
            editorClip.setBounds(visible);
            textEditor.setBounds(anchorBounds.translated(-visible.getX(), -visible.getY()));
            return;
        }
        editorClip.setVisible(true);
        editorClip.setBounds(getLocalBounds());
        const auto visibleBounds = getLocalBounds();
        const auto editorInsetX = getUiGap(*this);
        const auto editorInsetTop = getUiGap(*this);
        const auto editorInsetBottom = getUiGap(*this);
        const auto promptWidth = juce::jmax(0, getWidth() - editorInsetX * 2);
        const auto promptHeight = juce::jmin(promptEditorHeight, juce::jmax(0, visibleBounds.getHeight() - editorInsetTop - editorInsetBottom));

        textEditor.setBounds(editorInsetX,
                             visibleBounds.getY() + editorInsetTop,
                             promptWidth,
                             promptHeight);
    }

    bool hitTest(int x,int y) override
    {
        if (checkingBackground) return false;
        auto* target = underlyingComponentAt({x, y});
        if (auto* box = dynamic_cast<BoxTextButton*>(target); box != nullptr && box->isEnabled()
            && box->bottomIndicatorAt(box->getLocalPoint(this, juce::Point<int>(x, y))) >= 0) return false;
        auto* value = dynamic_cast<ValueBoxComponent*>(target);
        if (value == nullptr && target != nullptr) value = target->findParentComponentOfClass<ValueBoxComponent>();
        if (value != nullptr && value->isEnabled()
            && value->bottomIndicatorAt(value->getLocalPoint(this, juce::Point<int>(x, y))) >= 0) return false;
        // Embedded actions are siblings of the value box. When editing, do not
        // let the fullscreen prompt intercept their native button hover/clicks.
        auto* button = dynamic_cast<juce::Button*>(target);
        if (button != nullptr && button->isEnabled()
            && bool(button->getProperties().getWithDefault("embeddedControlButton", false)))
        {
            if (button->findParentComponentOfClass<ParameterField>() != nullptr)
            {
                const auto local = button->getLocalPoint(this, juce::Point<int>(x, y));
                if (button->getLocalBounds().contains(local)) return false;
            }
        }
        return juce::Component::hitTest(x,y);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (textEditor.beginIndicatorDrag(event.getEventRelativeTo(&textEditor))) return;
        auto* target = underlyingComponentAt(event.getPosition());
        if(event.mods.isLeftButtonDown() && isEmptyEditorBackground(target)) {
            if(!commit()) cancel();
            return;
        }
        textEditor.grabKeyboardFocus();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (textEditor.isIndicatorDragging())
            textEditor.mouseDrag(event.getEventRelativeTo(&textEditor));
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (textEditor.isIndicatorDragging())
            textEditor.mouseUp(event.getEventRelativeTo(&textEditor));
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (handleConfirmationKey(key))
            return true;
        textEditor.grabKeyboardFocus();
        return textEditor.keyPressed(key);
    }

private:
    juce::Component* underlyingComponentAt(juce::Point<int> position)
    {
        const juce::ScopedValueSetter<bool> checking(checkingBackground, true);
        auto* root = getTopLevelComponent();
        return root->getComponentAt(root->getLocalPoint(this, position));
    }

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
        return false;
    }

    void requestClose()
    {
        // Keep the underlying control in its editing state until this overlay
        // is removed, then restore it once in the destructor.
        textEditor.setCaretVisible(false);
        requestPromptClose(*this, closePending, onClose);
    }

    bool commit()
    {
        if (closePending)
            return false;

        const auto enteredText = textEditor.getText().trim();

        if (enteredText.isEmpty() && !allowEmpty)
        {
            textEditor.grabKeyboardFocus();
            return false;
        }

        if (onCommit != nullptr && ! onCommit(enteredText))
        {
            textEditor.grabKeyboardFocus();
            return false;
        }

        requestClose();
        return true;
    }

    void cancel()
    {
        requestClose();
    }

    juce::Component editorClip;
    ValueEditor textEditor;
    CommitCallback onCommit;
    void updateAnchor()
    {
        if (anchorComponent && getParentComponent())
        {
            const auto previous = editorClip.getBounds();
            anchorBounds = getLocalArea(anchorComponent, anchorComponent->getLocalBounds());
            resized();
            repaint(previous.getUnion(editorClip.getBounds()).expanded(2));
        }
    }
    void componentVisibilityChanged(juce::Component&) override { updateAnchor(); }
    void componentMovedOrResized(juce::Component&, bool, bool) override { updateAnchor(); }
    void componentBeingDeleted(juce::Component& component) override
    {
        if (anchorComponent == &component) cancel();
    }
    juce::Component::SafePointer<juce::Component> anchorComponent;
    std::vector<juce::Component::SafePointer<juce::Component>> anchorAncestors;
    juce::Rectangle<int> anchorBounds;
    DismissCallback onDismiss;
    CloseCallback onClose;
    bool useValueFieldFrame = false;
    bool closePending = false;
    bool initialSelectionDone = false;
    bool checkingBackground = false;
    const bool allowEmpty;
};
}

std::unique_ptr<PromptComponent> makeTextPrompt(juce::String currentText,
                                                std::function<bool(const juce::String&)> onCommit,
                                                const juce::Rectangle<int> anchorBounds,
                                                std::function<void()> onDismiss,
                                                std::function<void()> onClose,
                                                const bool uppercaseInput, const bool allowEmpty,
                                                juce::Slider* numericControl,
                                                std::function<double(const juce::String&)> textToValue,
                       std::function<juce::String()> valueToText)
{
    return std::make_unique<FloatingTextPrompt>(std::move(currentText),
                                                std::move(onCommit),
                                                anchorBounds,
                                                std::move(onDismiss),
                                                std::move(onClose),
                                                uppercaseInput, allowEmpty, numericControl, std::move(textToValue), std::move(valueToText));
}
