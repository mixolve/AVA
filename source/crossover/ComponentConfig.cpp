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
    config.crossoverSettingsHeading = "CROSSOVER";
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
        toggleControl("lc", "LC", "", "", "listen", 1, 4),
        toggleControl("rc", "RC", "", "", "listen"),
        toggleControl("mc", "MC", "", "", "listen"),
        toggleControl("sc", "SC", "", "", "listen"),
        toggleControl("ll", "LL", "", "", "listen", 1, 4),
        toggleControl("rr", "RR", "", "", "listen"),
        inactiveControl("MM"),
        toggleControl("ss", "SS", "", "", "listen"),

        headingControl("GAIN", 2),
        parameterToggleControl("stereo.gain", "STEREO", 2, "stereo.mute.icon", "MUTE", "gain", "", true, false, 95, iconControlSize, "volume-off"),
        parameterToggleControl("left.gain", "LEFT", 2, "left.mute.icon", "MUTE", "gain", "left-order", false, false, 95, iconControlSize, "volume-off"),
        parameterToggleControl("right.gain", "RIGHT", 2, "right.mute.icon", "MUTE", "gain", "right-order", false, false, 95, iconControlSize, "volume-off"),
        parameterToggleControl("mid.gain", "MID", 2, "mid.mute.icon", "MUTE", "gain", "mid-order", false, false, 95, iconControlSize, "volume-off"),
        parameterToggleControl("side.gain", "SIDE", 2, "side.mute.icon", "MUTE", "gain", "side-order", false, false, 95, iconControlSize, "volume-off"),

        headingControl("DELAY", 2),
        parameterControl("stereo.delay", "STEREO", 2),
        parameterControl("left.delay", "LEFT", 2),
        parameterControl("right.delay", "RIGHT", 2),

        headingControl("PHASE", 2),
        parameterControl("stereo.phase", "STEREO", 2),
        parameterControl("left.phase", "LEFT", 2),
        parameterControl("right.phase", "RIGHT", 2),

        headingControl("PANORAMA", 2),
        parameterControl("left.pan", "LEFT", 2),
        parameterControl("right.pan", "RIGHT", 2),
        parameterControl("law", "LAW", 2),

        headingControl("SHEAR", 2),
        parameterControl("impact", "IMPACT", 2),
        choiceControl("direction", "DIRECTION"),

        headingControl("MS BALANCE", 2),
        parameterControl("mid.balance", "MID", 2),
        parameterControl("side.balance", "SIDE", 2),

        headingControl("ORTHOGONAL", 2),
        parameterControl("degree", "DEGREE", 2),
        toggleControl("flip-right", "FLIP RIGHT"),
        readoutControl("degree", "POSITION", "flip-right"),

        headingControl("RECTIFICATION", 2),
        toggleControl("hpos", "HPOS", "", "", "rectification", 1, 4),
        toggleControl("hneg", "HNEG", "", "", "rectification"),
        toggleControl("fpos", "FPOS", "", "", "rectification"),
        toggleControl("fneg", "FNEG", "", "", "rectification"),
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
        headingControl("MAIN", 0),
        parameterControl("morph", "MORPH", 2, 0),
        parameterControl("ratio", "RATIO", 2, 0),
        parameterControl("knee", "KNEE", 2, 0),
        parameterControl("peak-hold", "PEAK-HOLD", 2, 0),
        parameterControl("lookahead", "LOOKAHEAD", 2, 0),
        parameterControl("tension-floor", "TENSION-FLOOR", 2, 0),
        parameterControl("tension-hysteresis", "TENSION-HYSTERESIS", 2, 0),
        choiceControl("release-form", "RELEASE-FORM", 0),
        parameterControl("release-curve", "RELEASE-CURVE", 2, 0, 1, "release-form"),
        headingControl("LINKING", 2),
        toggleControl("up-dn", "UP-DN (DUAL-MONO)"),
        toggleControl("l-r", "L-R (STEREO)"),
        toggleControl("opposite", "OPPOSITE"),
        headingControl("PROCESSOR", 2),
        parameterControl("l-up-threshold", "L-UP-THRESHOLD", 2),
        parameterControl("l-up-adaptive", "L-UP-ADAPTIVE", 2),
        parameterControl("l-up-tension", "L-UP-TENSION", 2),
        parameterControl("l-up-release", "L-UP-RELEASE", 2),
        parameterControl("l-up-output", "L-UP-OUTPUT", 2),
        parameterControl("l-dn-threshold", "L-DN-THRESHOLD", 2, -1, 2),
        parameterControl("l-dn-adaptive", "L-DN-ADAPTIVE", 2),
        parameterControl("l-dn-tension", "L-DN-TENSION", 2),
        parameterControl("l-dn-release", "L-DN-RELEASE", 2),
        parameterControl("l-dn-output", "L-DN-OUTPUT", 2),
        parameterControl("r-up-threshold", "R-UP-THRESHOLD", 2, -1, 2),
        parameterControl("r-up-adaptive", "R-UP-ADAPTIVE", 2),
        parameterControl("r-up-tension", "R-UP-TENSION", 2),
        parameterControl("r-up-release", "R-UP-RELEASE", 2),
        parameterControl("r-up-output", "R-UP-OUTPUT", 2),
        parameterControl("r-dn-threshold", "R-DN-THRESHOLD", 2, -1, 2),
        parameterControl("r-dn-adaptive", "R-DN-ADAPTIVE", 2),
        parameterControl("r-dn-tension", "R-DN-TENSION", 2),
        parameterControl("r-dn-release", "R-DN-RELEASE", 2),
        parameterControl("r-dn-output", "R-DN-OUTPUT", 2),
        headingControl("ADAPTIVE", 2),
        parameterControl("offset", "OFFSET", 2),
        parameterControl("attack", "ATTACK", 2),
        parameterControl("hold", "HOLD", 2),
        parameterControl("release", "RELEASE", 2),
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
                               TrsModuleProcessor::paramTransientMuteId,
                               "MUTE",
                               "",
                               "",
                               false,
                               false,
                               95,
                               iconControlSize,
                               "volume-off"),
        headingControl("SUSTAIN", 2),
        parameterToggleControl(TrsModuleProcessor::paramSustainGainId,
                               "GAIN",
                               2,
                               TrsModuleProcessor::paramSustainMuteId,
                               "MUTE",
                               "",
                               "",
                               false,
                               false,
                               95,
                               iconControlSize,
                               "volume-off"),
        timeControl(TrsModuleProcessor::paramHoldId,
                    "HOLD",
                    TrsModuleProcessor::paramHoldTypeId,
                    TrsModuleProcessor::paramHoldSyncId,
                    2,
                    false),
        choiceControl(TrsModuleProcessor::paramHoldTypeId, "HOLD-TYPE"),
        timeControl(TrsModuleProcessor::paramReleaseId,
                    "RELEASE",
                    TrsModuleProcessor::paramReleaseTypeId,
                    TrsModuleProcessor::paramReleaseSyncId,
                    1,
                    false),
        choiceControl(TrsModuleProcessor::paramReleaseTypeId, "RELEASE-TYPE"),
        parameterControl(TrsModuleProcessor::paramReleaseCurveId, "RELEASE-CURVE", 2),
        parameterControl(TrsModuleProcessor::paramLookaheadId, "LOOKAHEAD", 2),
        headingControl("SENSITIVITY", 2),
        parameterControl(TrsModuleProcessor::paramThresholdId, "THRESHOLD", 2),
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
