#include "Configs.h"
#include "ParameterIds.h"
#include "../shell/UiConstants.h"
#include "../shell/EditorFilterSection.h"
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
                                       const char* auxiliaryToggleSymbol = "")
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
    spec.auxiliaryToggleSymbol = auxiliaryToggleSymbol;
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
} // namespace

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

CrossoverModuleComponent::Config makeTlsCrossoverConfig(TlsAudioProcessor& processor)
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
        toggleControl("listenLc", "LC", "", "", "listen", 1, 4),
        toggleControl("listenRc", "RC", "", "", "listen"),
        toggleControl("listenMc", "MC", "", "", "listen"),
        toggleControl("listenSc", "SC", "", "", "listen"),
        toggleControl("listenLl", "LL", "", "", "listen", 1, 4),
        toggleControl("listenRr", "RR", "", "", "listen"),
        inactiveControl("MM"),
        toggleControl("listenSs", "SS", "", "", "listen"),

        headingControl("GAIN", 2),
        parameterToggleControl("gainLr", "STEREO", 2, "gainLrMute", "MUTE", "gain", "", true, false, 95, iconControlSize, "speaker.slash"),
        parameterToggleControl("gainL", "LEFT", 2, "gainLMute", "MUTE", "gain", "gainLOrder", false, false, 95, iconControlSize, "speaker.slash"),
        parameterToggleControl("gainR", "RIGHT", 2, "gainRMute", "MUTE", "gain", "gainROrder", false, false, 95, iconControlSize, "speaker.slash"),
        parameterToggleControl("gainMid", "MID", 2, "gainMidMute", "MUTE", "gain", "gainMidOrder", false, false, 95, iconControlSize, "speaker.slash"),
        parameterToggleControl("gainSide", "SIDE", 2, "gainSideMute", "MUTE", "gain", "gainSideOrder", false, false, 95, iconControlSize, "speaker.slash"),

        headingControl("DELAY", 2),
        parameterControl("stereoDelay", "STEREO", 2),
        parameterControl("leftDelay", "LEFT", 2),
        parameterControl("rightDelay", "RIGHT", 2),

        headingControl("PHASE", 2),
        parameterControl("leftPhase", "LEFT", 2),
        parameterControl("rightPhase", "RIGHT", 2),

        headingControl("PANORAMA", 2),
        parameterControl("left", "LEFT", 2),
        parameterControl("right", "RIGHT", 2),
        parameterControl("law", "LAW", 2),

        headingControl("SHEAR", 2),
        parameterControl("impact", "IMPACT", 2),
        choiceControl("impactDirection", "DIRECTION"),

        headingControl("MS BALANCE", 2),
        parameterControl("mid", "MID", 2),
        parameterControl("side", "SIDE", 2),

        headingControl("ORTHOGONAL", 2),
        parameterControl("degree", "DEGREE", 2),
        toggleControl("flipRight", "FLIP RIGHT"),
        readoutControl("degree", "POSITION", "flipRight"),

        headingControl("RECTIFICATION", 2),
        toggleControl("halfPositive", "HPOS", "", "", "rectification", 1, 4),
        toggleControl("halfNegative", "HNEG", "", "", "rectification"),
        toggleControl("fullPositive", "FPOS", "", "", "rectification"),
        toggleControl("fullNegative", "FNEG", "", "", "rectification"),
    };
    return config;
}

CrossoverModuleComponent::Config makeDynCrossoverConfig(DynAudioProcessor& processor)
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
        headingControl("ADAP SETTINGS", 2),
        parameterControl("adaptive_offset", "OFFSET", 2),
        parameterControl("adaptive_attack", "ATTACK", 2),
        parameterControl("adaptive_hold", "HOLD", 2),
        parameterControl("adaptive_release", "RELEASE", 2),
        headingControl("LINKING", 2),
        toggleControl("linkUpDown", "UPDN (DUAL-MONO)"),
        toggleControl("linkLeftRight", "LR (STEREO)"),
        toggleControl("linkOpposite", "OPP"),
        parameterControl("leftUpThreshold", "L.UP.THR", 2, -1, 2),
        parameterControl("leftUpAdaptive", "L.UP.ADAP", 2),
        parameterControl("leftUpTension", "L.UP.TENS", 2),
        parameterControl("leftUpRelease", "L.UP.REL", 2),
        parameterControl("leftUpOutput", "L.UP.OUT", 2),
        parameterControl("leftDownThreshold", "L.DN.THR", 2),
        parameterControl("leftDownAdaptive", "L.DN.ADAP", 2),
        parameterControl("leftDownTension", "L.DN.TENS", 2),
        parameterControl("leftDownRelease", "L.DN.REL", 2),
        parameterControl("leftDownOutput", "L.DN.OUT", 2),
        parameterControl("rightUpThreshold", "R.UP.THR", 2),
        parameterControl("rightUpAdaptive", "R.UP.ADAP", 2),
        parameterControl("rightUpTension", "R.UP.TENS", 2),
        parameterControl("rightUpRelease", "R.UP.REL", 2),
        parameterControl("rightUpOutput", "R.UP.OUT", 2),
        parameterControl("rightDownThreshold", "R.DN.THR", 2),
        parameterControl("rightDownAdaptive", "R.DN.ADAP", 2),
        parameterControl("rightDownTension", "R.DN.TENS", 2),
        parameterControl("rightDownRelease", "R.DN.REL", 2),
        parameterControl("rightDownOutput", "R.DN.OUT", 2),
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
        parameterToggleControl(TrsModuleProcessor::paramTransGainId,
                               "GAIN",
                               2,
                               TrsModuleProcessor::paramTransOnId,
                               "MUTE",
                               "",
                               "",
                               false,
                               true),
        headingControl("SUSTAIN", 2),
        parameterToggleControl(TrsModuleProcessor::paramSusGainId,
                               "GAIN",
                               2,
                               TrsModuleProcessor::paramSusOnId,
                               "MUTE",
                               "",
                               "",
                               false,
                               true),
        timeControl(TrsModuleProcessor::paramTimeHoldId,
                    "HOLD",
                    TrsModuleProcessor::paramTimeHoldModeId,
                    TrsModuleProcessor::paramTimeHoldSyncId,
                    2,
                    false),
        choiceControl(TrsModuleProcessor::paramTimeHoldModeId, "HOLD-TYPE"),
        timeControl(TrsModuleProcessor::paramTimeReleaseId,
                    "RELEASE",
                    TrsModuleProcessor::paramTimeReleaseModeId,
                    TrsModuleProcessor::paramTimeReleaseSyncId,
                    1,
                    false),
        choiceControl(TrsModuleProcessor::paramTimeReleaseModeId, "REL-TYPE"),
        parameterControl(TrsModuleProcessor::paramTimeReleaseCurveId, "REL-CURVE", 2),
        parameterControl(TrsModuleProcessor::paramLookaheadId, "LOOKAHEAD", 2),
        headingControl("SENSITIVITY", 2),
        parameterControl(TrsModuleProcessor::paramSensThresholdId, "THRESH", 2),
        parameterControl(TrsModuleProcessor::paramSensKneeId, "KNEE", 2),
        parameterControl(TrsModuleProcessor::paramSensRetriggerId, "RETRIGGER", 2),
        toggleControl(TrsModuleProcessor::paramSensOneShotId, "ONE-SHOT"),
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
} // namespace crossover_configs
