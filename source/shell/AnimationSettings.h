#pragma once
#include <JuceHeader.h>
#include "UppercaseText.h"
#include <array>
#include <atomic>
#include <cstdint>

inline constexpr std::array<std::uint32_t, 10> animationColours {
    0xFF9999, 0xFFCC99, 0xFFFF99, 0x99FF99, 0x99FFFF,
    0x9999FF, 0xCC99FF, 0xFF99CC, 0xFFFFFF, 0xBBBBBB
};

inline constexpr int animationModeCount = 6;
inline constexpr int customTextAnimationMode = 6;

struct AnimationModeSettings
{
    std::atomic<int> colour { static_cast<int>(animationColours.size()) - 1 };
    std::atomic<float> speed { 1.0f };
    std::atomic<bool> moving { true };
    std::atomic<float> glow { 1.0f };
    std::atomic<float> scaleFactor { 0.1f };
    std::atomic<float> thickness { 1.0f };
    std::atomic<float> density { 14.0f };
    std::atomic<float> cropY { 0.0f };
};

inline const char* animationModeName(int mode) noexcept
{
    return mode == customTextAnimationMode ? "custom-text" : mode == 5 ? "track-item-name" : mode == 4 ? "item-name" : mode == 3 ? "track-name" : mode == 2 ? "topographic" : "burning-film";
}

struct AnimationBandSettings
{
    // 0 = Off, 1 = Burning, 2 = Topographic, 3 = Track name, 4 = Item name, 5 = Track/item name, 6 = Custom text.
    std::atomic<int> animation { 1 };
    std::array<AnimationModeSettings, animationModeCount> modes;
    AnimationBandSettings() { modes[0].speed.store(2.0f); }
    void setCustomText(juce::String text)
    {
        const juce::ScopedLock lock(customTextLock);
        customText = uppercaseText(text);
    }
    juce::String getCustomText() const
    {
        const juce::ScopedLock lock(customTextLock);
        return customText;
    }
    mutable juce::CriticalSection customTextLock;
    juce::String customText;
    AnimationModeSettings& current() noexcept { return modes[static_cast<size_t>(juce::jlimit(1, animationModeCount, animation.load()) - 1)]; }
    const AnimationModeSettings& current() const noexcept { return modes[static_cast<size_t>(juce::jlimit(1, animationModeCount, animation.load()) - 1)]; }
};

struct AnimationSettings
{
    std::atomic<bool> same { false }, sync { false };
    AnimationBandSettings top, bottom;
    std::atomic<bool> hostIsItem { false };
    void setHostName(juce::String text, bool onItem, juce::String itemName = {})
    {
        const juce::ScopedLock lock(hostNameLock);
        hostName = uppercaseText(text);
        hostItemName = onItem ? uppercaseText(itemName) : juce::String {};
        hostIsItem.store(onItem);
    }
    juce::String getHostName(int mode = 3) const
    {
        const juce::ScopedLock lock(hostNameLock);
        if (mode == 4) return hostItemName;
        if (mode == 5 && hostItemName.isNotEmpty())
            return hostName.isEmpty() ? hostItemName : hostName + " / " + hostItemName;
        return hostName;
    }
    mutable juce::CriticalSection hostNameLock;
    juce::String hostName, hostItemName;
    AnimationBandSettings& band(bool isBottom) noexcept { return isBottom && !same.load() ? bottom : top; }
    const AnimationBandSettings& band(bool isBottom) const noexcept { return isBottom && !same.load() ? bottom : top; }
};
