#pragma once

class AvaAudioProcessor;

// Independent system-clipboard formats: an entire plugin and one INSTANCE/ALL.
// Neither format contains pointers or instance IDs from another process.
namespace ava::clipboard
{
bool copyPlugin(AvaAudioProcessor& processor);
bool pastePlugin(AvaAudioProcessor& processor);
bool copyInstanceAll(AvaAudioProcessor& processor);
bool pasteInstanceAll(AvaAudioProcessor& processor);
}
