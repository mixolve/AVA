#pragma once

#include "Editor.h"
#include "Page.h"
#include "../modules/shared/DspUtilities.h"

#include "../shell/ChoiceControl.h"
#include "../shell/ParameterControl.h"
#include "../shell/Controls.h"

#include <memory>
#include <vector>

class HeaderControlRow;

class CrossoverRangePage final : public CrossoverModulePage,
                                 private juce::AudioProcessorValueTreeState::Listener,
                                 private juce::AsyncUpdater
{
public:
    using CrossoverControlSpec = CrossoverModuleComponent::CrossoverControlSpec;

    CrossoverRangePage(CrossoverModuleComponent& ownerIn,
                       size_t rangeIndexIn);
    ~CrossoverRangePage() override;

    void refreshExternalState() override;
    int getPreferredHeight() const override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void hideIndependentBlocks();
    static void configureModuleScrollTile(juce::Component& block, const juce::String& caption,
                                          const std::function<bool()>& isBypassed,
                                          const std::function<void()>& toggle,
                                          const std::function<void()>& copy,
                                          const std::function<void()>& paste);
    std::vector<std::pair<int, juce::Component*>> independentBlocks(juce::Component& parent, int width);
private:
    void applyGainOrder(const ava::modules::chain::gain::Order& order);
    bool refreshBlockVisibility();
    struct RowBase : public ControlBlockComponent
    {
        RowBase() { getProperties().set("emptyEditorBackground",true); }
        virtual int getPreferredHeight() const = 0;
        virtual int getLayoutHeight(int) const { return getPreferredHeight(); }
        virtual void refreshExternalState() {}
        virtual int getTopGap() const noexcept;
        virtual bool isValueField() const noexcept { return false; }
        virtual int getCellSpan() const noexcept { return 1; }
        virtual int getPreferredWidth() const
        {
            const auto span = getCellSpan();
            return span * getSquareControlSize(*this) + (span - 1) * getUiGap(*this);
        }
        void mouseDown(const juce::MouseEvent&) override;

        template <typename Control>
        void bindAuxiliary(Control& field, juce::AudioProcessorValueTreeState& state,
                           const juce::String& id, const ava::modules::parameters::ControlPolicy& policy,
                           ParameterControl* gain = nullptr)
        {
            if (id.isEmpty()) return;
            const auto& metadata = policy.metadata;
            jassert(metadata.auxiliaryLabel[0] != '\0');
            auxiliaryIndicator = std::make_unique<AccessoryButton>(metadata.auxiliaryLabel);
            auxiliaryIndicator->setButtonText(metadata.auxiliaryLabel);
            auxiliaryIndicator->bindParameter(state, id);
            auxiliaryIndicator->setEnabled(policy.enabled);
            if (metadata.gainWithMute && gain != nullptr)
                auxiliaryIndicator->bindLowerMuteThreshold(*gain, ava::modules::dsp::gainMuteThresholdDb);
            field.setBottomAccessory(auxiliaryIndicator.get());
        }
        std::unique_ptr<AccessoryButton> auxiliaryIndicator;

        int blockIndex = -1;
        int gainIndex = -1;
        bool layoutHidden = false;
        juce::String label;
        bool hasTopGap = true;
        int controlsInRow = 1;
    };

    struct CustomRow final:public RowBase {
        explicit CustomRow(std::unique_ptr<juce::Component> fieldIn):field(std::move(fieldIn)){if(field)addAndMakeVisible(*field);}
        int getPreferredHeight() const override {return getSquareControlSize(*this);}
        bool isValueField() const noexcept override {return true;}
        void resized() override {if(field)field->setBounds(getLocalBounds());}
        std::unique_ptr<juce::Component> field;
    };
    struct ParameterGroup final : public RowBase
    {
        ParameterGroup(juce::String name, std::vector<std::unique_ptr<RowBase>> membersIn);
        ~ParameterGroup() override;
        void enableHorizontalLayout();
        int getPreferredHeight() const override;
        int getTopGap() const noexcept override;
        int getPreferredWidth() const override;
        void refreshExternalState() override;
        void resized() override;
        std::vector<std::unique_ptr<RowBase>> members;
        std::unique_ptr<HeaderControlRow> horizontalRow;
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
        void refreshInteractionState();

        CrossoverRangePage& page;
        CrossoverModuleComponent& owner;
        CrossoverControlSpec controlSpec;
        std::unique_ptr<ParameterControl> control;
        bool isValueField() const noexcept override { return true; }
        int getCellSpan() const noexcept override { return control != nullptr ? control->getLayoutCellSpan() : 1; }
    };

    struct ChoiceRow final : public RowBase
    {
        ChoiceRow(CrossoverModuleComponent& ownerIn, size_t rangeIndexIn,
                  const juce::String& parameterId,
                  const juce::String& auxiliaryToggleId,
                  const CrossoverControlSpec& spec);
        int getPreferredHeight() const override;
        void resized() override;
        void refreshExternalState() override;
        CrossoverModuleComponent& owner;
        size_t rangeIndex = 0;
        juce::String suffix;
        std::unique_ptr<ChoiceControl> control;
        bool isValueField() const noexcept override { return true; }
        int getCellSpan() const noexcept override { return control != nullptr ? control->getLayoutCellSpan() : 1; }
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

        CrossoverRangePage& page;
        CrossoverModuleComponent& owner;
        juce::String parameterIdToToggle, suffix;
        std::unique_ptr<ParameterControl> control;
        bool isValueField() const noexcept override { return true; }
        int getCellSpan() const noexcept override { return control != nullptr ? control->getLayoutCellSpan() : 1; }
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
        bool isValueField() const noexcept override { return true; }
        int getCellSpan() const noexcept override { return 2; }

        CrossoverModuleComponent& owner;
        juce::String degreeParameterIdToRead;
        juce::String flipParameterIdToRead;
        SquareTextLabel value;
    };

    struct TimeRow final : public RowBase
    {
        TimeRow(CrossoverModuleComponent& ownerIn, size_t rangeIndexIn,
                juce::String valueParameterId,
                juce::String modeParameterId,
                juce::String syncParameterId,
                const CrossoverControlSpec& spec);
        int getPreferredHeight() const override;
        void refreshExternalState() override;
        void resized() override;
        bool isNoteMode() const noexcept;
        int getTimeModeIndex() const noexcept;
        juce::StringArray getNoteChoices() const;
        void updateModeControl();

        CrossoverControlSpec controlSpec;
        size_t rangeIndex;
        CrossoverModuleComponent& owner;
        juce::String valueParameterIdToEdit;
        juce::String modeParameterIdToEdit;
        juce::String noteParameterIdToEdit;
        std::unique_ptr<ChoiceControl> noteControl;
        std::unique_ptr<ParameterControl> control;
        std::unique_ptr<AccessoryButton> modeIndicator;
        bool isValueField() const noexcept override { return true; }
        int getCellSpan() const noexcept override
        {
            return isNoteMode() ? noteControl->getLayoutCellSpan() : control->getLayoutCellSpan();
        }
    };

    juce::String getCrossoverRangeParameterId(const CrossoverControlSpec& spec) const;
    void listenToParameter(const juce::String& id);
    void addControlSpecs(const std::vector<CrossoverControlSpec>& specs, juce::Component& parent);
    void groupBlockControls();
    void handleAsyncUpdate() override;
    void refreshRows();
    void parameterChanged(const juce::String& parameterID, float) override;

    CrossoverModuleComponent& owner;
    size_t rangeIndex = 0;
    std::vector<std::unique_ptr<RowBase>> rows;
    ava::modules::chain::gain::Order displayedGainOrder = ava::modules::chain::gain::defaultOrder;
    std::unique_ptr<HeaderControlRow> dynRow;
    std::vector<juce::String> listenedParameterIds;
};
