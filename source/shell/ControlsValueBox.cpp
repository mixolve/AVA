#include "EditorControls.h"
#include "ChoiceControl.h"
#include "LocalParameterControl.h"
#include "ParameterControl.h"

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
    updateMouseCursor();
}

ValueBoxComponent::~ValueBoxComponent()
{
    stopGlobalEditTracking();
}

void ValueBoxComponent::scheduleMarqueeRepaint()
{
    if (marqueeRepaintPending || ! isShowing())
        return;

    marqueeRepaintPending = true;
    juce::Timer::callAfterDelay(16, [safeThis = juce::Component::SafePointer<ValueBoxComponent>(this)]
    {
        if (safeThis == nullptr)
            return;

        safeThis->marqueeRepaintPending = false;
        safeThis->repaint();
    });
}

void ValueBoxComponent::setInteractionEnabled(const bool shouldEnable)
{
    if (interactionEnabled == shouldEnable)
        return;

    interactionEnabled = shouldEnable;
    updateMouseCursor();

    if (! interactionEnabled && editor != nullptr)
        hideEditor(true);

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

void ValueBoxComponent::setHighlightColour(const juce::Colour colour)
{
    if (highlightColour == colour)
        return;

    highlightColour = colour;

    if (editor != nullptr)
        editor->setColour(juce::TextEditor::highlightColourId, highlightColour);
}

void ValueBoxComponent::setPromptActive(const bool shouldBeActive)
{
    if (promptActive == shouldBeActive)
        return;

    promptActive = shouldBeActive;

    if (! shouldBeActive)
    {
        pressHighlight = false;
        pointerDown = false;
        dragDetected = false;
    }

    repaint();
}

void ValueBoxComponent::setCustomPromptAction(std::function<void()> action)
{
    customPromptAction = std::move(action);
    updateMouseCursor();
}

void ValueBoxComponent::updateMouseCursor()
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void ValueBoxComponent::paint(juce::Graphics& g)
{
    const auto displayText = displayTextProvider != nullptr ? displayTextProvider()
                                                            : slider.getTextFromValue(slider.getValue());
    auto backgroundColour = uiGrey800;
    auto borderColour = outlineColour;
    const auto interactionHighlight = interactionEnabled && (pressHighlight || isMouseOver(true));

    if (promptActive)
    {
        backgroundColour = uiGreyLight;
        borderColour = uiAccent;
    }
    else if (interactionHighlight)
    {
        backgroundColour = uiGreyLight;
    }

    g.setColour(backgroundColour);
    g.fillRect(getLocalBounds());

    g.setColour(borderColour);
    g.drawRect(getLocalBounds(), 1);

    g.setColour(interactionEnabled
                    ? ((promptActive || interactionHighlight) ? uiBlack : getDisplayTextColour(displayText))
                    : uiGrey500);
    g.setFont(makeUiFont());
    if (drawLoopingText(g,
                        displayText,
                        getLocalBounds().reduced(uiGap, 0),
                        makeUiFont(),
                        juce::Justification::centred))
    {
        scheduleMarqueeRepaint();
    }
}

void ValueBoxComponent::resized()
{
    if (editor != nullptr)
        editor->setBounds(getLocalBounds());
}

void ValueBoxComponent::mouseDown(const juce::MouseEvent& event)
{
    auto* clickedComponent = event.originalComponent;
    const auto clickIsInsideThisValueBox = clickedComponent != nullptr
        && (clickedComponent == this || isParentOf(clickedComponent));

    if (editor != nullptr && ! clickIsInsideThisValueBox)
    {
        hideEditor(false);
        return;
    }

    if (! clickIsInsideThisValueBox)
        return;

    if (! event.mods.isLeftButtonDown())
        return;

    const auto valueCanOpenPrompt = interactionEnabled || customPromptAction != nullptr;

    pointerDown = true;
    dragDetected = false;
    dragStartViewportY = 0;

    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        dragStartViewportY = viewport->getViewPositionY();

    pressHighlight = valueCanOpenPrompt;
    repaint();
}

void ValueBoxComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (! pointerDown)
        return;

    if (! dragDetected && event.mouseWasDraggedSinceMouseDown())
        dragDetected = true;

    shell_parameter_focus::clearFocus(*this);

    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
    {
        const auto viewedHeight = viewport->getViewedComponent() != nullptr
            ? viewport->getViewedComponent()->getHeight()
            : 0;
        const auto maxScrollY = juce::jmax(0, viewedHeight - viewport->getHeight());
        const auto dragScale = event.mods.isShiftDown() ? 0.1 : 1.0;
        viewport->setViewPosition(0,
                                  juce::jlimit(0,
                                               maxScrollY,
                                               dragStartViewportY - juce::roundToInt(static_cast<double>(event.getDistanceFromDragStartY()) * dragScale)));
    }

    const auto canHighlight = interactionEnabled || customPromptAction != nullptr;
    const auto shouldHighlight = canHighlight && contains(event.getPosition());

    if (pressHighlight != shouldHighlight)
    {
        pressHighlight = shouldHighlight;
        repaint();
    }
}

void ValueBoxComponent::mouseUp(const juce::MouseEvent& event)
{
    if (! pointerDown)
        return;

    const auto shouldOpenPrompt = contains(event.getPosition())
        && ! dragDetected
        && ! event.mods.isPopupMenu();

    pointerDown = false;
    dragDetected = false;

    if (shouldOpenPrompt && customPromptAction != nullptr)
    {
        customPromptAction();
    }
    else if (shouldOpenPrompt && interactionEnabled)
    {
        showEditor();
    }

    pressHighlight = false;
    repaint();
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

    if (onBeforeShowEditor != nullptr)
        onBeforeShowEditor();

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
    textEditor->setName(slider.getName());
    textEditor->setFont(makeUiFont());
    textEditor->setPopupMenuEnabled(true);
    textEditor->setJustification(juce::Justification::centred);
    textEditor->setColour(juce::TextEditor::textColourId, uiWhite);
    textEditor->setColour(juce::TextEditor::backgroundColourId, uiGrey800);
    textEditor->setColour(juce::TextEditor::outlineColourId, outlineColour);
    textEditor->setColour(juce::TextEditor::focusedOutlineColourId, outlineColour);
    textEditor->setColour(juce::TextEditor::highlightColourId, highlightColour);
    textEditor->setColour(juce::TextEditor::highlightedTextColourId, uiWhite);
    textEditor->setText(editorText, false);
    textEditor->onReturnKey = [this] { hideEditor(false); };
    textEditor->onEscapeKey = [this] { hideEditor(true); };
    textEditor->onFocusLost = [this] { hideEditor(false); };

    addAndMakeVisible(*textEditor);
    editor = std::move(textEditor);
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
    stopGlobalEditTracking();
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
