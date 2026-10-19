#pragma once

#include "Editor.h"
#include "Page.h"
#include "../shell/DisclosureAnimation.h"
#include "../shell/SvgExport.h"

#include "../shell/ChoiceControl.h"
#include "../shell/ParameterControl.h"
#include "../shell/Controls.h"

#include <memory>
#include <vector>

class LocalParameterControl;

class CrossoverRangePage final : public CrossoverModulePage,
                                 public svg_export::ExpandableContent,
                                 private juce::AudioProcessorValueTreeState::Listener,
                                 private juce::AsyncUpdater
{
public:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CrossoverControlSpec = CrossoverModuleComponent::CrossoverControlSpec;
    using ExclusiveScope = CrossoverModuleComponent::ExclusiveScope;

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
    void showBlockArrange();
    void showGainArrange();

    // The exported artwork must contain every row, so collapsed parameter
    // groups are opened for the duration of the export only.
    void expandContentForSvgExport() override;
    void restoreContentAfterSvgExport() override;

private:
    int getFlatPreferredHeight() const;
    void applyBlockOrder(const tls::blocks::Order& order);
    void applyGainOrder(const tls::gain::Order& order);
    bool refreshBlockVisibility();
    struct RowBase : public juce::Component
    {
        virtual int getPreferredHeight() const = 0;
        virtual void refreshExternalState() {}
        virtual int getTopGap() const noexcept;
        virtual int getHorizontalOutset() const noexcept { return 0; }
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void paintOverChildren(juce::Graphics&) override;

        int blockIndex = -1;
        int gainIndex = -1;
        bool layoutHidden = false;
        bool paintsRowBackground = false;
        juce::String label;
        ExclusiveScope exclusiveScope = ExclusiveScope::none;
        int topGapMultiplier = 1;
        int controlsInRow = 1;
    };

    struct DisclosureGroup final : public RowBase
    {
        DisclosureGroup(CrossoverRangePage& pageIn, juce::String keyIn,
                        std::vector<size_t> definingIndicesIn, std::vector<std::unique_ptr<RowBase>> membersIn);
        int getPreferredHeight() const override;
        int getTopGap() const noexcept override;
        int getHorizontalOutset() const noexcept override;
        void refreshExternalState() override;
        void resized() override;
        void paint(juce::Graphics&) override;
        void toggle();
        void setExpanded(bool expandedIn, bool animate);
        void updateVisibility();

        CrossoverRangePage& page;
        juce::String stateKey;
        std::vector<size_t> definingIndices;
        bool expanded = false;
        DisclosureAnimation animation;
        juce::Component slidingClip, slidingBody;
        std::vector<std::unique_ptr<RowBase>> members;
        std::unique_ptr<juce::Button> petal;
    };

    struct ParameterRow final : public RowBase
    {
        ParameterRow(CrossoverRangePage& pageIn,
                     CrossoverModuleComponent& ownerIn,
                     juce::String parameterId,
                     juce::String auxiliaryToggleParameterId,
                     const CrossoverControlSpec& spec);

        int getPreferredHeight() const override;
        void resized() override;
        void refreshExternalState() override;
        void refreshAuxiliaryToggleState();
        void refreshInteractionState();
        void toggleAuxiliaryParameter();

        CrossoverRangePage& page;
        CrossoverModuleComponent& owner;
        juce::String auxiliaryToggleId;
        CrossoverControlSpec controlSpec;
        std::vector<juce::String> enabledWhenAnyNonZeroIds;
        bool auxiliaryToggleInverted = false;
        int parameterTitleWidth = 0;
        int auxiliaryToggleWidth = 0;
        std::unique_ptr<ParameterControl> control;
        std::unique_ptr<ChoiceControl> auxiliaryChoice;
        std::unique_ptr<BoxTextButton> auxiliaryToggle;
        std::unique_ptr<ButtonAttachment> auxiliaryToggleAttachment;
    };

    struct ChoiceRow final : public RowBase
    {
        ChoiceRow(CrossoverModuleComponent& ownerIn,
                  const juce::String& parameterId,
                  const juce::String& auxiliaryToggleId,
                  const CrossoverControlSpec& spec);
        int getPreferredHeight() const override;
        void resized() override;
        void refreshExternalState() override;
        CrossoverModuleComponent& owner;
        juce::String suffix;
        std::unique_ptr<ChoiceControl> control;
        std::unique_ptr<BoxTextButton> auxiliaryToggle;
        std::unique_ptr<ButtonAttachment> auxiliaryToggleAttachment;
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
        int auxiliaryToggleWidth = 60;
        std::unique_ptr<BoxTextButton> auxiliaryToggle;
        std::unique_ptr<ButtonAttachment> auxiliaryToggleAttachment;
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

        CrossoverControlSpec controlSpec;
        CrossoverModuleComponent& owner;
        juce::String valueParameterIdToEdit;
        juce::String modeParameterIdToEdit;
        juce::String syncParameterIdToEdit;
        std::unique_ptr<ParameterControl> control;
        std::unique_ptr<BoxTextButton> modeButton;
    };

    juce::String getCrossoverRangeParameterId(const CrossoverControlSpec& spec) const;
    void addControlSpecs(const std::vector<CrossoverControlSpec>& specs, juce::Component& parent);
    void groupDynControls();
    void setAllGroupsExpanded(bool expanded);
    void refreshSvgExportLayout();
    void handleAsyncUpdate() override;
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
    tls::blocks::Order displayedBlockOrder = tls::blocks::defaultOrder;
    bool previewingBlockOrder = false, previewingGainOrder = false;
    tls::gain::Order displayedGainOrder = tls::gain::defaultOrder;
    std::vector<juce::String> listenedParameterIds;
    std::vector<bool> svgExportGroupStates;
};
