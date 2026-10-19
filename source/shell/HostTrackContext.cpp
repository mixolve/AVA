#include "HostTrackContext.h"
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wshadow-field-in-constructor")
#include <pluginterfaces/base/funknown.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#include <array>

namespace
{
// REAPER's published VST3 host interface (SDK/reaper_vst3_interfaces.h).
class ReaperHost : public Steinberg::FUnknown
{
public:
    virtual void* PLUGIN_API getReaperApi(const char*) = 0;
    virtual void* PLUGIN_API getReaperParent(Steinberg::uint32) = 0;
    virtual void* PLUGIN_API reaperExtended(Steinberg::uint32, void*, void*, void*) = 0;
};
const Steinberg::TUID reaperHostId = INLINE_UID(0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C);
}

struct HostTrackContext::Impl
{
    ~Impl() { if (host != nullptr) host->release(); }
    juce::CriticalSection lock;
    ReaperHost* host = nullptr;
    juce::String fallbackTrackName;
    Location location;
    double lastRefresh = -1000.0;
};
HostTrackContext::HostTrackContext() : impl(std::make_unique<Impl>()) {}
HostTrackContext::~HostTrackContext() = default;

void HostTrackContext::setIHostApplication(Steinberg::FUnknown* application)
{
    void* queried = nullptr;
    if (application != nullptr && application->queryInterface(reaperHostId, &queried) != Steinberg::kResultOk)
        queried = nullptr;
    const juce::ScopedLock lock(impl->lock);
    if (impl->host != nullptr) impl->host->release();
    impl->host = static_cast<ReaperHost*>(queried);
    impl->lastRefresh = -1000.0;
    impl->location = { impl->fallbackTrackName, false, {} };
}

void HostTrackContext::updateTrackProperties(const juce::AudioProcessor::TrackProperties& properties)
{
    const juce::ScopedLock lock(impl->lock);
    impl->fallbackTrackName = properties.name.value_or(juce::String {});
    impl->lastRefresh = -1000.0;
    if (impl->host == nullptr) impl->location = { impl->fallbackTrackName, false, {} };
}

HostTrackContext::Location HostTrackContext::refresh()
{
    const juce::ScopedLock lock(impl->lock);
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now - impl->lastRefresh < 250.0) return impl->location;
    impl->lastRefresh = now;
    auto trackName = impl->fallbackTrackName;
    juce::String itemName;
    bool onItem = false;
    if (auto* host = impl->host)
    {
        auto* track = host->getReaperParent(1);
        auto* take = host->getReaperParent(2);
        onItem = take != nullptr;
        using GetTrackName = bool (*)(void*, char*, int);
        using GetTakeName = const char* (*)(void*);
        if (track != nullptr)
            if (const auto getName = reinterpret_cast<GetTrackName>(host->getReaperApi("GetTrackName")))
            {
                std::array<char, 4096> name {};
                if (getName(track, name.data(), static_cast<int>(name.size())))
                {
                    name.back() = 0;
                    trackName = juce::String::fromUTF8(name.data());
                }
            }
        if (take != nullptr)
            if (const auto getName = reinterpret_cast<GetTakeName>(host->getReaperApi("GetTakeName")))
                if (const auto* name = getName(take)) itemName = juce::String::fromUTF8(name);
    }
    impl->location = { trackName, onItem, itemName };
    return impl->location;
}
