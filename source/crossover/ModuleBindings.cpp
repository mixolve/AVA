#include "../modules/shared/TimeUnits.h"
#include "../modules/sibil/Synchronization.h"
#include "ModuleBindings.h"
#include "../shell/EditorSetup.h"
#include "ParameterIds.h"
#include "../shell/Processor.h"
#include "../shell/ClipboardTransfer.h"
#include "../shell/Editor.h"
#include "../shell/Style.h"
#include "../modules/shared/BlockChainProcessor.h"
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

CrossoverControlSpec parameterControl(const char* suffix,
                                 const char* label,
                                 const int decimals,
                                 const int sourceRangeIndex = -1,
                                 const bool hasTopGap = true)
{
    CrossoverControlSpec spec;
    spec.kind = ControlKind::parameter;
    spec.suffix = suffix;
    spec.label = label;
    spec.decimals = decimals;
    spec.sourceRangeIndex = sourceRangeIndex;
    spec.hasTopGap = hasTopGap;
    return spec;
}

CrossoverControlSpec choiceControl(const char* suffix,
                              const char* label,
                              const int sourceRangeIndex = -1,
                              const bool hasTopGap = true)
{
    CrossoverControlSpec spec;
    spec.kind = ControlKind::choice;
    spec.suffix = suffix;
    spec.label = label;
    spec.sourceRangeIndex = sourceRangeIndex;
    spec.hasTopGap = hasTopGap;
    return spec;
}

CrossoverControlSpec toggleControl(const char* suffix,
                              const char* label,
                              const bool hasTopGap = true,
                              const int controlsInRow = 1)
{
    CrossoverControlSpec spec;
    spec.kind = ControlKind::toggle;
    spec.suffix = suffix;
    spec.label = label;
    spec.hasTopGap = hasTopGap;
    spec.controlsInRow = controlsInRow;
    return spec;
}

CrossoverControlSpec readoutControl(const char* suffix,
                               const char* label,
                               const char* modeSuffix,
                               const bool hasTopGap = true)
{
    CrossoverControlSpec spec;
    spec.kind = ControlKind::readout;
    spec.suffix = suffix;
    spec.label = label;
    spec.modeSuffix = modeSuffix;
    spec.hasTopGap = hasTopGap;
    return spec;
}

CrossoverControlSpec timeControl(const char* suffix,
                            const char* label,
                            const bool hasTopGap = true)
{
    CrossoverControlSpec spec;
    spec.kind = ControlKind::time;
    spec.suffix = suffix;
    spec.label = label;
    spec.decimals = 2;
    spec.hasTopGap = hasTopGap;
    return spec;
}

// Preserve a group boundary even when the preceding row has free cells.
CrossoverControlSpec startControlRow(CrossoverControlSpec spec)
{
    spec.startNewControlRow = true;
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
    config.copyInstanceAll = [&processor] { return ava::clipboard::copyInstanceAll(processor); };
    config.pasteInstanceAll = [&processor] { return ava::clipboard::pasteInstanceAll(processor); };
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
    config.outputMuteParameterId = AvaAudioProcessor::paramOutputMuteId;
    config.soloChannelParameterId = AvaAudioProcessor::paramSoloChannelId;
    config.isProcessingInputAvailable = [&processor] (int choice) { return processor.isProcessingInputAvailable(choice); };
    config.crossoverDecimals = 2;
    return config;
}

std::vector<CrossoverModuleComponent::CrossoverControlSpec> makeBlockRangeControls()
{
    return {
        toggleControl("lc", "LEFT\nCENTER", false, 4),
        toggleControl("rc", "RIGHT\nCENTER"),
        toggleControl("ll", "LEFT\nLEFT"),
        toggleControl("rr", "RIGHT\nRIGHT"),
        toggleControl("mc", "MID\nCENTER", true, 4),
        toggleControl("sc", "SIDE\nCENTER"),
        toggleControl("ms+3", "MS+3"),
        toggleControl("ss", "SIDE\nSIDE"),

        []
        {
            auto spec = parameterControl("stereo.gain", "STEREO", 2);
            spec.hasTopGap = true;
            return spec;
        }(),
        parameterControl("left.gain", "LEFT", 2),
        parameterControl("right.gain", "RIGHT", 2),
        parameterControl("mid.gain", "MID", 2),
        parameterControl("side.gain", "SIDE", 2),

        parameterControl("stereo.delay", "STEREO", 2, -1, true),
        parameterControl("left.delay", "LEFT", 2),
        parameterControl("right.delay", "RIGHT", 2),
        toggleControl("delay.link.opp", "LINK\nOPPOSITE", false),

        parameterControl("stereo.phase", "STEREO", 2, -1, true),
        parameterControl("left.phase", "LEFT", 2),
        parameterControl("right.phase", "RIGHT", 2),

        parameterControl("left.pan", "LEFT", 2, -1, true),
        parameterControl("right.pan", "RIGHT", 2),
        parameterControl("law", "LAW", 2),

        parameterControl("impact", "IMPACT", 2, -1, true),
        choiceControl("direction", "DIRECT"),

        parameterControl("mid.balance", "MID", 2, -1, true),
        parameterControl("side.balance", "SIDE", 2),

        [] {
            auto spec = parameterControl("degree", "DEGREE", 2);
            spec.hasTopGap = true;
            return spec;
        }(),
        toggleControl("flip-right", "FLIP\nRIGHT", false),
        readoutControl("degree", "POSITION", "flip-right", false),

        toggleControl("hpos", "HALF\nPOSITIVE", true, 2),
        toggleControl("hneg", "HALF\nNEGATIVE"),
        toggleControl("fpos", "FULL\nPOSITIVE", true, 2),
        toggleControl("fneg", "FULL\nNEGATIVE"),

        [] {
            auto spec=parameterControl("comb.count", "COUNT", 0);
            spec.hasTopGap = true;
            return spec;
        }(),
        []
        {
            auto spec=parameterControl("comb.shift", "SHIFT", 0);
            return spec;
        }(),
        parameterControl("comb.mix", "MIX", 2),
        parameterControl("shift.pitch", "PITCH", 2, -1, true),
        parameterControl("shift.freq", "FREQ", 2),
        choiceControl("shift.order", "ORDER"),
        parameterControl("shift.q", "Q", 2),
        [] {
            auto spec = parameterControl("shift.gain-orig", "GAIN\nORIG", 2);
            spec.startNewControlRow = true;
            return spec;
        }(),
        parameterControl("shift.gain-pitched", "GAIN\nPITCHED", 2),
        toggleControl("shift.solo-orig", "SOLO\nORIG"),
        toggleControl("shift.solo-pitched", "SOLO\nPITCHED"),
        [] {
            auto spec = choiceControl("shift.window", "WINDOW");
            spec.startNewControlRow = true;
            return spec;
        }(),
        choiceControl("shift.pitch-mode", "MODE"),
        choiceControl("shift.detector", "DETECT"),
        choiceControl("shift.transients", "TRANS"),
        toggleControl("shift.independent-phase", "INDEP\nPHASE"),
        parameterControl("varispeed.rate", "RATE", 3),
        toggleControl("varispeed.preserve", "PRESERVE\nPITCH"),
        choiceControl("varispeed.quality", "ST-QUALITY"),
        parameterControl("varispeed.threshold", "THRESH", 2),
        parameterControl("varispeed.retrigger", "RETRIG", 2),
        parameterControl("varispeed.crossfade", "FADE", 2),
        toggleControl("sibil.solo-rest", "SOLO\nTONAL"),
        parameterControl("sibil.gain-rest", "GAIN\nTONAL", 2),
        toggleControl("sibil.solo-s", "SOLO\nS"),
        parameterControl("sibil.gain-s", "GAIN\nS", 2),
        []{CrossoverControlSpec spec;spec.kind=ControlKind::custom;spec.suffix="sibil.sync";spec.label="SYNC";return spec;}(),
        startControlRow(parameterControl("sibil.level", "LEVEL\nDETECT", 2)),
        parameterControl("sibil.brightness", "BRIGHT\nDETECT", 2),
        parameterControl("sibil.noise", "NOISINS\nDETECT", 2),
        parameterControl("sibil.fade", "CROSS\nFADE", 2),
        parameterControl("sibil.edge-detect", "EDGE\nDETECT", 2),
    };
}

CrossoverModuleComponent::Config makeBlockChainCrossoverConfig(BlockChainProcessor& processor)
{
    CrossoverModuleComponent::Config config;
    config.processorIdentity = &processor;
    config.makeCustomControl=[&processor](size_t band,const juce::String& suffix)->std::unique_ptr<juce::Component>{
        return suffix=="sibil.sync"?ava::modules::sibil::makeLinkControl(processor,band):nullptr;
    };
    config.moduleKey = "chain";
    config.getControlPolicy = [&processor] (size_t range, const juce::String& suffix) { return ava::modules::chain::parameters::getControlPolicy(processor.getValueTreeState(), range, suffix); };
    config.valueTreeState = &processor.getValueTreeState();
    config.markParametersDirty = [&processor] { processor.markParametersDirty(); };
    config.makeRangeParameterId = ava::crossover::parameters::makeRangeParameterId;
    config.crossoverDecimals = 2;
    config.showCrossoverControls = false;
    config.showCrossoverNavigation = false;
    config.showCrossoverSolo = false;
    config.rangeControls = makeBlockRangeControls();
    config.getGainOrder = [&processor](size_t range) { return processor.getGainOrder(range); };
    return config;
}

std::vector<CrossoverModuleComponent::CrossoverControlSpec> makeDynRangeControls(
    const DynModuleProcessor& processor, const size_t rangeIndex)
{
    const auto correlationMode = processor.isCorrelationRange(rangeIndex);
    std::vector<CrossoverControlSpec> controls {
        parameterControl("input", "INPUT", 2, -1, false),
        choiceControl("domain", "DOMAIN", -1, true),
        choiceControl("mode", "MODE"),
        parameterControl("ratio", "RATIO", 2, 0, true),
        parameterControl(correlationMode ? "corr-knee" : "comp-knee", "COMP-KNEE", 2, 0, true),
        choiceControl("sc-channel", "SC-CHANNEL", -1, false),

        startControlRow(parameterControl("morph", "MORPH", 2, 0, true)),
        parameterControl("feedback", "FWD/FB", 2, 0, true),
        parameterControl("clip-knee", "CLIP-KNEE", 2, 0, true),
        startControlRow(choiceControl("fft-size", "WIN-SIZE")),
        choiceControl("fft-overlap", "OVERLAP"),
        parameterControl(correlationMode ? "corr-slope" : "spectral-slope", "SLOPE", 2),
        startControlRow(parameterControl("trans-gain", "TRANS-GAIN", 2)),
        parameterControl("sust-gain", "SUST-GAIN", 2),
        parameterControl("retrigger", "RETRIG", 2),
        parameterControl("corr-smoothing", "SMOOTH", 0),
        parameterControl("corr-impact", "IMPACT", 2),

        startControlRow(parameterControl("peak-rms", "PEAK/RMS", 2, -1, true)),
        parameterControl(correlationMode ? "corr-peak-crest" : "peak-crest", "PEAK-CREST", 2, -1, true),
        parameterControl("rms-time", "RMS-TIME", 2, -1, true),
        parameterControl("tension", "TENSION", 2, -1, true),
        parameterControl(correlationMode ? "corr-tension-floor" : "tension-floor", "TENSION-FLOOR", 2, 0),
        parameterControl("tension-hyst", "TENSION-HYST", 2, 0),

        startControlRow(parameterControl(correlationMode ? "corr-threshold" : "threshold", "THRESH", 2, -1, true)),
        parameterControl(correlationMode ? "corr-thresh-hyst" : "thresh-hyst", "THRESH-HYST", 2),
        parameterControl("range", "RANGE", 2),
        parameterControl("adaptive", "ADAP THRESH", 2),
        parameterControl("adap-time", "ADAP-TIME", 2, 0, true),
        parameterControl(correlationMode ? "corr-offset" : "offset", "ADAP-OFFSET", 2, -1, true),

        startControlRow(parameterControl("lookahead", "LOOK AHEAD", 2, 0, true)),
        parameterControl("lookahead-curve", "LOOK-CURVE", 2, 0, true),
        timeControl("processor-attack", "ATTACK"),
        parameterControl("attack-curve", "ATTACK-CURVE", 2, 0, true),
        startControlRow(timeControl("processor-hold", "HOLD", true)),
        timeControl("processor-release-peak", "RELEASE-PEAK"),
        timeControl("processor-release-rms", "RELEASE-RMS"),
        parameterControl("release-curve", "RELEASE-CURVE", 2, 0),
        startControlRow(parameterControl("auto-rel", "AUTO RELEASE", 2, 1)),
        parameterControl("rel-memory", "REL-MEMORY", 2, 0),
        parameterControl("rel-depth", "REL-DEPTH", 2, 0),
        parameterControl("make-up", "MAKE-UP", 2, -1, true),
        parameterControl("mix", "MIX", 2),
        parameterControl("output", "OUTPUT", 2),
    };
    std::erase_if(controls, [&] (const auto& control)
    {
        return !dyn::parameters::getControlPolicy(processor.getValueTreeState(), rangeIndex, control.suffix).visible;
    });
    const auto modeControl = std::find_if(controls.begin(), controls.end(),
        [] (const auto& control) { return juce::String(control.suffix) == "mode"; });
    if (modeControl != controls.end() && std::next(modeControl) != controls.end())
        std::next(modeControl)->hasTopGap = true;
    return controls;
}

CrossoverModuleComponent::Config makeDynCrossoverConfig(DynModuleProcessor& processor,
                                                        AvaAudioProcessorEditor& editor,
                                                        const size_t rangeIndex)
{
    CrossoverModuleComponent::Config config;
    config.processorIdentity = processor.getDomainEditorIdentity(rangeIndex);
    config.moduleKey = "dyn";
    config.getControlPolicy = [&processor] (size_t range, const juce::String& suffix) { return dyn::parameters::getControlPolicy(processor.getValueTreeState(), range, suffix); };
    config.valueTreeState = &processor.getValueTreeState();
    config.markParametersDirty = [&processor] { processor.markParametersDirty(); };
    config.makeRangeParameterId = ava::crossover::parameters::makeRangeParameterId;
    config.showCrossoverControls = false;
    config.showCrossoverNavigation = false;
    config.showCrossoverSolo = false;
    config.clearKeyboardFocus = [&editor] { clearKeyboardFocus(editor); };
    config.getNoteChoices = ava::modules::parameters::getTimeNoteChoices;
    config.getTempoBpm = [&processor] { return processor.getTempoBpm(); };
    config.isRangeChoiceAvailable = [&processor] (const juce::String& suffix, const int choice)
    {
        return suffix != "sc-channel" || processor.isSidechainChannelAvailable(choice);
    };
    config.rangeControls = makeDynRangeControls(processor, rangeIndex);
    return config;
}

}
