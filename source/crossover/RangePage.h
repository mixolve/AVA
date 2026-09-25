#pragma once

#include "Component.h"
#include "Page.h"

#include "../shell/ChoiceControl.h"
#include "../shell/ParameterControl.h"
#include "../shell/Controls.h"

#include <memory>
#include <vector>

class CrossoverRangePage final : public CrossoverModulePage,
                                 private juce::AudioProcessorValueTreeState::Listener
{
public:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CrossoverControlSpec = CrossoverModuleComponent::CrossoverControlSpec;

    CrossoverRangePage(CrossoverModuleComponent& ownerIn,
                       size_t rangeIndexIn,
                       juce::Colour accent);
    ~CrossoverRangePage() override;

    void refreshExternalState() override;
    int getPreferredHeight() const override;
    juce::Component* getPinnedHeaderComponent() noexcept override;
    int getPinnedHeaderHeight() const noexcept override;
    juce::Component* getPinnedTailComponent() noexcept override;
    int getPinnedTailHeight() const noexcept override;
    void layoutPinnedTail() override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    struct RowBase : public juce::Component
    {
        virtual int getPreferredHeight() const = 0;
        virtual void refreshExternalState() {}
        int getTopGap() const noexcept;
        void mouseDown(const juce::MouseEvent&) override;

        int topGapMultiplier = 1;
        int controlsInRow = 1;
        juce::String reorderGroup;
        juce::String orderParameterId;
        bool fixedOrder = false;
    };

    struct ParameterRow final : public RowBase
    {
        ParameterRow(CrossoverRangePage& pageIn,
                     CrossoverModuleComponent& ownerIn,
                     juce::String parameterId,
                     juce::String auxiliaryToggleParameterId,
                     juce::String enabledWhenParameterId,
                     const CrossoverControlSpec& spec);

        int getPreferredHeight() const override;
        void resized() override;
        void refreshExternalState() override;
        void refreshOrderLabel();
        void refreshAuxiliaryToggleState();
        void toggleAuxiliaryParameter();

        CrossoverRangePage& page;
        CrossoverModuleComponent& owner;
        juce::String auxiliaryToggleId;
        juce::String enabledWhenId;
        bool auxiliaryToggleInverted = false;
        int parameterTitleWidth = 0;
        int auxiliaryToggleWidth = 0;
        std::unique_ptr<ParameterControl> control;
        std::unique_ptr<BoxTextButton> orderLabel;
        std::unique_ptr<BoxTextButton> auxiliaryToggle;
        std::unique_ptr<ButtonAttachment> auxiliaryToggleAttachment;
    };

    struct ChoiceRow final : public RowBase
    {
        ChoiceRow(CrossoverModuleComponent& ownerIn,
                  const juce::String& parameterId,
                  const CrossoverControlSpec& spec);
        int getPreferredHeight() const override;
        void resized() override;
        std::unique_ptr<ChoiceControl> control;
    };

    struct HeadingRow final : public RowBase
    {
        explicit HeadingRow(const CrossoverControlSpec& spec);
        int getPreferredHeight() const override;
        void resized() override;
        std::unique_ptr<BoxTextButton> heading;
    };

    struct InactiveRow final : public RowBase
    {
        explicit InactiveRow(const CrossoverControlSpec& spec);
        int getPreferredHeight() const override;
        void resized() override;
        std::unique_ptr<BoxTextButton> button;
    };

    struct ToggleRow final : public RowBase
    {
        ToggleRow(CrossoverRangePage& pageIn,
                  CrossoverModuleComponent& ownerIn,
                  juce::String parameterId,
                  const CrossoverControlSpec& spec);
        int getPreferredHeight() const override;
        void refreshExternalState() override;
        void resized() override;
        void updateLabel();

        CrossoverRangePage& page;
        CrossoverModuleComponent& owner;
        juce::String parameterIdToToggle;
        juce::String exclusiveGroup;
        juce::String enabledLabel;
        juce::String disabledLabel;
        std::unique_ptr<BoxTextButton> button;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    struct ReadoutRow final : public RowBase
    {
        ReadoutRow(CrossoverModuleComponent& ownerIn,
                   juce::String degreeParameterId,
                   juce::String flipParameterId,
                   const CrossoverControlSpec& spec);
        int getPreferredHeight() const override;
        void refreshExternalState() override;
        void resized() override;
        void updateText();

        CrossoverModuleComponent& owner;
        juce::String degreeParameterIdToRead;
        juce::String flipParameterIdToRead;
        juce::Label value;
    };

    struct TimeRow final : public RowBase
    {
        TimeRow(CrossoverModuleComponent& ownerIn,
                juce::String valueParameterId,
                juce::String modeParameterId,
                juce::String syncParameterId,
                const CrossoverControlSpec& spec);
        int getPreferredHeight() const override;
        void refreshExternalState() override;
        void resized() override;
        bool isHostSyncMode() const noexcept;
        int getTimeModeIndex() const noexcept;
        int getSyncChoiceIndex() const noexcept;
        juce::String getSyncChoiceText() const;
        juce::StringArray getSyncChoices() const;
        void showSyncPrompt();
        void updateModeControl();

        CrossoverModuleComponent& owner;
        juce::String valueParameterIdToEdit;
        juce::String modeParameterIdToEdit;
        juce::String syncParameterIdToEdit;
        std::unique_ptr<ParameterControl> control;
        std::unique_ptr<BoxTextButton> modeButton;
    };

    juce::String getCrossoverRangeParameterId(const CrossoverControlSpec& spec) const;
    void addControlSpecs(const std::vector<CrossoverControlSpec>& specs, juce::Component& parent);
    void moveReorderRow(ParameterRow& sourceRow, int delta);
    void armReorderMove(ParameterRow& sourceRow);
    void applyReorderMove(ParameterRow& destinationRow);
    void clearReorderDragTarget(const juce::String& group);
    bool reorderRows(const juce::String& group);
    void clearExclusiveToggleGroup(const juce::String& activeParameterId, const juce::String& exclusiveGroup);
    void updateToggleLabels();
    void updateTimeModeControls();
    void parameterChanged(const juce::String& parameterID, float) override;

    CrossoverModuleComponent& owner;
    size_t rangeIndex = 0;
    BoxTextButton moduleHeading;
    std::unique_ptr<juce::Component> pinnedTail;
    std::vector<std::unique_ptr<RowBase>> rows;
    size_t tailRowStart = 0;
    std::vector<juce::String> listenedParameterIds;
    ParameterRow* reorderMoveSource = nullptr;
};
