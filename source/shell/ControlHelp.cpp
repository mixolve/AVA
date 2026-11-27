#include "ControlHelp.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"
#include "TextControl.h"
#include "Style.h"
#include "UppercaseText.h"

namespace
{
struct Hint { const char* key; const char* text; };

constexpr Hint parameters[] {
    {"lc", "LEFT CENTER. Plays the left channel through both speakers."},
    {"rc", "RIGHT CENTER. Plays the right channel through both speakers."},
    {"mc", "MID CENTER. Plays (Left + Right) / 2 through both speakers."},
    {"sc", "SIDE CENTER. Plays (Left - Right) / 2 through both speakers, in the same polarity."},
    {"ll", "LEFT LEFT. Keeps the left channel in place and silences the right."},
    {"rr", "RIGHT RIGHT. Keeps the right channel in place and silences the left."},
    {"ss", "SIDE SIDE. Plays the side component in stereo with opposite polarity on the right."},
    {"ms+3", "MS+3. Adds 3 dB to the monitored signal in the Mid and Side modes."},
    {"stereo.gain", "STEREO GAIN. Applies the same gain in dB to the left and right channels."},
    {"left.gain", "LEFT GAIN. Adjusts the left channel level in dB."},
    {"right.gain", "RIGHT GAIN. Adjusts the right channel level in dB."},
    {"mid.gain", "MID GAIN. Adjusts the common component of the stereo signal in dB."},
    {"side.gain", "SIDE GAIN. Adjusts the difference component in dB. Lower values narrow the stereo image."},
    {"stereo.delay", "STEREO DELAY. Offsets both channels together in milliseconds. Use the individual LEFT and RIGHT controls to change the timing between channels."},
    {"left.delay", "LEFT DELAY. Offsets the left channel in milliseconds. Compare against RIGHT to adjust relative timing."},
    {"right.delay", "RIGHT DELAY. Offsets the right channel in milliseconds. Compare against LEFT to adjust relative timing."},
    {"delay.link.opp", "LINK OPPOSITE. Moving one channel delay moves the other by the opposite amount."},
    {"stereo.phase", "STEREO PHASE. Rotates the phase of both channels by the same angle."},
    {"left.phase", "LEFT PHASE. Rotates the phase of the left channel in degrees."},
    {"right.phase", "RIGHT PHASE. Rotates the phase of the right channel in degrees."},
    {"left.pan", "LEFT PAN. Moves the left input across the stereo output."},
    {"right.pan", "RIGHT PAN. Moves the right input across the stereo output."},
    {"law", "PAN LAW. Adjusts the gain compensation as a channel is panned towards the centre."},
    {"impact", "SHEAR IMPACT. Sets the amount of crossfeed between channels. DIRECT selects its direction."},
    {"direction", "SHEAR DIRECT. Selects the direction of the channel crossfeed."},
    {"mid.balance", "MID BALANCE. Moves the mid component across the stereo image."},
    {"side.balance", "SIDE BALANCE. Adjusts the balance of the side component."},
    {"degree", "ORTHO DEGREE. Rotates the two-channel signal by the selected angle."},
    {"flip-right", "ORTHO FLIP RIGHT. Flips the right channel in the orthogonal stereo transform."},
    {"hpos", "HALF POSITIVE. Keeps the positive half-wave and suppresses the negative half-wave."},
    {"hneg", "HALF NEGATIVE. Keeps the negative half-wave and suppresses the positive half-wave."},
    {"fpos", "FULL POSITIVE. Folds negative samples into the positive half-wave."},
    {"fneg", "FULL NEGATIVE. Folds positive samples into the negative half-wave."},
    {"comb.count", "COMB COUNT. Sets the spacing of the comb teeth via its delay length. At zero the comb is inactive."},
    {"comb.shift", "COMB SHIFT. Offsets the comb teeth by up to half their spacing in either direction. COUNT changes the spacing itself."},
    {"comb.mix", "COMB MIX. Blends the comb-processed signal with the original."},
    {"shift.pitch", "SHIFT PITCH. Transposes the processed signal in semitones. Use GAIN PITCHED and SOLO PITCHED to hear it separately."},
    {"shift.freq", "SHIFT FREQ. Sets the split frequency in Hz. The low band stays unpitched and the high band uses PITCH."},
    {"shift.order", "SHIFT ORDER. Chooses the order of the crossover filters that split the original and pitch-processed bands."},
    {"shift.q", "SHIFT Q. Adjusts the crossover filter response around FREQ."},
    {"shift.gain-orig", "GAIN ORIG. Sets the level of the original branch before summing with the shifted branch."},
    {"shift.gain-pitched", "GAIN PITCHED. Sets the level of the shifted branch before summing with the original."},
    {"shift.solo-orig", "SOLO ORIG. Monitors the original branch of SHIFT."},
    {"shift.solo-pitched", "SOLO PITCHED. Monitors the processed branch of SHIFT."},
    {"shift.window", "SHIFT WINDOW. Chooses the processing window. Shorter windows track fast changes more closely; longer windows give finer frequency resolution."},
    {"shift.pitch-mode", "SHIFT MODE. Selects the pitch algorithm for the transposed branch."},
    {"shift.detector", "SHIFT DETECT. Selects how the pitch shifter detects the input material."},
    {"shift.transients", "SHIFT TRANS. Chooses how the pitch shifter handles transients."},
    {"shift.independent-phase", "INDEP PHASE. Switches the pitch algorithm from laminar to independent phase handling."},
    {"varispeed.rate", "VSPEED RATE. Sets the playback speed multiplier. At 1 the original speed is unchanged. R/P changes the display to the equivalent pitch shift in semitones."},
    {"varispeed.preserve", "PRESERVE PITCH. Uses time stretching to change speed while keeping the original pitch."},
    {"varispeed.units", "R/P. Switches between showing the varispeed amount as a rate multiplier or a pitch interval in semitones. The same underlying rate is kept."},
    {"varispeed.quality", "ST QUALITY. Selects the quality of the pitch-preserving time stretch."},
    {"varispeed.threshold", "VSPEED THRESH. Sets the level used to detect an event for retriggering the varispeed engine."},
    {"varispeed.retrigger", "VSPEED RETRIG. Sets the minimum time between detected onsets that can retrigger the engine."},
    {"varispeed.crossfade", "VSPEED FADE. Sets the crossfade length at varispeed transitions in milliseconds."},
    {"sibil.solo-rest", "SOLO TONAL. Monitors the remainder of the signal after the detected sibilant portion is separated."},
    {"sibil.gain-rest", "GAIN TONAL. Adjusts the level of the remainder after separation, before the two portions are recombined."},
    {"sibil.solo-s", "SOLO S. Monitors the isolated sibilant portion. Use it to hear what the detector is picking up. Listen for vowel fragments that should stay in TONAL."},
    {"sibil.gain-s", "GAIN S. Adjusts the detected sibilant portion before recombination. Use negative gain for de-essing."},
    {"sibil.level", "LEVEL DETECT. Sets the minimum signal level considered by the sibilance detector. Lower it to include quieter events."},
    {"sibil.brightness", "SENS. Adjusts how readily the detector classifies the signal as sibilance. Raise it for missed consonants; lower it if tonal material leaks into S."},
    {"sibil.lookahead", "SBL LOOK AHEAD. Delays the audio so the detector can open before a sibilant onset. Range 0 to 30 ms. Higher values add latency. If the initial consonant leaks into TONAL, try increasing this first."},
    {"sibil.hold", "SBL HOLD. Keeps detection open briefly after it would close. Increase it if long consonants such as SH break up."},
    {"sibil.fade", "CROSS FADE. Sets the transition time between TONAL and S. Longer fades smooth gain changes but spread the separation across the boundary."},
    {"input", "DYN INPUT. Adjusts input gain before dynamics processing. Moving it changes how hard the signal drives the detector or clipper."},
    {"domain", "DYN DOMAIN. TIME processes the waveform. SPECTRAL operates on FFT frames and offers FREQ and CORR modes."},
    {"mode", "DYN MODE. In TIME, choose COMP CLIP or TRANS SUST. In SPECTRAL, choose frequency-based or correlation-based dynamics."},
    {"ratio", "RATIO. Sets the steepness of gain reduction above the compressor threshold. A larger ratio means less increase at the output for the same increase at the input."},
    {"comp-knee", "COMP KNEE. Widens the transition around the compressor threshold. At zero the curve changes abruptly."},
    {"corr-knee", "CORR KNEE. Widens the transition around the correlation threshold."},
    {"sc-channel", "SC CHANNEL. Chooses the signal that drives the dynamics detector. Use LSN to monitor the selected sidechain."},
    {"delta", "DT. Auditions the difference between the aligned input and processed DYN signal. Use it to hear what the dynamics processing removes."},
    {"sc-listen", "LSN. Auditions the selected sidechain at the output while this switch is on."},
    {"morph", "MORPH. Blends between the clipper and compressor in TIME mode. Both endpoints can have different envelope behaviour."},
    {"feedback", "FWD/FB. Blends detector sensing from the input towards feedback sensing from the processed signal."},
    {"clip-qual", "EQL. Anchors the soft clip curve at the clip threshold and changes how CLIP KNEE is scaled. Use it to round the transition while keeping the same ceiling."},
    {"mute-trans", "MT. Mutes the transient branch in TRANS SUST."},
    {"mute-sust", "MT. Mutes the sustain branch in TRANS SUST."},
    {"clip-knee", "CLIP KNEE. Rounds the transition into clipping. At zero the clipper has a hard knee."},
    {"fft-size", "WIN SIZE. Sets FFT frame length. Larger frames resolve finer frequency detail and react more slowly in time."},
    {"fft-overlap", "OVERLAP. Sets the overlap between successive FFT frames. More overlap gives more frequent updates at higher CPU cost."},
    {"spectral-slope", "SLOPE. Tilts the frequency-dependent dynamics threshold across the spectrum."},
    {"corr-slope", "CORR SLOPE. Tilts the correlation-mode threshold across the spectrum."},
    {"trans-gain", "TRANS GAIN. Changes the level of the portion classified as transient in TRANS SUST mode."},
    {"sust-gain", "SUST GAIN. Changes the level of the sustained portion in TRANS SUST mode. Muting it leaves detected transients."},
    {"retrigger", "RETRIG. Adds a wait after ATTACK and HOLD before TRANS SUST can trigger again. Zero removes the extra wait."},
    {"one-shot", "ONE. Uses one-shot transient triggering. Requires a fresh upward threshold crossing. ATTACK, HOLD and RETRIG still govern when another event is accepted."},
    {"corr-smoothing", "SMOOTH. Smooths correlation detection across updates to reduce sudden changes."},
    {"corr-impact", "CORR IMPACT. Selects how strongly correlation processing affects left or right. At the centre both channels are affected."},
    {"peak-rms", "PEAK/RMS. Blends instantaneous peak sensing with averaged energy. PEAK follows brief spikes; RMS follows sustained level."},
    {"peak-crest", "PEAK CREST. Offsets the peak threshold relative to the RMS threshold. Positive values make the peak branch less sensitive. This applies when both detector branches contribute."},
    {"corr-peak-crest", "CORR PEAK CREST. Offsets the peak correlation threshold relative to the RMS branch. Positive values make the peak branch less sensitive when both branches contribute."},
    {"rms-time", "RMS TIME. Sets the detector averaging time in milliseconds. Only the RMS part of PEAK/RMS uses it. A longer window responds less to brief spikes and more to sustained changes."},
    {"tension", "TENSION. Adjusts the dynamics transfer driven by the detector. TENSION FLOOR and TENSION HYST apply when tension is in use."},
    {"tension-floor", "TENSION FLOOR. Sets the lower level reference for the TENSION response."},
    {"corr-tension-floor", "CORR TENSION FLOOR. Sets the lower reference for the correlation TENSION response."},
    {"tension-hyst", "TENSION HYST. Separates the opening and closing points of the tension response to stabilise it near the floor."},
    {"threshold", "THRESH. Sets the fixed dynamics threshold. ADAP THRESH blends it with the adaptive reference."},
    {"corr-threshold", "CORR THRESH. Sets the threshold used by correlation dynamics."},
    {"thresh-hyst", "THRESH HYST. Separates the detector turn-on and turn-off thresholds. Raising it helps avoid repeated switching near THRESH."},
    {"corr-thresh-hyst", "CORR THRESH HYST. Separates the onset and recovery thresholds for the correlation detector."},
    {"range", "RANGE. Limits how much attenuation the compressor can apply. Lower the limit to keep more of the signal above threshold."},
    {"adaptive", "ADAP THRESH. Blends a fixed threshold with one derived from a running signal-level estimate. Use ADAP TIME and ADAP OFFSET to tune the adaptive reference."},
    {"adap-time", "ADAP TIME. Controls how quickly the adaptive threshold follows changes in the signal level."},
    {"offset", "ADAP OFFSET. Shifts the adaptive threshold relative to the measured level."},
    {"corr-offset", "CORR ADAP OFFSET. Shifts the adaptive threshold in correlation mode."},
    {"lookahead", "DYN LOOK AHEAD. Delays the audio relative to detection to anticipate peaks. Range 0 to 30 ms. Adds latency and uses LOOK CURVE to shape the approach to gain reduction."},
    {"lookahead-curve", "LOOK CURVE. Shapes gain changes across the lookahead interval. It operates only when LOOK AHEAD is above zero."},
    {"processor-attack", "ATTACK. Sets how fast gain moves towards reduction after a transient or threshold crossing. Use MD to switch between milliseconds and a note value."},
    {"attack-curve", "ATTACK CURVE. Shapes the attack transition. Zero gives the neutral curve; this requires a nonzero attack or a note-synced attack."},
    {"processor-hold", "HOLD. In COMP CLIP, holds gain reduction before release. In TRANS SUST, holds the transient envelope. MD switches between milliseconds and a musical note."},
    {"processor-release-peak", "RELEASE PEAK. Sets how quickly the peak-driven part recovers after gain reduction. Used when PEAK/RMS includes peak detection."},
    {"processor-release-rms", "RELEASE RMS. Sets how quickly the RMS-driven part recovers after gain reduction. Used when PEAK/RMS includes RMS detection."},
    {"release-curve", "RELEASE CURVE. Shapes the recovery towards unity gain independently of the base release setting."},
    {"auto-rel", "AUTO RELEASE. Blends in signal-dependent recovery. REL MEMORY and REL DEPTH tune how strongly and for how long prior reduction matters."},
    {"rel-memory", "REL MEMORY. Sets the time span of the gain-reduction history used by AUTO RELEASE."},
    {"rel-depth", "REL DEPTH. Sets how much reduction history can change the auto-release response."},
    {"make-up", "MAKE UP. Adds gain after dynamics processing to compensate for gain reduction."},
    {"mix", "DYN MIX. Blends the unprocessed signal with the dynamics output for parallel processing. Fully wet gives the processed branch alone."},
    {"output", "DYN OUTPUT. Trims the module output level. Compare bypass at matched loudness to judge the processing."},
    {"true-peak", "TRUE PEAK. Enables true-peak limiting at the DYN output with a 0 dBTP target. 04, 08 and 16 select the interpolation factor for peak detection."},
    {"true-peak-rate", "TRUE PEAK RATE. Chooses 4x, 8x or 16x interpolation for output peak detection. Higher settings cost more CPU."},
    {"bp-compensation-gain", "COMPENSATION. Applies the level offset to the bypass comparison so dry and processed loudness can be matched."},
};

constexpr Hint controls[] {
    {"ROUTING", "ROUTING. Opens the signal flow editor. Left to right is serial processing. Parallel branches are summed where they rejoin. Click empty space to return."},
    {"SLOTS", "SLOTS. Opens host automation and OSC assignments. Use SL on a parameter to assign it, then manage the assignments here. Click empty space to return."},
    {"ABOUT", "ABOUT. Opens information about AVA. Click empty space to return. Ctrl+Shift-click ABOUT to export the interface as SVG."},
    {"UNDO", "UNDO. Reverts the previous edit in AVA's history."},
    {"REDO", "REDO. Reapplies the last edit reverted by AVA's undo."},
    {"A/B", "A/B. Switches between two versions of the unit settings so you can compare them without rebuilding the chain."},
    {"COPY A TO B", "COPY A TO B. Copies the currently active comparison state into the other slot. If B is active, this copies B into A."},
    {"BYPASS", "BYPASS. Switches the processing off for comparison. Use COMPENSATION to match the comparison level."},
    {"COPY", "UNIT COPY. Copies the unit setup to the clipboard, including its routing and processing settings."},
    {"PASTE", "UNIT PASTE. Applies a copied unit setup from the clipboard to this unit."},
    {"ZOOM", "ZOOM. Scales the interface, including the control tiles and their text. 100% is the base scale."},
    {"WINDOW WIDTH", "WINDOW WIDTH. Sets the expanded editor width in control columns."},
    {"DOCK SIZE", "DOCK SIZE. Sets how many pinned-control slots are visible in the collapsed dock, alongside DOCK SCROLL. The expanded dock follows WINDOW WIDTH."},
    {"OSC", "OSC. Enables control and feedback via Open Sound Control. SLOTS shows the parameters currently assigned."},
    {"IN PORT", "IN PORT. UDP port on which AVA listens for incoming OSC. Match the destination port configured in the sender."},
    {"OUT PORT", "OUT PORT. Destination UDP port for outgoing OSC. Match the listening port of the receiver."},
    {"HOST", "HOST. Destination hostname or IP address for outgoing OSC. Use 127.0.0.1 for a receiver on this computer."},
    {"UNIT NUMBER", "UNIT NUMBER. Sets this unit's visible identifier. The number helps distinguish units in the link list."},
    {"SEND", "SEND. Sends subsequent changes from this unit to the selected LINK TO targets. Receiving is controlled separately."},
    {"RECEIVE", "RECEIVE. Allows this unit to apply incoming edits from linked units. Enable it on the destination."},
    {"LINKS", "LINKS. Sets how many LINK TO selectors are available. The same destination can only be selected once."},
    {"LINK TO", "LINK TO. Selects a destination for outgoing edits. Turn on SEND here and RECEIVE at the destination. NONE clears this destination."},
    {"INSTANCE LINK", "INSTANCE LINK. Select a named peer to toggle its link to this instance. LINK ALL toggles all peers together. NONE leaves existing links in place."},
    {"CHANNEL INPUT", "CHANNEL INPUT. Selects the input channel or pair for this processing instance. The selection also applies when CHANNEL SOLO is off."},
    {"OUT MUTE", "OUT MUTE. Removes this instance from the audible mix while leaving its processed signal available as a feed for other instances."},
    {"CHANNEL SOLO", "CHANNEL SOLO. Auditions the channel selection of this instance. CHANNEL INPUT still applies when solo is off."},
    {"NOTE SYNC", "NOTE SYNC. HOST follows the session tempo. MANUAL uses MANUAL BPM for this instance. Use MD on an individual time control to select milliseconds or note values."},
    {"MANUAL BPM", "MANUAL BPM. Supplies the tempo used by note-synced controls in manual tempo mode."},
    {"XOVERS", "XOVERS. Sets the number of crossover points. Each point adds another frequency band. Zero runs a single full-range chain."},
    {"AUTO SOLO", "AUTO SOLO. Auditions the selected band automatically while you work on its controls."},
    {"SOLO MODE", "SOLO MODE. EXCL solos a single band at a time. INCL lets you solo more than one band."},
    {"COPY ALL", "COPY ALL. Copies the current instance's processing setup to the clipboard."},
    {"PASTE ALL", "PASTE ALL. Applies the copied processing setup to this instance."},
    {"PRESET SELECT", "PRESET SELECT. Chooses an EQL preset and loads its filters into the current EQL module."},
    {"PRESET ADD", "PRESET ADD. Saves the current EQL filters as a new named preset."},
    {"PRESET SAVE", "PRESET SAVE. Updates the selected EQL preset with the current filters."},
    {"PRESET RENAME", "PRESET RENAME. Changes the name of the selected EQL preset."},
    {"PRESET DEFAULT", "PRESET DEFAULT. Marks the selected EQL preset as the default."},
    {"PRESET DELETE", "PRESET DELETE. Removes the selected EQL preset from the preset list. At least one preset must remain."},
    {"FILTER TYPE", "FILTER TYPE. Chooses the response shape of the selected filter. The shape determines which frequency, order, bandwidth and gain settings apply."},
    {"FILTER ORDER", "FILTER ORDER. Sets the steepness or order of the selected filter, when its type supports it."},
    {"FILTER FREQ", "FILTER FREQ. Sets the cutoff or centre frequency in Hz, according to the selected filter type."},
    {"FILTER BW", "FILTER BW. Sets the bandwidth in octaves. A smaller width concentrates the effect closer to the centre frequency."},
    {"FILTER GAIN", "FILTER GAIN. Boosts or cuts in dB where the selected filter type supports gain. With PH on, it sets the phase amount in degrees."},
    {"SORT", "EQL SORT. Sorts the filter tiles by ascending frequency, with volume filters first and phase-only filters in their own group at the end."},
    {"POSITION", "POSITION. Displays the current ORTHO angle and right-channel flip state."},
    {"SYNC", "SBL SYNC. Pairs this SBL with another instance. It takes the selected peer's detector settings when linked, and later detector edits follow both ways. TONAL and S solo and gain stay local. Choose NONE to unlink."},
    {"BP", "BP. Bypasses processing for this module while retaining its settings. Click again to restore the module."},
    {"SL", "SL. Assigns the parameter to a host automation slot or removes its assignment."},
    {"MT", "MT. Mutes the signal controlled by this switch."},
    {"DT", "DT. Auditions the difference between the aligned input and processed DYN signal. Use it to hear what the dynamics processing removes."},
    {"R/P", "R/P. Switches the display between rate and the equivalent pitch in semitones."},
    {"MD", "MD. Switches the time control between milliseconds and a musical note value. Note mode follows NOTE SYNC and its tempo source."},
    {"PH", "PH. Switches supported EQL shapes to phase-only processing. FILTER GAIN then sets the phase amount in degrees instead of a level change in dB."},
    {"SOLO", "BAND SOLO. Auditions this frequency band. SOLO MODE controls whether other bands can stay soloed at the same time."},
    {"LSN", "LSN. Auditions the selected sidechain instead of the ordinary dynamics output."},
    {"ONE", "ONE. Enables one-shot triggering in TRANS SUST."},
    {"04", "04. Uses 4x interpolation for DYN's output true-peak detector."},
    {"08", "08. Uses 8x interpolation for DYN's output true-peak detector."},
    {"16", "16. Uses 16x interpolation for DYN's output true-peak detector."},
};

constexpr Hint modules[] {
    {"EQL", "Shapes frequency response with a bank of parametric filters."},
    {"DYN", "Processes dynamics in the time or spectral domain, with compression, clipping and transient/sustain shaping."},
    {"SBL", "Separates sibilants from the rest of the signal so their gain and monitoring can be adjusted independently."},
    {"LISTEN", "Auditions individual channels, mid or side without changing their source routing."},
    {"GAIN", "Adjusts stereo, left, right, mid and side levels."},
    {"DELAY", "Aligns or offsets the timing of the stereo channels."},
    {"PHASE", "Rotates phase on both channels together or on each channel separately."},
    {"PAN", "Pans the left and right input channels independently."},
    {"SHEAR", "Feeds one stereo channel into the other by a controlled amount."},
    {"MS BAL", "Adjusts the stereo balance of the mid and side components separately."},
    {"ORTHO", "Rotates the stereo signal in the two-channel plane."},
    {"RECT", "Applies half-wave or full-wave rectification to the waveform."},
    {"COMB", "Processes the signal with a comb response, with adjustable count, offset and wet/dry mix."},
    {"SHIFT", "Splits the signal into original and pitch-processed branches around an adjustable cutoff."},
    {"VSPEED", "Changes playback speed with optional pitch preservation."},
};

constexpr Hint identities[] {
    {"SEARCH", "SEARCH. Type a control name to find and reveal it in this instance. Enter focuses search or opens the selected result. UP scrolls to the top; COLLAPSE folds open sections."},
    {"control-search-results", "SEARCH RESULTS. Choose a control to reveal and highlight it in the editor. Use the arrow keys and Enter, or click a result."},
    {"dock-sort-overlay", "DOCK SORT MODE. Drag a pinned control into the desired position. Click SORT DOCK again to return to normal editing."},
    {"sample-rate", "SAMPLE RATE / LATENCY. The top line is the host sample rate in Hz. The lower line is AVA's reported latency in samples."},
    {"status", "OSC PORT STATUS. BUSY indicates the configured input port is in use. FREE indicates that no port conflict is reported."},
    {"preset-list", "PRESET SELECT. Chooses an EQL preset and loads its filters into the current EQL module."},
    {"eql-filter-add", "ADD FILTER. Inserts a filter in the current EQL module and selects it for editing."},
    {"eql-filter-delete", "DELETE FILTER. Deletes the selected filter from the current EQL module. At least one filter must remain."},
    {"eql-filter-copy", "COPY FILTER. Copies the selected filter's settings to the clipboard."},
    {"eql-filter-paste", "PASTE FILTER. Applies the copied filter settings to the selected filter."},
    {"ALL\nCOPY", "COPY FILTERS. Copies the whole current EQL filter bank to the clipboard."},
    {"ALL\nPASTE", "PASTE FILTERS. Replaces the current EQL filter bank with the copied bank."},
    {"module-copy-settings", "MODULE COPY. Copies the settings of this module. Use PASTE on a module of the same type."},
    {"module-paste-settings", "MODULE PASTE. Applies the copied settings to this module. The clipboard must contain settings for the same module type."},
    {"routing.containers", "CN. Shows or hides the outlines and controls of parallel containers in ROUTING. This changes the view only."},
    {"dock-clip", "CLIP. Lights briefly when a processed sample reaches or exceeds 0 dBFS."},
    {"Sort dock", "SORT DOCK. Enables rearrangement of pinned controls. Drag a tile to its new position and turn this off to return to editing."},
    {"Collapse or expand dock", "COLLAPSE / EXPAND. Switches between the full editor and the compact dock. The expanded window size is remembered."},
    {"Vertical dock", "VERTICAL DOCK. Rotates the collapsed dock between a horizontal row and a vertical column. Collapse the dock first."},
    {"window-drag-handle", "WINDOW. Drag the top margin to move the plugin window."},
};

template <size_t N>
juce::String lookup(const juce::String& key, const Hint (&table)[N])
{
    for (const auto& hint : table)
        if (key == hint.key) return hint.text;
    return {};
}

juce::String caption(const juce::String& title)
{
    return title.replaceCharacters("\n-", "  ").trim().toUpperCase();
}

juce::String parameterHint(const juce::String& id)
{
    if (id.startsWith("filter-"))
    {
        const auto suffix = id.fromFirstOccurrenceOf("_", false, false);
        if (suffix == "phase") return lookup("PH", controls);
        return lookup("FILTER " + suffix.toUpperCase(), controls);
    }
    auto key = id.startsWith("band-") ? id.fromFirstOccurrenceOf("_", false, false) : id;
    // Time values in NOTE mode are different parameters with the same purpose.
    if (key.startsWith("processor-") && key.endsWith("-note"))
        key = key.dropLastCharacters(5);
    return lookup(key, parameters);
}

juce::String slotHint()
{
    return "SL. Assigns this parameter to the first free host automation slot. If already assigned, it removes that assignment. Open SLOTS to inspect or move assignments.";
}

juce::String scrollHint(const juce::String& id, const juce::String& title)
{
    juce::String target = "Scrolls the controls in this row left or right.";
    if (id == "pinned-routing-scroll") target = "Scrolls the pinned instance list left or right.";
    else if (id == "quick-access-scroll") target = "Scrolls the dock along its current orientation.";
    else if (id == "slots-vertical-scroll") target = "Scrolls the automation slot list vertically.";
    else if (id == "pinned-instance-vertical-scroll") target = "Scrolls the main editor vertically.";
    return title + ". " + target + " Drag the body or click it to enter a position from 00 to 99. "
           "The upper marker shows the current position.";
}

juce::String forControl(juce::Component& component, juce::Point<int> point)
{
    auto& properties = component.getProperties();
    if (const auto explicitHint = properties["controlHelp"].toString(); explicitHint.isNotEmpty())
        return explicitHint;

    const auto id = component.getComponentID();
    if (auto hint = lookup(properties["controlHelpIdentity"].toString(), identities); hint.isNotEmpty())
        return hint;
    if (auto hint = lookup(id, identities); hint.isNotEmpty()) return hint;
    if (auto hint = lookup(component.getName(), identities); hint.isNotEmpty()) return hint;
    if (auto hint = lookup(component.getTitle(), identities); hint.isNotEmpty()) return hint;

    if (auto* value = dynamic_cast<ValueBoxComponent*>(&component))
    {
        if (auto* field = value->findParentComponentOfClass<ParameterField>())
            if (field->getProperties()["dockInstanceNumber"].toString().isNotEmpty()
                && value->getBottomIndicatorBounds(value->getValueBounds().reduced(controlEdgeInset), 0).contains(point))
                return "DOCK SOURCE. Shows the number of the instance that owns this pinned control. 00 denotes a unit-level control.";
        const auto indicator = value->bottomIndicatorAt(point);
        if (indicator == 0) return slotHint();
        if (indicator == 1) return "BP. Bypasses this parameter while retaining its setting. Click again to restore its effect.";
        return {};
    }
    if (auto* field = dynamic_cast<ParameterField*>(&component))
    {
        const auto title = caption(field->getTitleText());
        if (dynamic_cast<LocalContinuousControl*>(field) != nullptr) return scrollHint(id, title);
        if (title == "POSITION") return lookup(title, controls);
        if (auto* parameter = dynamic_cast<ParameterControl*>(field))
            if (auto hint = parameterHint(parameter->getParameterId()); hint.isNotEmpty()) return hint;
        if (auto* choice = dynamic_cast<ChoiceControl*>(field))
            if (auto hint = parameterHint(choice->getParameterId()); hint.isNotEmpty()) return hint;
        if (auto hint = lookup(title, controls); hint.isNotEmpty())
        {
            if (auto* choice = dynamic_cast<ChoiceField*>(field);
                choice != nullptr && (title == "DOCK SIZE" || title == "WINDOW WIDTH"
                    || title == "UNIT NUMBER" || title == "LINKS"))
            {
                const auto& items = choice->getChoiceItems();
                if (items.size() > 1)
                    hint << " Range " << items[0] << " to " << items[items.size() - 1] << ".";
            }
            return hint;
        }
        if (title.startsWith("SPLIT "))
            return title + ". Sets the frequency where two adjacent crossover bands meet. Used when this split is enabled by XOVERS.";
        if (title.startsWith("BAND "))
            return title + ". Selects this frequency band's processing chain and settings. Use SOLO to hear the band in isolation.";
        if (title.isEmpty()) return {};
        if (dynamic_cast<ChoiceField*>(field) != nullptr)
            return title + ". Chooses the setting for this control.";
        if (dynamic_cast<PushControl*>(field) != nullptr)
            return title + ". Click and release to run this action.";
        if (auto* text = dynamic_cast<TextControl*>(field); text != nullptr && !text->isEditable())
            return title + ". Displays the current value.";
        if (dynamic_cast<ParameterControl*>(field) != nullptr)
            return title + ". Drag to adjust the value, or click to edit it directly where available.";
        return title + ". Click to edit the value.";
    }

    if (id.startsWith("host-slot-"))
        return "AUTOMATION SLOT. Shows the assigned parameter, or a blank if unused. Drag to another slot to move the assignment. "
               "Ctrl-click copies its OSC address. Ctrl+Shift-click clears the slot.";
    if (id.startsWith("routing.compensation."))
        return "C. Enables gain compensation for this parallel container. With compensation on, each branch input is divided by the number of branches before summing. Ctrl-click assigns the switch to a slot.";
    if (id.startsWith("routing.petal."))
    {
        if (id.endsWith(".before")) return "SERIAL BEFORE. Click to insert an instance before this point, or drop an existing instance here.";
        if (id.endsWith(".after")) return "SERIAL AFTER. Click to insert an instance after this point, or drop an existing instance here.";
        if (id.endsWith(".above")) return "PARALLEL ABOVE. Click to add a parallel branch above this point, or drop an existing instance here.";
        return "PARALLEL BELOW. Click to add a parallel branch below this point, or drop an existing instance here.";
    }
    if (auto* bar = dynamic_cast<juce::ScrollBar*>(&component))
        return bar->isVertical() ? "VERTICAL SCROLLBAR. Drag to move the view up or down."
                                 : "HORIZONTAL SCROLLBAR. Drag to move the view left or right.";
    if (auto* tile = dynamic_cast<BoxTextButton*>(&component))
    {
        const auto title = caption(tile->getButtonText());
        if (bool(properties["routingPinnedTile"]) || id.startsWith("routing.instance."))
        {
            switch (tile->bottomIndicatorAt(point))
            {
                case 0: return "S. Solos this instance for monitoring.";
                case 1: return "MT. Mutes this instance in the routing graph. For a silent sidechain feed, use OUT MUTE in INSTANCE MAIN.";
                case 2: return "BP. Bypasses processing in this instance so you can compare the incoming signal.";
                default: break;
            }
            if (bool(properties["routingPinnedTile"]))
                return title + ". Click to open this instance. Drag to change the pinned display order.";
            return title + ". Click to open. Shift-click renames it. Ctrl-click removes it where permitted. Ctrl+Alt-click toggles bypass. "
                "Drag it to an insertion petal to move it in the signal graph.";
        }
        const auto catalogTile = id.startsWith("module-type-");
        if (catalogTile || id.startsWith("module-slot-"))
        {
            auto name = title;
            const auto suffix = title.fromLastOccurrenceOf(" ", false, false);
            if (suffix.isNotEmpty() && suffix.containsOnly("0123456789"))
                name = title.dropLastCharacters(suffix.length()).trimEnd();
            const auto purpose = lookup(name, modules);
            auto hint = title + ". ";
            if (purpose.isNotEmpty()) hint << purpose << " ";
            return hint + (catalogTile
                ? "Drag into MODACT to add it with default settings. Drag within MODULES to reorder the catalog."
                : "Drag to change its processing order. Ctrl-click removes it from the chain.");
        }
        if (auto hint = lookup(title, controls); hint.isNotEmpty()) return hint;
        if (title.isNotEmpty()) return title + ". Click to select.";
    }
    if (auto* accessory = dynamic_cast<AccessoryButton*>(&component))
    {
        if (auto hint = parameterHint(accessory->getParameterId()); hint.isNotEmpty()) return hint;
        if (auto hint = lookup(caption(accessory->getButtonText()), controls); hint.isNotEmpty()) return hint;
    }
    if (auto* button = dynamic_cast<juce::Button*>(&component))
    {
        if (auto hint = lookup(caption(button->getButtonText()), controls); hint.isNotEmpty()) return hint;
    }
    if (auto* block = dynamic_cast<ControlBlockComponent*>(&component))
        for (const auto& frame : block->controlBlockFrames)
            if (frame.bounds.withHeight(controlBlockHeaderHeight).contains(point))
            {
                const auto name = frame.displayTitle.isNotEmpty() ? frame.displayTitle : frame.title;
                if (name.isNotEmpty()) return caption(name) + ". Click the heading to expand or collapse the section.";
            }
    return {};
}
}

juce::String control_help::describe(juce::Component& component, juce::Point<int> screen)
{
    const auto point = component.getLocalPoint(nullptr, screen);
    for (auto* child : component.getChildren())
    {
        if (!child->isVisible()) continue;
        const auto childPoint = child->getLocalPoint(&component, point);
        if (!child->getLocalBounds().contains(childPoint)) continue;
        bool acceptsMouse = false, acceptsChildMouse = false;
        child->getInterceptsMouseClicks(acceptsMouse, acceptsChildMouse);
        if (acceptsMouse || acceptsChildMouse) continue;
        if (auto hint = forControl(*child, childPoint); hint.isNotEmpty()) return uppercaseText(hint);
    }
    return uppercaseText(forControl(component, point));
}

bool control_help::paint(juce::Graphics& graphics, const juce::String& text,
                         juce::Rectangle<int> bounds, double startedAtMs)
{
    const juce::Graphics::ScopedSaveState state(graphics);
    graphics.reduceClipRegion(bounds);
    graphics.setColour(juce::Colour(0xff222222));
    graphics.fillRect(bounds);
    graphics.setColour(uiWhite);
    const auto font = makeUiFont();
    graphics.setFont(font);
    // Start every new hint at its title. Use the existing marquee with a short
    // reading pause and a quicker scroll for the longer help sentences.
    const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startedAtMs;
    const auto marqueeTime = juce::jmax(0.0, elapsedMs - 800.0) * 0.002;
    return drawLoopingText(graphics, text, bounds.reduced(uiGap, 0), font,
                           juce::Justification::centred, marqueeTime);
}
