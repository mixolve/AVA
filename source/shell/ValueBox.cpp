#include "Controls.h"
#include "InlineParameterSlider.h"
#include "Editor.h"
#include "ChoiceControl.h"
#include "LocalParameterControl.h"
#include "ParameterControl.h"

namespace
{
juce::Component::SafePointer<ValueBoxComponent> activeInlineValueEditor;

}

bool scrollViewportWithWheel(juce::Viewport& viewport,
                             const int contentHeight,
                             const juce::MouseWheelDetails& wheel,
                             const bool fineControl)
{
    const auto maxScrollY = juce::jmax(0, contentHeight - viewport.getHeight());

    if (maxScrollY <= 0)
        return false;

    const auto dominantDelta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY
                                                                                : -wheel.deltaX;

    if (std::abs(dominantDelta) < 1.0e-6f)
        return false;

    const auto scrollSensitivity = (wheel.isSmooth ? 140.0f : 48.0f) * (fineControl ? 0.1f : 1.0f);
    auto pixelDelta = juce::roundToInt(-dominantDelta * scrollSensitivity);

    if (pixelDelta == 0)
        pixelDelta = dominantDelta < 0.0f
            ? (wheel.isSmooth ? 1 : 48)
            : (wheel.isSmooth ? -1 : -48);

    viewport.setViewPosition(0, juce::jlimit(0, maxScrollY, viewport.getViewPositionY() + pixelDelta));
    return true;
}

ValueBoxComponent::ValueBoxComponent(juce::Slider& sliderToControl)
    : slider(sliderToControl)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    inlineSlider = std::make_unique<InlineParameterSlider>(slider);
    inlineSlider->canOpen = [this]
    { return !refocusActiveInlineEditorIn(*getTopLevelComponent()); };
    addAndMakeVisible(*inlineSlider);
    updateSliderAvailability();
}

ValueBoxComponent::~ValueBoxComponent()
{
    if (activeInlineValueEditor == this)
        activeInlineValueEditor = nullptr;
    stopGlobalEditTracking();
}

bool ValueBoxComponent::refocusActiveInlineEditorIn(juce::Component& scope)
{
    auto* active = activeInlineValueEditor.getComponent();
    if (active == nullptr || active->editor == nullptr
        || (active != &scope && ! scope.isParentOf(active)))
        return false;

    juce::MessageManager::callAsync([safeActive = juce::Component::SafePointer<ValueBoxComponent>(active)]
    {
        if (safeActive != nullptr && safeActive->editor != nullptr)
            safeActive->editor->grabKeyboardFocus();
    });
    return true;
}

void ValueBoxComponent::cancelActiveInlineEditorIn(juce::Component& scope)
{
    auto* active = activeInlineValueEditor.getComponent();
    if (active != nullptr && (active == &scope || scope.isParentOf(active)))
        active->hideEditor(true);
}

void ValueBoxComponent::setNumericControlEnabled(bool enabled)
{
    numericControlEnabled = enabled;
    updateSliderAvailability();
    repaint();
}

void ValueBoxComponent::updateSliderAvailability()
{
    inlineSlider->setAvailable(numericControlEnabled && interactionEnabled && customPromptAction == nullptr
        && !promptActive && editor == nullptr && slider.getMaximum() > slider.getMinimum());
}

void ValueBoxComponent::setInteractionEnabled(const bool shouldEnable)
{
    if (interactionEnabled == shouldEnable)
        return;

    interactionEnabled = shouldEnable;

    if (! interactionEnabled && editor != nullptr)
        hideEditor(true);

    updateSliderAvailability();

    repaint();
}

void ValueBoxComponent::setOutlineColour(const juce::Colour colour)
{
    if (outlineColour == colour)
        return;

    outlineColour = colour;

    if (editor != nullptr)
        editor->setColour(juce::TextEditor::outlineColourId, outlineColour);

    repaint();
}

void ValueBoxComponent::setPromptActive(const bool shouldBeActive)
{
    if (promptActive == shouldBeActive)
        return;

    promptActive = shouldBeActive;
    updateSliderAvailability();

    if (! shouldBeActive)
    {
        pressHighlight = false;
        pointerDown = false;
    }

    repaint();
}

void ValueBoxComponent::setCustomPromptAction(std::function<void()> action)
{
    customPromptAction = std::move(action);
    updateSliderAvailability();
    repaint();
}

void ValueBoxComponent::paint(juce::Graphics& g)
{
    ControlAppearance style(promptActive);
    style.paintBackground(g,getLocalBounds());

}

void ValueBoxComponent::paintOverChildren(juce::Graphics& g)
{
    const auto editing=editor!=nullptr || inlineSlider->isExpanded() || inlineSlider->hasReveal();
    const auto hover=hoverAnimation.amount(interactionEnabled && (pressHighlight || isMouseHovering(*this)));
    ControlAppearance style(promptActive,editing?0.0f:hover,editing);
    style.normalBorder=outlineColour;
    style.paintFrame(g,getLocalBounds());
    if(editor!=nullptr) return;
    const auto text = displayTextProvider ? displayTextProvider() : slider.getTextFromValue(slider.getValue());
    style.normalContent=getDisplayTextColour(text);
    const auto colour=interactionEnabled?style.contentColour():uiGreyLight;
    // One font, bounds and time for all states: revealing the controller only
    // changes the background/contrast, never the glyph positions or marquee phase.
    const auto textBounds = getLocalBounds().reduced(uiGap, 0).reduced(
        inlineSlider->isVisible() ? InlineParameterSlider::petalWidth : 0, 0);
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() * 0.001;
    bool scrolling = numericControlEnabled && textRaster.prepare(text,getLocalBounds(),textBounds,
        g.getInternalContext().getPhysicalPixelScaleFactor(),elapsed);
    const auto paintText = [&](const auto& clip, juce::Colour textColour)
    {
        const juce::Graphics::ScopedSaveState saved(g);
        if (!g.reduceClipRegion(clip)) return;
        g.setColour(textColour);
        g.setFont(makeUiFont());
        if(numericControlEnabled) textRaster.draw(g);
        else scrolling=drawLoopingText(g,text,textBounds,makeUiFont(),juce::Justification::centred,elapsed) || scrolling;
    };
    juce::RectangleList<int> normal(getLocalBounds());
    if (inlineSlider->isVisible() && inlineSlider->hasReveal())
    {
        const auto revealBounds = inlineSlider->getRevealBounds();
        const auto light = inlineSlider->getGripPath();
        normal.subtract(revealBounds);
        juce::Path dark;
        dark.addRectangle(revealBounds);
        dark.addPath(light);
        dark.setUsingNonZeroWinding(false);
        paintText(normal, colour);
        paintText(dark, colour.interpolatedWith(uiWhite, inlineSlider->getRevealOpacity()));
        paintText(light, colour.interpolatedWith(uiBlack, inlineSlider->getRevealOpacity()));
    }
    else paintText(normal, colour);
    if (scrolling) scheduleMarqueeRepaint(*this, marqueeRepaintPending);
}

void ValueBoxComponent::resized()
{
    inlineSlider->setBounds(getLocalBounds());
    if (editor != nullptr)
    {
        editor->setBounds(getLocalBounds());
        const auto text=editor->getTextBounds({0,editor->getTotalNumChars()}).getBounds();
        if(!text.isEmpty()) editor->setBounds(getLocalBounds().translated(0,getLocalBounds().getCentreY()-text.getCentreY()));
    }
}

void ValueBoxComponent::mouseDown(const juce::MouseEvent& event)
{
    auto* clickedComponent = event.originalComponent;
    const auto clickIsInsideThisValueBox = clickedComponent != nullptr
        && (clickedComponent == this || isParentOf(clickedComponent));

    if (editor != nullptr && ! clickIsInsideThisValueBox)
    {
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<ValueBoxComponent>(this)]
        {
            if (safeThis != nullptr && safeThis->editor != nullptr && safeThis->isShowing())
                safeThis->editor->grabKeyboardFocus();
        });
        return;
    }

    if (! clickIsInsideThisValueBox)
        return;

    if (! event.mods.isLeftButtonDown())
        return;

    const auto valueCanOpenPrompt = interactionEnabled || customPromptAction != nullptr;

    pointerDown = true;
    pressHighlight = valueCanOpenPrompt;
    repaint();
}

void ValueBoxComponent::mouseUp(const juce::MouseEvent& event)
{
    if (! pointerDown)
        return;

    const auto clicked = contains(event.getPosition())
        && ! event.mouseWasDraggedSinceMouseDown()
        && ! event.mods.isPopupMenu();

    pointerDown = false;
    pressHighlight = false;
    repaint();

    if (clicked)
    {
        if (customPromptAction != nullptr)
            customPromptAction();
        else if (interactionEnabled)
            showEditor();
    }
}

void ValueBoxComponent::mouseExit(const juce::MouseEvent&)
{
    if (pointerDown)
        pressHighlight = false;

    repaint();
}

void ValueBoxComponent::mouseEnter(const juce::MouseEvent&)
{
    repaint();
}

void ValueBoxComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
    {
        const auto viewedHeight = viewport->getViewedComponent() != nullptr
            ? viewport->getViewedComponent()->getHeight()
            : 0;
        scrollViewportWithWheel(*viewport, viewedHeight, wheel, event.mods.isShiftDown());
    }
}

void ValueBoxComponent::showEditor()
{
    if (editor != nullptr)
        return;

    if (auto* active = activeInlineValueEditor.getComponent();
        active != nullptr && active != this && active->editor != nullptr
        && active->getTopLevelComponent() == getTopLevelComponent())
    {
        refocusActiveInlineEditorIn(*getTopLevelComponent());
        return;
    }

    if (auto* owner = findParentComponentOfClass<AvaAudioProcessorEditor>())
    {
        const auto editorText = editorTextProvider != nullptr ? editorTextProvider()
                                                              : slider.getTextFromValue(slider.getValue());
        auto safeThis = juce::Component::SafePointer<ValueBoxComponent>(this);
        const auto anchorBounds = owner->getLocalArea(this, getLocalBounds());

        setPromptActive(true);
        pressHighlight = false;
        repaint();

        owner->showTextPrompt(editorText,
                              [safeThis] (const juce::String& enteredText)
                              {
                                  if (safeThis == nullptr)
                                      return false;

                                  safeThis->applyEnteredText(enteredText);
                                  return true;
                              },
                              anchorBounds,
                              [safeThis]
                              {
                                  if (safeThis != nullptr)
                                      safeThis->setPromptActive(false);
                              },
                              [safeThis]
                              {
                                  if (safeThis != nullptr)
                                      safeThis->setPromptActive(false);
                              });
        return;
    }

    const auto editorText = editorTextProvider != nullptr ? editorTextProvider()
                                                          : slider.getTextFromValue(slider.getValue());
    auto textEditor = std::make_unique<CopyPasteTextEditor>();
    if(numericControlEnabled) textEditor->setCachedComponentImage(new NumericEditorRaster(*textEditor));
    textEditor->setName(slider.getName());
    textEditor->setFont(makeUiFont());
    textEditor->setPopupMenuEnabled(true);
    textEditor->setJustification(juce::Justification::centred);
    textEditor->setColour(juce::TextEditor::textColourId, uiWhite);
    textEditor->setColour(juce::TextEditor::backgroundColourId, uiGreyDark);
    textEditor->setColour(juce::TextEditor::outlineColourId, outlineColour);
    textEditor->setColour(juce::TextEditor::focusedOutlineColourId, outlineColour);
    textEditor->setText(editorText, false);
    textEditor->onReturnKey = [this] { hideEditor(false); };
    textEditor->onEscapeKey = [this] { hideEditor(true); };

    addAndMakeVisible(*textEditor);
    editor = std::move(textEditor);
    activeInlineValueEditor = this;
    updateSliderAvailability();
    startGlobalEditTracking();
    resized();
    editor->grabKeyboardFocus();
    editor->selectAll();
}

void ValueBoxComponent::applyEnteredText(const juce::String& enteredText)
{
    const auto enteredValue = textToValueParser != nullptr
        ? textToValueParser(enteredText)
        : slider.getValueFromText(enteredText);
    const auto clampedValue = juce::jlimit(static_cast<double>(slider.getMinimum()),
                                           static_cast<double>(slider.getMaximum()),
                                           enteredValue);

    applyValue(clampedValue);

    if (auto* parent = getParentComponent())
        parent->repaint();

    repaint();
}

void ValueBoxComponent::applyValue(const double value)
{
    slider.setValue(value, juce::sendNotificationSync);
}

void ValueBoxComponent::hideEditor(const bool discard)
{
    if (editor == nullptr)
        return;

    if (! discard)
    {
        const auto enteredText = editor->getText().trim();
        const auto enteredValue = textToValueParser != nullptr
            ? textToValueParser(enteredText)
            : slider.getValueFromText(enteredText);
        const auto clampedValue = juce::jlimit(static_cast<double>(slider.getMinimum()),
                                               static_cast<double>(slider.getMaximum()),
                                               enteredValue);

        applyValue(clampedValue);
    }

    removeChildComponent(editor.get());
    editor.reset();
    if (activeInlineValueEditor == this)
        activeInlineValueEditor = nullptr;
    stopGlobalEditTracking();
    updateSliderAvailability();
    clearKeyboardFocus(*this);
    repaint();
}

void ValueBoxComponent::startGlobalEditTracking()
{
    if (isTrackingGlobalClicks)
        return;

    juce::Desktop::getInstance().addGlobalMouseListener(this);
    isTrackingGlobalClicks = true;
}

void ValueBoxComponent::stopGlobalEditTracking()
{
    if (! isTrackingGlobalClicks)
        return;

    juce::Desktop::getInstance().removeGlobalMouseListener(this);
    isTrackingGlobalClicks = false;
}
