#include "ModuleBindings.h"
#include "../shell/EditorSetup.h"
#include "ParameterIds.h"
#include "../shell/Processor.h"
#include "../shell/Editor.h"
#include "../shell/Style.h"
#include "../modules/tls/Processor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/shared/DspUtilities.h"

#include <algorithm>
#include <iterator>

namespace crossover_configs
{
namespace
{
using CrossoverControlSpec = CrossoverModuleComponent::CrossoverControlSpec;
using ControlKind = CrossoverModuleComponent::ControlKind;
using ExclusiveScope = CrossoverModuleComponent::ExclusiveScope;

void configureChoicePrompts(CrossoverModuleComponent::Config& config,
                            AvaAudioProcessorEditor& editor,
                            std::unique_ptr<juce::Component>& editorHolder)
{
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
}

CrossoverControlSpec parameterControl(const char* suffix,
                                 const char* label,
                                 const int decimals,
                                 const int sourceRangeIndex = -1,
                                 const int topGapMultiplier = 1,
                                 const char* enabledWhenSuffix = "",
                                 const bool enabledWhenBelowMaximum = false,
                                 const bool enabledWhenAboveMinimum = false)
{
    CrossoverControlSpec spec;
    spec.kind = ControlKind::parameter;
    spec.suffix = suffix;
    spec.label = label;
    spec.decimals = decimals;
    spec.sourceRangeIndex = sourceRangeIndex;
    spec.topGapMultiplier = topGapMultiplier;
    spec.enabledWhenSuffix = enabledWhenSuffix;
    spec.enabledWhenBelowMaximum = enabledWhenBelowMaximum;
    spec.enabledWhenAboveMinimum = enabledWhenAboveMinimum;
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
                                       const bool toggleInverted = false,
                                       const int parameterTitleWidth = 0,
                                       const int auxiliaryToggleWidth = 0,
                                       const char* auxiliaryToggleIcon = "")
{
    auto spec = parameterControl(suffix, label, decimals);
    spec.auxiliaryToggleSuffix = toggleSuffix;
    spec.auxiliaryToggleLabel = toggleLabel;
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
                              const bool toggleActiveFillEnabled = true)
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
    spec.toggleActiveFillEnabled = toggleActiveFillEnabled;
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

bool isRangeControlEnabled(const CrossoverModuleComponent::CrossoverControlSpec& control,
                           const juce::AudioProcessorValueTreeState& state,
                           const size_t rangeIndex, const juce::String& moduleKey)
{
    if (control.interactionDisabled) return false;
    const auto sourceRange = control.sourceRangeIndex >= 0
        ? static_cast<size_t>(juce::jlimit(0, static_cast<int>(ava::crossover::BufferRouter::numRanges - 1), control.sourceRangeIndex))
        : rangeIndex;
    const auto read = [&state, sourceRange, &moduleKey] (const char* suffix)
    {
        const auto* value = state.getRawParameterValue(ava::crossover::parameters::makeRangeParameterId(sourceRange, suffix));
        const auto rawValue = value != nullptr ? value->load() : 0.0f;
        // Use the same effective zero as DYN's DSP, rather than float residue
        // from normalised host/UI values that still displays as 0.00.
        if (moduleKey == "dyn" && (juce::String(suffix) == "morph" || juce::String(suffix) == "tension"
            || juce::String(suffix) == "adaptive" || juce::String(suffix) == "peak-rms" || juce::String(suffix) == "range"
            || juce::String(suffix) == "processor-release-peak" || juce::String(suffix) == "processor-release-rms" || juce::String(suffix) == "processor-attack"))
            return static_cast<float>(ava::modules::dsp::roundToParameterStep(rawValue));
        return rawValue;
    };
    if (moduleKey == "dyn")
    {
        const juce::String suffix(control.suffix);
        if (suffix == "prog-span" || suffix == "corr-prog-span")
            return std::abs(read("progression")) >= 0.005f
                && (read("domain") >= 0.5f || read("morph") > 0.0f);
        const auto pureClip = read("domain") < 0.5f && read("mode") < 0.5f && read("morph") == 0.0f;
        if (suffix == "tension-floor" || suffix == "corr-tension-floor" || suffix == "tension-hyst")
            return std::abs(read("tension")) > 0.0f;
        if (pureClip && suffix == "lookahead")
            return read("adaptive") > 0.0f;
        const auto envelopeActive = !pureClip;
        const auto detectorActive = envelopeActive || std::abs(read("tension")) > 0.0f;
        const auto peakActive = read("peak-rms") < 100.0f;
        const auto rmsActive = read("peak-rms") > 0.0f;
        if (suffix == "thresh-hyst" || suffix == "corr-thresh-hyst") return detectorActive;
        if (suffix == "peak-rms") return detectorActive;
        if (suffix == "peak-crest" || suffix == "corr-peak-crest") return detectorActive && peakActive && rmsActive;
        if (suffix == "rms-time") return detectorActive && rmsActive;
        if (suffix == "processor-release-peak") return detectorActive && peakActive;
        if (suffix == "processor-release-rms") return detectorActive && rmsActive;
        if (suffix == "attack-curve")
            return envelopeActive
                && (read("processor-attack-mode") >= 0.5f || read("processor-attack") > 0.0f);
        if (suffix == "release-curve" || suffix == "auto-rel" || suffix == "rel-memory" || suffix == "rel-depth")
            return ((suffix != "rel-memory" && suffix != "rel-depth") || read("auto-rel") >= 0.005f)
                && (envelopeActive || std::abs(read("tension")) > 0.0f)
                && ((peakActive && (read("processor-release-peak-mode") >= 0.5f || read("processor-release-peak") > 0.0f))
                    || (rmsActive && (read("processor-release-rms-mode") >= 0.5f || read("processor-release-rms") > 0.0f)));
    }
    if (moduleKey == "dyn" && control.kind == ControlKind::time)
    {
        const auto compressor = read("domain") >= 0.5f || read("morph") > 0.0f;
        const auto transust = read("domain") < 0.5f && read("mode") >= 0.5f;
        return transust || compressor || (juce::String(control.suffix) != "processor-attack" && std::abs(read("tension")) > 0.0f);
    }
    if (! control.enabledWhenAnyNonZeroSuffixes.empty())
        return std::any_of(control.enabledWhenAnyNonZeroSuffixes.begin(), control.enabledWhenAnyNonZeroSuffixes.end(),
            [&read] (const char* suffix) { return std::abs(read(suffix)) > 0.0f; });
    if (juce::String(control.enabledWhenSuffix).isEmpty()) return true;
    const auto* dependency = state.getParameter(ava::crossover::parameters::makeRangeParameterId(sourceRange, control.enabledWhenSuffix));
    if (control.enabledWhenBelowMaximum) return dependency == nullptr || read(control.enabledWhenSuffix) < dependency->convertFrom0to1(1.0f);
    if (control.enabledWhenAboveMinimum) return dependency == nullptr || read(control.enabledWhenSuffix) > dependency->convertFrom0to1(0.0f);
    return read(control.enabledWhenSuffix) >= 0.5f;
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
    config.processingInputParameterId = AvaAudioProcessor::paramProcessingInputId;
    config.soloInstanceParameterId = AvaAudioProcessor::paramSoloInstanceId;
    config.soloChannelParameterId = AvaAudioProcessor::paramSoloChannelId;
    config.isProcessingInputAvailable = [&processor] (int choice) { return processor.isProcessingInputAvailable(choice); };
    config.crossoverDecimals = 2;
    config.startOnCrossoverSettings = false;
    config.showModuleHeading = false;
    return config;
}

std::vector<CrossoverModuleComponent::CrossoverControlSpec> makeTlsRangeControls()
{
    return {
        toggleControl("lc", "LC", "", "", "listen", 0, 4),
        toggleControl("rc", "RC", "", "", "listen"),
        toggleControl("mc", "MC", "", "", "listen"),
        toggleControl("sc", "SC", "", "", "listen"),
        toggleControl("ll", "LL", "", "", "listen", 1, 4),
        toggleControl("rr", "RR", "", "", "listen"),
        toggleControl("ms+3", "MS+3"),
        toggleControl("ss", "SS", "", "", "listen"),

        []
        {
            auto spec = parameterToggleControl("stereo.gain", "GAIN-STEREO", 2, "stereo.mute.icon", "MUTE", false, 95, iconControlSize, "volume-off");
            spec.topGapMultiplier = 2;
            return spec;
        }(),
        parameterToggleControl("left.gain", "GAIN-LEFT", 2, "left.mute.icon", "MUTE", false, 95, iconControlSize, "volume-off"),
        parameterToggleControl("right.gain", "GAIN-RIGHT", 2, "right.mute.icon", "MUTE", false, 95, iconControlSize, "volume-off"),
        parameterToggleControl("mid.gain", "GAIN-MID", 2, "mid.mute.icon", "MUTE", false, 95, iconControlSize, "volume-off"),
        parameterToggleControl("side.gain", "GAIN-SIDE", 2, "side.mute.icon", "MUTE", false, 95, iconControlSize, "volume-off"),

        parameterControl("stereo.delay", "DELAY-STEREO", 2, -1, 2),
        parameterControl("left.delay", "DELAY-LEFT", 2),
        parameterControl("right.delay", "DELAY-RIGHT", 2),

        parameterControl("stereo.phase", "PHASE-STEREO", 2, -1, 2),
        parameterControl("left.phase", "PHASE-LEFT", 2),
        parameterControl("right.phase", "PHASE-RIGHT", 2),

        parameterControl("left.pan", "PANORAMA-LEFT", 2, -1, 2),
        parameterControl("right.pan", "PANORAMA-RIGHT", 2),
        parameterControl("law", "PANORAMA-LAW", 2),

        parameterControl("impact", "SHEAR-IMPACT", 2, -1, 2),
        choiceControl("direction", "SHEAR-DIRECTION"),

        parameterControl("mid.balance", "MS-BALANCE-MID", 2, -1, 2),
        parameterControl("side.balance", "MS-BALANCE-SIDE", 2),

        parameterControl("degree", "ORTHO-DEGREE", 2, -1, 2),
        toggleControl("flip-right", "ORTHO-FLIP-RIGHT"),
        readoutControl("degree", "ORTHO-POSITION", "flip-right"),

        toggleControl("hpos", "RECT-HALF-POS", "", "", "rectification", 2, 2),
        toggleControl("hneg", "RECT-HALF-NEG", "", "", "rectification"),
        toggleControl("fpos", "RECT-FULL-POS", "", "", "rectification", 1, 2),
        toggleControl("fneg", "RECT-FULL-NEG", "", "", "rectification"),

        [] {
            auto spec=parameterToggleControl("comb.count", "COMB-COUNT", 0,
                "comb.count.bypass", "BYPASS", false, 0, iconControlSize, "plug-off");
            spec.topGapMultiplier=2;
            return spec;
        }(),
        []
        {
            auto spec=parameterToggleControl("comb.shift", "COMB-SHIFT", 0,
                "comb.shift.bypass", "BYPASS", false, 0, iconControlSize, "plug-off");
            spec.enabledWhenSuffix="comb.count";
            spec.enabledWhenAboveMinimum=true;
            return spec;
        }(),
    };
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
    config.showModuleHeading = false;
    config.rangeControls = makeTlsRangeControls();
    config.getBlockOrder = [&processor](size_t range) { return processor.getBlockOrder(range); };
    config.getGainOrder = [&processor](size_t range) { return processor.getGainOrder(range); };
    config.setGainOrder = [&processor](size_t range, const tls::gain::Order& order) { processor.setGainOrder(range,order); };
    config.setBlockOrder = [&processor](size_t range, const tls::blocks::Order& order) { processor.setBlockOrder(range,order); };
    return config;
}

std::vector<CrossoverModuleComponent::CrossoverControlSpec> makeDynRangeControls(
    const DynModuleProcessor& processor, const size_t rangeIndex)
{
    const auto correlationMode = processor.isCorrelationRange(rangeIndex);
    std::vector<CrossoverControlSpec> controls {
        parameterControl("input", "INPUT", 2, -1, 0),
        choiceControl("domain", "DOMAIN", -1, 2),
        choiceControl("mode", "MODE"),
        choiceControl("fft-size", "WIN-SIZE"),
        choiceControl("fft-overlap", "OVERLAP"),
        parameterControl(correlationMode ? "corr-slope" : "spectral-slope", "SLOPE", 2),
        parameterControl("corr-smoothing", "SMOOTHING", 0),
        parameterControl("corr-impact", "IMPACT", 2),
        parameterControl("morph", "MORPH", 2, 0, 2),
        parameterControl("feedback", "FWD/FB", 2, 0, 1, "morph", false, true),
        parameterControl("clip-knee", "CLIP-KNEE", 2, 0, 1, "morph", true),
        parameterToggleControl("trans-gain", "TRANS-GAIN", 2, "mute-trans", "MUTE", false, 95, iconControlSize, "volume-off"),
        parameterToggleControl("sust-gain", "SUST-GAIN", 2, "mute-sust", "MUTE", false, 95, iconControlSize, "volume-off"),
        parameterControl("retrigger", "RETRIGGER", 2, -1, 1),
        toggleControl("one-shot", "ONE-SHOT"),
        parameterControl("ratio", "RATIO", 2, 0, 2, "morph", false, true),
        parameterControl("progression", "PROGRESSION", 2, 0, 1, "morph", false, true),
        parameterControl(correlationMode ? "corr-prog-span" : "prog-span", "PROG-SPAN", 2, 0),
        parameterControl(correlationMode ? "corr-knee" : "comp-knee", "COMP-KNEE", 2, 0, 1, "morph", false, true),
        parameterControl("tension", "TENSION", 2, -1, 2),
        parameterControl(correlationMode ? "corr-tension-floor" : "tension-floor", "TENSION-FLOOR", 2, 0),
        parameterControl("tension-hyst", "TENSION-HYST", 2, 0),
        parameterControl("peak-rms", "PEAK/RMS", 2, -1, 2),
        parameterControl(correlationMode ? "corr-peak-crest" : "peak-crest", "PEAK-CREST", 2, -1, 1, "peak-rms", true),
        parameterControl("rms-time", "RMS-TIME", 0, -1, 1, "peak-rms", false, true),
        parameterControl(correlationMode ? "corr-threshold" : "threshold", "THRESHOLD", 2, -1, 2, "adaptive", true),
        parameterControl(correlationMode ? "corr-thresh-hyst" : "thresh-hyst", "THRESH-HYST", 2),
        parameterControl("range", "RANGE", 2),
        parameterControl("range-curve", "RANGE-CURVE", 2, 0, 1, "range", false, true),
        parameterControl("adaptive", "THRESHOLD-ADAP", 2),
        parameterControl("adap-time", "ADAP-TIME", 2, 0, 1, "adaptive", false, true),
        parameterControl(correlationMode ? "corr-offset" : "offset", "ADAP-OFFSET", 2, -1, 1, "adaptive", false, true),
        parameterControl("attack", "ADAP-ATTACK", 2, -1, 1, "adaptive", false, true),
        parameterControl("hold", "ADAP-HOLD", 2, -1, 1, "adaptive", false, true),
        parameterControl("release", "ADAP-RELEASE", 2, -1, 1, "adaptive", false, true),
        parameterControl("lookahead", "LOOKAHEAD", 2, 0, 2),
        parameterControl("processor-delay", "DELAY", 2, -1, 1, "morph", false, true),
        timeControl("processor-attack", "ATTACK", "processor-attack-mode", "processor-attack-note"),
        parameterControl("attack-curve", "ATTACK-CURVE", 2, 0, 1, "morph", false, true),
        timeControl("processor-hold", "HOLD", "processor-hold-mode", "processor-hold-note", 2),
        timeControl("processor-release-peak", "RELEASE-PEAK", "processor-release-peak-mode", "processor-release-peak-note"),
        timeControl("processor-release-rms", "RELEASE-RMS", "processor-release-rms-mode", "processor-release-rms-note"),
        parameterControl("release-curve", "RELEASE-CURVE", 2, 0),
        parameterControl("release-type", "RELEASE-TYPE", 2, 0),
        parameterControl("auto-rel", "AUTO-REL", 2, 0),
        parameterControl("rel-memory", "REL-MEMORY", 2, 0),
        parameterControl("rel-depth", "REL-DEPTH", 2, 0),
        parameterControl("make-up", "MAKE-UP", 2, -1, 2),
        parameterControl("mix", "MIX", 2),
        parameterControl("output", "OUTPUT", 2),
        choiceControl("sc-channel", "SC-CHANNEL", -1, 2),
        toggleControl("delta", "DELTA", "", "", "", 2),
    };
    const auto spectralDomain = processor.isSpectralRange(rangeIndex);
    const auto transustMode = processor.isTransustRange(rangeIndex);
    if (! spectralDomain)
        std::erase_if(controls, [] (const auto& control)
        {
            const juce::String suffix(control.suffix);
            return suffix == "fft-size" || suffix == "fft-overlap" || suffix == "spectral-slope";
        });
    if (! transustMode)
        std::erase_if(controls, [] (const auto& control)
        {
            const juce::String suffix(control.suffix);
            return suffix == "trans-gain" || suffix == "sust-gain"
                || suffix == "retrigger" || suffix == "one-shot";
        });
    if (spectralDomain || transustMode)
        std::erase_if(controls, [] (const auto& control)
        {
            const juce::String suffix(control.suffix);
            return suffix == "morph" || suffix == "clip-knee" || suffix == "feedback";
        });
    if (! correlationMode)
        std::erase_if(controls, [] (const auto& control)
        {
            const juce::String suffix(control.suffix);
            return suffix == "corr-smoothing" || suffix == "corr-impact";
        });
    for (auto& control : controls)
    {
        const juce::String suffix(control.suffix);
        const auto spectralOnly = suffix == "fft-size" || suffix == "fft-overlap" || suffix == "spectral-slope";
        if (spectralOnly || suffix == "corr-slope") control.exclusiveScope = ExclusiveScope::domain;
        const auto timeOnly = suffix == "morph" || suffix == "clip-knee" || suffix == "feedback";
        control.interactionDisabled = spectralOnly ? ! spectralDomain : timeOnly && spectralDomain;
        const auto transustOnly = suffix == "trans-gain" || suffix == "sust-gain"
            || suffix == "retrigger" || suffix == "one-shot";
        const auto compClipOnly = suffix == "morph" || suffix == "clip-knee" || suffix == "ratio"
            || suffix == "progression" || suffix == "prog-span" || suffix == "corr-prog-span"
            || suffix == "feedback" || suffix == "range" || suffix == "range-curve"
            || suffix == "tension" || suffix == "tension-floor" || suffix == "tension-hyst" || suffix == "release-type";
        if (suffix == "trans-gain") control.topGapMultiplier = 2;
        if (transustOnly)
        {
            control.interactionDisabled = ! transustMode;
            control.exclusiveScope = ExclusiveScope::mode;
        }
        if (! spectralDomain && ! transustMode
            && (suffix == "morph" || suffix == "clip-knee" || suffix == "feedback"))
            control.exclusiveScope = ExclusiveScope::mode;
        if (transustMode && compClipOnly) control.interactionDisabled = true;
        const auto correlationOnly = suffix.startsWith("corr-");
        if (correlationOnly) control.interactionDisabled = ! correlationMode;
        if (correlationMode && (suffix == "corr-smoothing" || suffix == "corr-impact"))
            control.exclusiveScope = ExclusiveScope::mode;
        if ((spectralDomain || transustMode) && juce::String(control.enabledWhenSuffix) == "morph")
            control.enabledWhenSuffix = "";
    }
    for (auto& control : controls)
    {
        const auto suffix = juce::String(control.suffix);
        if (suffix == "prog-span" || suffix == "corr-prog-span")
            control.enabledWhenAnyNonZeroSuffixes = { "progression", "morph" };
        if (suffix == "auto-rel" || suffix == "rel-memory" || suffix == "rel-depth")
            control.enabledWhenAnyNonZeroSuffixes = { "auto-rel", "morph", "tension", "peak-rms", "processor-release-peak", "processor-release-rms" };
        if (suffix == "sc-channel")
        {
            control.auxiliaryToggleSuffix = "sc-listen";
            control.auxiliaryToggleLabel = "LISTEN";
            control.auxiliaryToggleIcon = "headphones";
            control.auxiliaryToggleWidth = 30;
        }
        if (! spectralDomain && ! transustMode && (suffix == "processor-hold" || suffix == "processor-release-peak" || suffix == "processor-release-rms" || suffix == "release-curve" || suffix == "release-type"))
            control.enabledWhenAnyNonZeroSuffixes = { "morph", "tension" };
    }
    const auto modeControl = std::find_if(controls.begin(), controls.end(),
        [] (const auto& control) { return juce::String(control.suffix) == "mode"; });
    if (modeControl != controls.end() && std::next(modeControl) != controls.end())
        std::next(modeControl)->topGapMultiplier = 2;
    return controls;
}

CrossoverModuleComponent::Config makeDynCrossoverConfig(DynModuleProcessor& processor,
                                                        AvaAudioProcessorEditor& editor,
                                                        std::unique_ptr<juce::Component>& editorHolder,
                                                        const size_t rangeIndex)
{
    CrossoverModuleComponent::Config config;
    config.processorIdentity = processor.getDomainEditorIdentity(rangeIndex);
    config.moduleKey = "dyn";
    config.valueTreeState = &processor.getValueTreeState();
    config.undoManager = &processor.getUndoManager();
    config.markParametersDirty = [&processor] { processor.markParametersDirty(); };
    config.makeRangeParameterId = ava::crossover::parameters::makeRangeParameterId;
    config.showCrossoverControls = false;
    config.showCrossoverNavigation = false;
    config.showCrossoverSolo = false;
    config.showModuleHeading = false;
    configureChoicePrompts(config, editor, editorHolder);
    config.getHostSyncChoices = dyn::parameters::getTimeNoteChoices;
    config.getDefaultHostSyncChoiceIndex = [] { return 7; };
    config.isRangeChoiceAvailable = [&processor] (const juce::String& suffix, const int choice)
    {
        return suffix != "sc-channel" || processor.isSidechainChannelAvailable(choice);
    };
    config.rangeControls = makeDynRangeControls(processor, rangeIndex);
    return config;
}

}
