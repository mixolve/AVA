#pragma once
#include "HostShortcutWindow.h"
#include "AuxiliaryWindowAttachment.h"
#include "Style.h"

class OwnerAttachedWindow : public HostShortcutDocumentWindow, private juce::Timer
{
public:
    OwnerAttachedWindow() : HostShortcutDocumentWindow(uiBackground) {}
    ~OwnerAttachedWindow() override
    {
        stopTimer();
        ownerWatcher.reset();
        auxiliary_window::detachFromOwner(*this);
    }

    void showFor(juce::Component& owner)
    {
        ownerWatcher.reset();
        ownerComponent = &owner;
        const auto ownerBounds = owner.getScreenBounds();
        auto bounds = juce::Rectangle<int>(getAttachedWidth(), ownerBounds.getHeight())
            .withPosition(ownerBounds.getRight() + uiGap, ownerBounds.getY());
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(ownerBounds))
            bounds = bounds.constrainedWithin(display->userArea);
        bounds.setHeight(ownerBounds.getHeight());
        ownerOffset = bounds.getPosition() - juce::Point<int>(ownerBounds.getRight(), ownerBounds.getY());
        setBounds(bounds);
        auxiliary_window::attachToOwner(*this, owner);
        setVisible(true);
        toFront(false);
        ownerWatcher = std::make_unique<OwnerWatcher>(*this, owner);
       #if !JUCE_MAC
        // Native macOS child windows move as part of the owner's window group.
        // Other platforms retain polling for host moves outside the JUCE tree.
        startTimerHz(60);
       #endif
    }

protected:
    virtual int getAttachedWidth() const = 0;

private:
    class OwnerWatcher final : public juce::ComponentMovementWatcher
    {
    public:
        OwnerWatcher(OwnerAttachedWindow& windowIn, juce::Component& owner)
            : juce::ComponentMovementWatcher(&owner), window(windowIn) {}
        void componentMovedOrResized(bool, bool) override { window.syncWithOwner(); }
        void componentPeerChanged() override
        {
            if (auto* owner = getComponent()) auxiliary_window::attachToOwner(window, *owner);
            window.syncWithOwner();
        }
        void componentVisibilityChanged() override {}
    private:
        OwnerAttachedWindow& window;
    };
    void timerCallback() override { syncWithOwner(); }
    void syncWithOwner()
    {
        if (!isVisible() || ownerComponent == nullptr) return;
        const auto ownerBounds = ownerComponent->getScreenBounds();
        const auto bounds = juce::Rectangle<int>(getAttachedWidth(), ownerBounds.getHeight())
            .withPosition(juce::Point<int>(ownerBounds.getRight(), ownerBounds.getY()) + ownerOffset);
        if (getBounds() != bounds) setBounds(bounds);
    }
    juce::Component::SafePointer<juce::Component> ownerComponent;
    juce::Point<int> ownerOffset;
    std::unique_ptr<OwnerWatcher> ownerWatcher;
};
