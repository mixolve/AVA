#include "ComponentConfig.h"
#include "ParameterIds.h"
#include "../shell/Processor.h"
#include "../shell/Editor.h"
#include "../shell/Style.h"
#include "../modules/tls/Processor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/trs/Processor.h"

namespace crossover_configs
{
namespace
{
using CrossoverControlSpec = CrossoverModuleComponent::CrossoverControlSpec;
using ControlKind = CrossoverModuleComponent::ControlKind;

CrossoverControlSpec headingControl(const char* label, const int topGapMultiplier = 1)
{
    CrossoverControlSpec spec;
    spec.kind = ControlKind::heading;
    spec.label = label;
    spec.topGapMultiplier = topGapMultiplier;
    return spec;
}

CrossoverControlSpec parameterControl(const char* suffix,
                                 const char* label,
                                 const int decimals,
                                 const int sourceRangeIndex = -1,
                                 const int topGapMultiplier = 1,
                                 const char* enabledWhenSuffix = "")
{
    CrossoverControlSpec spec;
    spec.kind = ControlKind::parameter;
    spec.suffix = suffix;
    spec.label = label;
    spec.decimals = decimals;
    spec.sourceRangeIndex = sourceRangeIndex;
    spec.topGapMultiplier = topGapMultiplier;
    spec.enabledWhenSuffix = enabledWhenSuffix;
    return spec;
}

CrossoverControlSpec choiceControl(const char* suffix,
                              const char* label,
                              const int sourceRangeIndex = -1,
                              const int topGapMultiplier = 1)
{
    CrossoverControlSpec spec;
    spec.kind = ControlKind::choice;
    spec.suffix = suffix;
    spec.label = label;
    spec.sourceRangeIndex = sourceRangeIndex;
    spec.topGapMultiplier = topGapMultiplier;
    return spec;
}

CrossoverControlSpec parameterToggleControl(const char* suffix,
                                       const char* label,
                                       const int decimals,
                                       const char* toggleSuffix,
                                       const char* toggleLabel,
                                       const char* reorderGroup = "",
                                       const char* orderSuffix = "",
                                       const bool fixedOrder = false,
                                       const bool toggleInverted = false,
                                       const int parameterTitleWidth = 0,
                                       const int auxiliaryToggleWidth = 0,
                                       const char* auxiliaryToggleIcon = "")
{
    auto spec = parameterControl(suffix, label, decimals);
    spec.auxiliaryToggleSuffix = toggleSuffix;
    spec.auxiliaryToggleLabel = toggleLabel;
    spec.reorderGroup = reorderGroup;
    spec.orderSuffix = orderSuffix;
    spec.fixedOrder = fixedOrder;
    spec.auxiliaryToggleInverted = toggleInverted;
    spec.parameterTitleWidth = parameterTitleWidth;
    spec.auxiliaryToggleWidth = auxiliaryToggleWidth;
    spec.auxiliaryToggleIcon = auxiliaryToggleIcon;
    return spec;
}

CrossoverControlSpec toggleControl(const char* suffix,
                              const char* label,
                              const char* enabledLabel = "",
                              const char* disabledLabel = "",
                              const char* exclusiveGroup = "",
                              const int topGapMultiplier = 1,
                              const int controlsInRow = 1,
                              const bool toggleAccentVisible = true)
{
    CrossoverControlSpec spec;
    spec.kind = ControlKind::toggle;
    spec.suffix = suffix;
    spec.label = label;
    spec.enabledLabel = enabledLabel;
    spec.disabledLabel = disabledLabel;
    spec.exclusiveGroup = exclusiveGroup;
    spec.topGapMultiplier = topGapMultiplier;
    spec.controlsInRow = controlsInRow;
    spec.toggleAccentVisible = toggleAccentVisible;
    return spec;
}

CrossoverControlSpec inactiveControl(const char* label)
{
    CrossoverControlSpec spec;
    spec.kind = ControlKind::inactive;
    spec.label = label;
    return spec;
}

CrossoverControlSpec readoutControl(const char* suffix,
                               const char* label,
                               const char* modeSuffix,
                               const int topGapMultiplier = 1)
{
    CrossoverControlSpec spec;
    spec.kind = ControlKind::readout;
    spec.suffix = suffix;
    spec.label = label;
    spec.modeSuffix = modeSuffix;
    spec.topGapMultiplier = topGapMultiplier;
    return spec;
}

CrossoverControlSpec timeControl(const char* suffix,
                            const char* label,
                            const char* modeSuffix,
                            const char* syncSuffix,
                            const int topGapMultiplier = 1,
                            const bool showModeButton = true)
{
    CrossoverControlSpec spec;
    spec.kind = ControlKind::time;
    spec.suffix = suffix;
    spec.label = label;
    spec.decimals = 2;
    spec.modeSuffix = modeSuffix;
    spec.syncSuffix = syncSuffix;
    spec.topGapMultiplier = topGapMultiplier;
    spec.showTimeModeButton = showModeButton;
    return spec;
}
}

CrossoverModuleComponent::Config makeCrossoverConfig(AvaAudioProcessor& processor)
{
    CrossoverModuleComponent::Config config;
    config.processorIdentity = &processor;
    config.moduleKey = "crossover";
    config.valueTreeState = &processor.getValueTreeState();
    config.markParametersDirty = [&processor] { processor.notifyHostOfStateChange(); };
    config.makeCrossoverParameterId = [] (const char* suffix)
    {
        return AvaAudioProcessor::getCrossoverParameterId(suffix);
    };
    config.makeCrossoverSoloParameterId = [] (const size_t rangeIndex)
    {
        return AvaAudioProcessor::getCrossoverSoloParameterId(rangeIndex);
    };
    config.makeCrossoverSplitCountParameterId = []
    {
        return juce::String(AvaAudioProcessor::paramCrossoverActiveSplitCountId);
    };
    config.crossoverDecimals = 2;
    config.startOnCrossoverSettings = false;
    config.showModuleHeading = false;
    config.crossoverSettingsHeading = "CROSSOVER SETTINGS";
    return config;
}

CrossoverModuleComponent::Config makeTlsCrossoverConfig(TlsModuleProcessor& processor)
{
    CrossoverModuleComponent::Config config;
    config.processorIdentity = &processor;
    config.moduleKey = "tls";
    config.valueTreeState = &processor.getValueTreeState();
    config.undoManager = &processor.getUndoManager();
    config.markParametersDirty = [&processor] { processor.markParametersDirty(); };
    config.makeRangeParameterId = ava::crossover::parameters::makeRangeParameterId;
    config.crossoverDecimals = 2;
    config.showCrossoverControls = false;
    config.showCrossoverNavigation = false;
    config.showCrossoverSolo = false;
    config.pinModuleHeading = true;
    config.rangeControls = {
        headingControl("LISTEN", 0),
        toggleControl("listen_lc", "LC", "", "", "listen", 1, 4),
        toggleControl("listen_rc", "RC", "", "", "listen"),
        toggleControl("listen_mc", "MC", "", "", "listen"),
        toggleControl("listen_sc", "SC", "", "", "listen"),
        toggleControl("listen_ll", "LL", "", "", "listen", 1, 4),
        toggleControl("listen_rr", "RR", "", "", "listen"),
        inactiveControl("MM"),
        toggleControl("listen_ss", "SS", "", "", "listen"),

        headingControl("GAIN", 2),
        parameterToggleControl("gain_stereo", "STEREO", 2, "gain_stereo_mute", "MUTE", "gain", "", true, false, 95, iconControlSize, "volume-off"),
        parameterToggleControl("gain_left", "LEFT", 2, "gain_left_mute", "MUTE", "gain", "gain_left_order", false, false, 95, iconControlSize, "volume-off"),
        parameterToggleControl("gain_right", "RIGHT", 2, "gain_right_mute", "MUTE", "gain", "gain_right_order", false, false, 95, iconControlSize, "volume-off"),
        parameterToggleControl("gain_mid", "MID", 2, "gain_mid_mute", "MUTE", "gain", "gain_mid_order", false, false, 95, iconControlSize, "volume-off"),
        parameterToggleControl("gain_side", "SIDE", 2, "gain_side_mute", "MUTE", "gain", "gain_side_order", false, false, 95, iconControlSize, "volume-off"),

        headingControl("DELAY", 2),
        parameterControl("stereo_delay", "STEREO", 2),
        parameterControl("left_delay", "LEFT", 2),
        parameterControl("right_delay", "RIGHT", 2),

        headingControl("PHASE", 2),
        parameterControl("stereo_phase", "STEREO", 2),
        parameterControl("left_phase", "LEFT", 2),
        parameterControl("right_phase", "RIGHT", 2),

        headingControl("PANORAMA", 2),
        parameterControl("left", "LEFT", 2),
        parameterControl("right", "RIGHT", 2),
        parameterControl("law", "LAW", 2),

        headingControl("SHEAR", 2),
        parameterControl("impact", "IMPACT", 2),
        choiceControl("impact_direction", "DIRECTION"),

        headingControl("MS BALANCE", 2),
        parameterControl("mid", "MID", 2),
        parameterControl("side", "SIDE", 2),

        headingControl("ORTHOGONAL", 2),
        parameterControl("degree", "DEGREE", 2),
        toggleControl("flip_right", "FLIP RIGHT"),
        readoutControl("degree", "POSITION", "flip_right"),

        headingControl("RECTIFICATION", 2),
        toggleControl("half_positive", "HPOS", "", "", "rectification", 1, 4),
        toggleControl("half_negative", "HNEG", "", "", "rectification"),
        toggleControl("full_positive", "FPOS", "", "", "rectification"),
        toggleControl("full_negative", "FNEG", "", "", "rectification"),
    };
    return config;
}

CrossoverModuleComponent::Config makeDynCrossoverConfig(DynModuleProcessor& processor)
{
    CrossoverModuleComponent::Config config;
    config.processorIdentity = &processor;
    config.moduleKey = "dyn";
    config.valueTreeState = &processor.getValueTreeState();
    config.undoManager = &processor.getUndoManager();
    config.markParametersDirty = [&processor] { processor.markParametersDirty(); };
    config.makeRangeParameterId = ava::crossover::parameters::makeRangeParameterId;
    config.showCrossoverControls = false;
    config.showCrossoverNavigation = false;
    config.showCrossoverSolo = false;
    config.pinModuleHeading = true;
    config.rangeControls = {
        headingControl("GENERAL", 0),
        parameterControl("morph", "MORPH", 2, 0),
        parameterControl("ratio", "RATIO", 2, 0),
        parameterControl("knee", "KNEE", 2, 0),
        parameterControl("peak_hold", "PEAK-HOLD", 2, 0),
        parameterControl("lookahead", "LOOKAHEAD", 2, 0),
        parameterControl("tension_floor", "TEN-FLOOR", 2, 0),
        parameterControl("tension_hysteresis", "TEN-HYST", 2, 0),
        choiceControl("release_form", "REL-FORM", 0),
        parameterControl("release_curve", "REL-CURVE", 2, 0, 1, "release_form"),
        headingControl("ADAPTIVE SETTINGS", 2),
        parameterControl("adaptive_offset", "OFFSET", 2),
        parameterControl("adaptive_attack", "ATTACK", 2),
        parameterControl("adaptive_hold", "HOLD", 2),
        parameterControl("adaptive_release", "RELEASE", 2),
        headingControl("LINKING", 2),
        toggleControl("link_up_down", "UPDN (DUAL-MONO)"),
        toggleControl("link_left_right", "LR (STEREO)"),
        toggleControl("link_opposite", "OPP"),
        parameterControl("left_up_threshold", "L.UP.THR", 2, -1, 2),
        parameterControl("left_up_adaptive", "L.UP.ADAPTIVE", 2),
        parameterControl("left_up_tension", "L.UP.TENS", 2),
        parameterControl("left_up_release", "L.UP.REL", 2),
        parameterControl("left_up_output", "L.UP.OUT", 2),
        parameterControl("left_down_threshold", "L.DN.THR", 2),
        parameterControl("left_down_adaptive", "L.DN.ADAPTIVE", 2),
        parameterControl("left_down_tension", "L.DN.TENS", 2),
        parameterControl("left_down_release", "L.DN.REL", 2),
        parameterControl("left_down_output", "L.DN.OUT", 2),
        parameterControl("right_up_threshold", "R.UP.THR", 2),
        parameterControl("right_up_adaptive", "R.UP.ADAPTIVE", 2),
        parameterControl("right_up_tension", "R.UP.TENS", 2),
        parameterControl("right_up_release", "R.UP.REL", 2),
        parameterControl("right_up_output", "R.UP.OUT", 2),
        parameterControl("right_down_threshold", "R.DN.THR", 2),
        parameterControl("right_down_adaptive", "R.DN.ADAPTIVE", 2),
        parameterControl("right_down_tension", "R.DN.TENS", 2),
        parameterControl("right_down_release", "R.DN.REL", 2),
        parameterControl("right_down_output", "R.DN.OUT", 2),
    };
    config.rangeTailControls = {
        toggleControl("delta", "DELTA"),
    };
    return config;
}

CrossoverModuleComponent::Config makeTrsCrossoverConfig(TrsModuleProcessor& processor,
                                                        AvaAudioProcessorEditor& editor,
                                                        std::unique_ptr<juce::Component>& editorHolder)
{
    CrossoverModuleComponent::Config config;
    config.processorIdentity = &processor;
    config.moduleKey = "trs";
    config.valueTreeState = &processor.getValueTreeState();
    config.makeRangeParameterId = ava::crossover::parameters::makeRangeParameterId;
    config.showCrossoverControls = false;
    config.showCrossoverNavigation = false;
    config.showCrossoverSolo = false;
    config.pinModuleHeading = true;
    config.rangeControls = {
        headingControl("TRANSIENT", 0),
        parameterToggleControl(TrsModuleProcessor::paramTransientGainId,
                               "GAIN",
                               2,
                               TrsModuleProcessor::paramTransientEnabledId,
                               "MUTE",
                               "",
                               "",
                               false,
                               true),
        headingControl("SUSTAIN", 2),
        parameterToggleControl(TrsModuleProcessor::paramSustainGainId,
                               "GAIN",
                               2,
                               TrsModuleProcessor::paramSustainEnabledId,
                               "MUTE",
                               "",
                               "",
                               false,
                               true),
        timeControl(TrsModuleProcessor::paramHoldId,
                    "HOLD",
                    TrsModuleProcessor::paramHoldModeId,
                    TrsModuleProcessor::paramHoldSyncId,
                    2,
                    false),
        choiceControl(TrsModuleProcessor::paramHoldModeId, "HOLD-TYPE"),
        timeControl(TrsModuleProcessor::paramReleaseId,
                    "RELEASE",
                    TrsModuleProcessor::paramReleaseModeId,
                    TrsModuleProcessor::paramReleaseSyncId,
                    1,
                    false),
        choiceControl(TrsModuleProcessor::paramReleaseModeId, "REL-TYPE"),
        parameterControl(TrsModuleProcessor::paramReleaseCurveId, "REL-CURVE", 2),
        parameterControl(TrsModuleProcessor::paramLookaheadId, "LOOKAHEAD", 2),
        headingControl("SENSITIVITY", 2),
        parameterControl(TrsModuleProcessor::paramThresholdId, "THRESH", 2),
        parameterControl(TrsModuleProcessor::paramKneeId, "KNEE", 2),
        parameterControl(TrsModuleProcessor::paramRetriggerId, "RETRIGGER", 2),
        toggleControl(TrsModuleProcessor::paramOneShotId, "ONE-SHOT"),
    };
    config.getHostSyncChoices = []
    {
        return TrsModuleProcessor::getHostSyncChoices();
    };
    config.getDefaultHostSyncChoiceIndex = []
    {
        return TrsModuleProcessor::getDefaultHostSyncChoiceIndex();
    };
    config.showChoicePrompt = [&editor, &editorHolder] (const juce::Rectangle<int>& anchorBounds,
                                                        const juce::StringArray& choices,
                                                        const int selectedIndex,
                                                        std::vector<bool> itemEnabledStates,
                                                        const juce::Justification itemJustification,
                                                        std::function<void(int)> onSelect,
                                                        std::function<void()> onClose,
                                                        std::function<void()> onDismiss)
    {
        editor.showChoicePrompt(editor.getLocalArea(editorHolder.get(), anchorBounds),
                                choices,
                                selectedIndex,
                                std::move(itemEnabledStates),
                                itemJustification,
                                std::move(onSelect),
                                std::move(onClose),
                                std::move(onDismiss));
    };
    config.clearKeyboardFocus = [&editor]
    {
        clearKeyboardFocus(editor);
    };
    return config;
}
}
