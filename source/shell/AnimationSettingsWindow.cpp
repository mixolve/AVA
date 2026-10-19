#include "OwnerAttachedWindow.h"
#include "OscPanel.h"
#include "Editor.h"
#include "AnimationState.h"
#include "Controls.h"
#include "LocalParameterControl.h"
#include "ChoiceControl.h"
#include "ParameterEditing.h"
#include "Prompt.h"

#include <array>

namespace
{
class AnimationSettingsContent final : public juce::Component, private juce::Timer
{
public:
    AnimationSettingsContent(std::shared_ptr<AnimationSettings> settingsIn,
                             juce::ValueTree& stateIn, BurningFilmArtwork& topSource,
                             BurningFilmArtwork& bottomSource, std::function<void()> close)
        : settings(std::move(settingsIn)), state(stateIn),
          sameButton(uiAccent), syncButton(uiAccent), closeButton(uiAccent),
          topArtwork(topSource), bottomArtwork(bottomSource)
    {
        viewport.setViewedComponent(&rows, false);
        viewport.setScrollBarsShown(false, false, true, false);
        viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
        viewport.setWantsKeyboardFocus(false);
        addAndMakeVisible(viewport);
        sameButton.setButtonText("SAME");
        syncButton.setButtonText("SYNC");
        sameButton.setClickingTogglesState(true);
        syncButton.setClickingTogglesState(true);
        sameButton.setToggleState(settings->same.load(), juce::dontSendNotification);
        syncButton.setToggleState(settings->sync.load(), juce::dontSendNotification);
        sameButton.onClick = [this]
        {
            settings->same.store(sameButton.getToggleState());
            state.setProperty("anim-same", settings->same.load(), nullptr);
            refreshBandControls();
        };
        syncButton.onClick = [this]
        {
            settings->sync.store(syncButton.getToggleState());
            state.setProperty("anim-sync", settings->sync.load(), nullptr);
        };
        rows.addAndMakeVisible(sameButton);
        rows.addAndMakeVisible(syncButton);
        const juce::StringArray choices { "OFF", "BURNING", "TOPOGRAPHIC", "TRACK-NAME", "ITEM-NAME", "TR/IT-NAME", "CUSTOM-TEXT" };
        juce::StringArray colours;
        for (const auto colour : animationColours)
            colours.add(juce::String::toHexString(static_cast<int>(colour)).paddedLeft('0', 6).toUpperCase());
        topAnimation = std::make_unique<LocalChoiceControl>("MODE", choices, 1);
        bottomAnimation = std::make_unique<LocalChoiceControl>("MODE", choices, 1);
        for (size_t bandIndex = 0; bandIndex < previews.size(); ++bandIndex)
        {
            auto& preview = previews[bandIndex];
            preview.setGpuRenderingEnabled(true);
            preview.setAnimationSource(bandIndex == 0 ? topSource : bottomSource);
            preview.onCropChanged = [this, bandIndex](float position)
            {
                state.setProperty(modePropertyName(bandIndex, "crop-y"), position, nullptr);
            };
            preview.setInterceptsMouseClicks(true, false);
            rows.addAndMakeVisible(preview);
        }
        const auto connectChoicePrompt = [this] (LocalChoiceControl& control, bool nameSelector = false)
        {
            control.setChoicePromptPresenter(
                [this, &control, nameSelector] (const juce::StringArray& choicesToShow, const int selectedIndex,
                                  std::vector<bool> enabled, const juce::Justification justification,
                                  std::function<void(int)> onSelect)
                {
                    if (ValueBoxComponent::refocusActiveInlineEditorIn(*this))
                        return;
                    auto availableChoices = choicesToShow;
                    auto visibleSelectedIndex = selectedIndex;
                    const auto hideItemModes = nameSelector && !settings->hostIsItem.load();
                    if (hideItemModes)
                    {
                        availableChoices.removeRange(4, 2);
                        if (!enabled.empty())
                        {
                            enabled.resize(static_cast<size_t>(choicesToShow.size()), true);
                            enabled.erase(enabled.begin() + 4, enabled.begin() + 6);
                        }
                        visibleSelectedIndex = selectedIndex >= 6 ? selectedIndex - 2
                            : selectedIndex >= 4 ? -1 : selectedIndex;
                    }
                    auto selectVisibleChoice = [hideItemModes, callback = std::move(onSelect)](int index)
                    {
                        if (callback != nullptr) callback(hideItemModes && index >= 4 ? index + 2 : index);
                    };
                    choicePromptOverlay.reset();
                    choicePromptOverlay = makeChoicePrompt(getLocalArea(&control, control.getValueBounds()),
                        availableChoices, visibleSelectedIndex, std::move(enabled), justification, std::move(selectVisibleChoice), {},
                        [safeThis = juce::Component::SafePointer<AnimationSettingsContent>(this)]
                        {
                            if (safeThis != nullptr)
                                safeThis->choicePromptOverlay.reset();
                        });
                    addAndMakeVisible(*choicePromptOverlay);
                    choicePromptOverlay->setBounds(getLocalBounds());
                    choicePromptOverlay->toFront(false);
                });
        };
        for (size_t bandIndex = 0; bandIndex < controls.size(); ++bandIndex)
        {
            auto& band = bandIndex == 0 ? settings->top : settings->bottom;
            auto& selector = bandIndex == 0 ? topAnimation : bottomAnimation;
            selector->setSelectedChoiceIndex(band.animation.load(), false);
            selector->onValueChanged = [this, bandIndex]
            {
                auto& selectedBand = bandIndex == 0 ? settings->top : settings->bottom;
                const auto& selectedControl = bandIndex == 0 ? topAnimation : bottomAnimation;
                const auto index = selectedControl->getSelectedChoiceIndex();
                selectedBand.animation.store(index);
                state.setProperty(bandIndex == 0 ? "anim-top" : "anim-bottom", index, nullptr);
                refreshBandControls();
            };
            connectChoicePrompt(*selector, true);
            rows.addAndMakeVisible(*selector);

            auto colour = std::make_unique<LocalChoiceControl>("COLOR", colours,
                static_cast<int>(animationColours.size()) - 1);
            colour->setSelectedChoiceIndex(band.current().colour.load(), false);
            colour->onValueChanged = [this, bandIndex]
            {
                if (refreshingControls) return;
                auto& selectedBand = bandIndex == 0 ? settings->top : settings->bottom;
                const auto index = colourControls[bandIndex]->getSelectedChoiceIndex();
                selectedBand.current().colour.store(index);
                state.setProperty(modePropertyName(bandIndex, "color"), index, nullptr);
                repaintArtwork();
            };
            connectChoicePrompt(*colour);
            rows.addAndMakeVisible(*colour);
            colourControls[bandIndex] = std::move(colour);
            auto motion = std::make_unique<LocalChoiceControl>("MODE", juce::StringArray { "STATIC", "MOVING" }, 1);
            motion->onValueChanged = [this, bandIndex]
            {
                if (refreshingControls) return;
                auto& selectedBand = bandIndex == 0 ? settings->top : settings->bottom;
                const auto moving = motionControls[bandIndex]->getSelectedChoiceIndex() == 1;
                selectedBand.current().moving.store(moving);
                state.setProperty(modePropertyName(bandIndex, "moving"), moving, nullptr);
                refreshBandControls();
                repaintArtwork();
            };
            connectChoicePrompt(*motion);
            rows.addAndMakeVisible(*motion);
            motionControls[bandIndex] = std::move(motion);
            textTitles[bandIndex] = std::make_unique<BoxTextButton>(uiAccent);
            textTitles[bandIndex]->setButtonText("TEXT");
            textTitles[bandIndex]->setTextJustification(juce::Justification::centredLeft);
            textTitles[bandIndex]->onClick = [this, bandIndex] { showCustomTextEditor(bandIndex); };
            textValues[bandIndex] = std::make_unique<BoxTextButton>(uiAccent);
            textValues[bandIndex]->setTextScrollingEnabled(false);
            textValues[bandIndex]->onClick = [this, bandIndex] { showCustomTextEditor(bandIndex); };
            rows.addAndMakeVisible(*textTitles[bandIndex]);
            rows.addAndMakeVisible(*textValues[bandIndex]);
        }
        startTimerHz(30);
        closeButton.setTablerIcon("x");
        closeButton.setTooltip("CLOSE");
        closeButton.onClick = std::move(close);
        rows.addAndMakeVisible(closeButton);
        for (size_t bandIndex = 0; bandIndex < controls.size(); ++bandIndex)
        {
            auto& band = bandIndex == 0 ? settings->top : settings->bottom;
            for (size_t fieldIndex = 0; fieldIndex < animationFields.size(); ++fieldIndex)
            {
                const auto& field = animationFields[fieldIndex];
                auto control = std::make_unique<LocalParameterControl>(field.label, field.decimals,
                    field.minimum, field.maximum, field.interval, field.initial);
                control->setValue((band.current().*field.value).load(), false);
                control->onValueChanged = [this, bandIndex, fieldIndex]
                {
                    if (refreshingControls) return;
                    auto& selectedBand = bandIndex == 0 ? settings->top : settings->bottom;
                    const auto value = static_cast<float>(controls[bandIndex][fieldIndex]->getValue());
                    (selectedBand.current().*animationFields[fieldIndex].value).store(value);
                    state.setProperty(modePropertyName(bandIndex, animationFields[fieldIndex].property), value, nullptr);
                };
                rows.addAndMakeVisible(*control);
                controls[bandIndex][fieldIndex] = std::move(control);
            }

        }
        refreshBandControls();
    }

    ~AnimationSettingsContent() override
    {
        stopTimer();
        viewport.setViewedComponent(nullptr, false);
    }

    void cancelEditors()
    {
        ValueBoxComponent::cancelActiveInlineEditorIn(*this);
        choicePromptOverlay.reset();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(uiBackground);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(uiGap);
        viewport.setBounds(bounds);
        const auto contentWidth = juce::jmax(0, bounds.getWidth());
        rows.setSize(contentWidth, getRowsHeight(contentWidth));
        auto rowBounds = rows.getLocalBounds();
        auto mainRow = rowBounds.removeFromTop(rowHeight);
        closeButton.setBounds(mainRow.removeFromRight(rowHeight));
        mainRow.removeFromRight(uiGap);
        sameButton.setBounds(mainRow.removeFromLeft((mainRow.getWidth() - uiGap) / 2));
        mainRow.removeFromLeft(uiGap);
        syncButton.setBounds(mainRow);
        rowBounds.removeFromTop(uiGap * 2);
        for (size_t bandIndex = 0; bandIndex < controls.size(); ++bandIndex)
        {
            if (!previews[bandIndex].isVisible()) continue;
            if (bandIndex != 0)
                rowBounds.removeFromTop(uiGap);
            previews[bandIndex].setBounds(rowBounds.removeFromTop(contentWidth));
            rowBounds.removeFromTop(uiGap);
            auto& selector = bandIndex == 0 ? topAnimation : bottomAnimation;
            selector->setBounds(rowBounds.removeFromTop(rowHeight));
            rowBounds.removeFromTop(uiGap);
            if (colourControls[bandIndex]->isVisible())
            {
                colourControls[bandIndex]->setBounds(rowBounds.removeFromTop(rowHeight));
                rowBounds.removeFromTop(uiGap);
            }
            if (motionControls[bandIndex]->isVisible())
            {
                motionControls[bandIndex]->setBounds(rowBounds.removeFromTop(rowHeight));
                rowBounds.removeFromTop(uiGap);
            }
            for (auto& control : controls[bandIndex])
            {
                if (! control->isVisible())
                    continue;
                control->setBounds(rowBounds.removeFromTop(rowHeight));
                rowBounds.removeFromTop(uiGap);
            }
            if (textTitles[bandIndex]->isVisible())
            {
                auto textRow = rowBounds.removeFromTop(rowHeight);
                textTitles[bandIndex]->setBounds(textRow.removeFromLeft((contentWidth - uiGap) / 2));
                textRow.removeFromLeft(uiGap);
                textValues[bandIndex]->setBounds(textRow);
                rowBounds.removeFromTop(uiGap);
            }

        }
        if (choicePromptOverlay != nullptr)
            choicePromptOverlay->setBounds(getLocalBounds());
    }

private:
    void timerCallback() override
    {
        if (!isShowing())
            return;
        if (settingsSnapshot != captureSettings()) refreshBandControls();
        refreshTextValues();
    }
    int getRowsHeight(int width) const
    {
        auto height = rowHeight + 2 * uiGap;
        bool hasBand = false;
        for (size_t bandIndex = 0; bandIndex < controls.size(); ++bandIndex)
        {
            if (!previews[bandIndex].isVisible()) continue;
            hasBand = true;
            if (bandIndex != 0) height += uiGap;
            height += width + rowHeight + 2 * uiGap;
            if (colourControls[bandIndex] != nullptr && colourControls[bandIndex]->isVisible())
                height += rowHeight + uiGap;
            if (motionControls[bandIndex] != nullptr && motionControls[bandIndex]->isVisible())
                height += rowHeight + uiGap;
            for (const auto& control : controls[bandIndex])
                if (control != nullptr && control->isVisible())
                    height += rowHeight + uiGap;
            if (textTitles[bandIndex] != nullptr && textTitles[bandIndex]->isVisible())
                height += rowHeight + uiGap;

        }
        return hasBand ? height - uiGap : rowHeight;
    }

    juce::String modePropertyName(size_t bandIndex, const char* property) const
    {
        const auto& band = bandIndex == 0 ? settings->top : settings->bottom;
        return animationModeProperty(bandIndex, band.animation.load(), property);
    }

    using SettingsSnapshot = std::array<float, 2 + 2 * (3 + animationFields.size())>;
    SettingsSnapshot captureSettings() const
    {
        SettingsSnapshot snapshot {};
        size_t index = 0;
        snapshot[index++] = settings->same.load() ? 1.f : 0.f;
        snapshot[index++] = settings->sync.load() ? 1.f : 0.f;
        for (const auto* band : { &settings->top, &settings->bottom })
        {
            snapshot[index++] = static_cast<float>(band->animation.load());
            snapshot[index++] = static_cast<float>(band->current().colour.load());
            snapshot[index++] = band->current().moving.load() ? 1.f : 0.f;
            for (const auto& field : animationFields)
                snapshot[index++] = (band->current().*field.value).load();
        }
        return snapshot;
    }
    void refreshBandControls()
    {
        const juce::ScopedValueSetter<bool> guard(refreshingControls, true);
        settingsSnapshot = captureSettings();
        sameButton.setToggleState(settings->same.load(), juce::dontSendNotification);
        syncButton.setToggleState(settings->sync.load(), juce::dontSendNotification);
        syncButton.setEnabled(settings->same.load() && settings->top.animation.load() < 3);
        for (size_t bandIndex = 0; bandIndex < controls.size(); ++bandIndex)
        {
            const auto& band = bandIndex == 0 ? settings->top : settings->bottom;
            const auto showBand = bandIndex == 0 || !settings->same.load();
            previews[bandIndex].setVisible(showBand);
            auto& selector = bandIndex == 0 ? topAnimation : bottomAnimation;
            selector->setVisible(showBand);
            selector->setSelectedChoiceIndex(band.animation.load(), false);
            const auto isOff = band.animation.load() == 0;
            const auto customTextVisible = showBand && band.animation.load() == customTextAnimationMode;
            textTitles[bandIndex]->setVisible(customTextVisible);
            textValues[bandIndex]->setVisible(customTextVisible);
            colourControls[bandIndex]->setVisible(showBand && !isOff);
            const auto& mode = band.current();
            colourControls[bandIndex]->setSelectedChoiceIndex(mode.colour.load(), false);
            motionControls[bandIndex]->setVisible(showBand && band.animation.load() >= 3);
            motionControls[bandIndex]->setSelectedChoiceIndex(mode.moving.load() ? 1 : 0, false);
            for (size_t fieldIndex = 0; fieldIndex < animationFields.size(); ++fieldIndex)
            {
                const auto& field = animationFields[fieldIndex];
                controls[bandIndex][fieldIndex]->setValueRange(field.lowerBound(band.animation.load()),
                    field.upperBound(band.animation.load()), field.interval);
                controls[bandIndex][fieldIndex]->setDefaultValue(
                    field.defaultValue(band.animation.load()));
                controls[bandIndex][fieldIndex]->setValue((mode.*field.value).load(), false);
                controls[bandIndex][fieldIndex]->setVisible(showBand && !isOff && field.supports(band.animation.load()));
                controls[bandIndex][fieldIndex]->setInteractionEnabled(band.animation.load() < 3 || mode.moving.load());
            }

        }
        refreshTextValues();
        resized();
    }

    void refreshTextValues()
    {
        for (size_t index = 0; index < textValues.size(); ++index)
        {
            const auto& band = index == 0 ? settings->top : settings->bottom;
            const auto text = band.getCustomText();
            const auto display = text.isEmpty() ? juce::String::charToString(0x2014) : text;
            if (textValues[index]->getButtonText() != display) textValues[index]->setButtonText(display);
        }
    }

    void showCustomTextEditor(size_t bandIndex)
    {
        if (ValueBoxComponent::refocusActiveInlineEditorIn(*this)) return;
        choicePromptOverlay.reset();
        const auto& band = bandIndex == 0 ? settings->top : settings->bottom;
        choicePromptOverlay = makeTextPrompt(band.getCustomText(),
            [safeThis = juce::Component::SafePointer<AnimationSettingsContent>(this), bandIndex](const juce::String& text)
            {
                if (safeThis == nullptr) return false;
                auto& selectedBand = bandIndex == 0 ? safeThis->settings->top : safeThis->settings->bottom;
                selectedBand.setCustomText(text);
                safeThis->state.setProperty(animationModeProperty(bandIndex, customTextAnimationMode, "text"), selectedBand.getCustomText(), nullptr);
                safeThis->refreshTextValues();
                safeThis->repaintArtwork();
                return true;
            }, getLocalArea(textValues[bandIndex].get(), textValues[bandIndex]->getLocalBounds()), {},
            [safeThis = juce::Component::SafePointer<AnimationSettingsContent>(this)]
            {
                if (safeThis != nullptr) safeThis->choicePromptOverlay.reset();
            }, true, true);
        addAndMakeVisible(*choicePromptOverlay);
        choicePromptOverlay->setBounds(getLocalBounds());
        choicePromptOverlay->toFront(true);
        juce::MessageManager::callAsync([safePrompt = juce::Component::SafePointer<PromptComponent>(choicePromptOverlay.get())]
        {
            if (safePrompt != nullptr) safePrompt->focusEditor();
        });
    }

    void repaintArtwork()
    {
        topArtwork.repaint();
        bottomArtwork.repaint();
        for (auto& preview : previews) preview.repaint();
        if (auto* owner = topArtwork.getTopLevelComponent()) owner->repaint();
    }

    std::shared_ptr<AnimationSettings> settings;
    juce::ValueTree& state;
    juce::Component rows;
    juce::Viewport viewport;
    BoxTextButton sameButton, syncButton;
    std::array<BurningFilmArtwork, 2> previews;
    std::unique_ptr<LocalChoiceControl> topAnimation, bottomAnimation;
    std::array<std::unique_ptr<LocalChoiceControl>, 2> colourControls, motionControls;
    std::array<std::unique_ptr<BoxTextButton>, 2> textTitles, textValues;
    BoxTextButton closeButton;
    BurningFilmArtwork& topArtwork;
    BurningFilmArtwork& bottomArtwork;
    std::array<std::array<std::unique_ptr<LocalParameterControl>, animationFields.size()>, 2> controls;
    std::unique_ptr<PromptComponent> choicePromptOverlay;
    SettingsSnapshot settingsSnapshot {};
    bool refreshingControls = false;
};

class AnimationSettingsWindow final : public OwnerAttachedWindow
{
public:
    AnimationSettingsWindow(juce::Component& owner, std::shared_ptr<AnimationSettings> settings,
                            juce::ValueTree& state, BurningFilmArtwork& topSource,
                            BurningFilmArtwork& bottomSource)
        : OwnerAttachedWindow(),
          content(std::move(settings), state, topSource, bottomSource,
                  [this] { closeButtonPressed(); })
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setDropShadowEnabled(false);
        setResizable(false, false);
        setResizeLimits(minimumEditorWidth, minimumEditorHeight, minimumEditorWidth, maximumEditorHeight);
        setLookAndFeel(&owner.getLookAndFeel());
        setContentNonOwned(&content, false);
        setSize(minimumEditorWidth, minimumEditorHeight);
        addToDesktop();
        showFor(owner);
    }
    ~AnimationSettingsWindow() override
    {
        clearContentComponent();
        setLookAndFeel(nullptr);
    }
    void closeButtonPressed() override
    {
        content.cancelEditors();
        setVisible(false);
    }

private:
    int getAttachedWidth() const override { return minimumEditorWidth; }
    AnimationSettingsContent content;
};
}

void AvaAudioProcessorEditor::setAnimationSettings(std::shared_ptr<AnimationSettings> settings)
{
    animationSettings = std::move(settings);
    if (topLines != nullptr) topLines->setAnimationSettings(animationSettings);
    if (bottomLines != nullptr) bottomLines->setAnimationSettings(animationSettings);
}

void AvaAudioProcessorEditor::showAnimationSettings()
{
    if (auto* parent = findParentComponentOfClass<AvaAudioProcessorEditor>())
    {
        parent->showAnimationSettings();
        return;
    }
    if (oscParameterListWindow != nullptr)
    {
        oscParameterListWindow.reset();
        if (oscPanel != nullptr) oscPanel->refresh();
    }
    if (animationSettingsWindow == nullptr)
        animationSettingsWindow = std::make_unique<AnimationSettingsWindow>(*this, animationSettings,
            valueTreeState.state, *topLines, *bottomLines);
    else
    {
        static_cast<AnimationSettingsWindow*>(animationSettingsWindow.get())->showFor(*this);
    }
}
