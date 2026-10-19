#pragma once
#include <JuceHeader.h>
#include "AuxiliaryWindowFocus.h"
#include "SvgExport.h"
#include "SvgExportLayout.h"
class HostShortcutDocumentWindow : public juce::DocumentWindow, public svg_export::ExpandableContent, private juce::FocusChangeListener
{
public:
    explicit HostShortcutDocumentWindow(const juce::Colour backgroundColour)
        : juce::DocumentWindow(juce::String(), backgroundColour, 0, false)
    {
        juce::Desktop::getInstance().addFocusChangeListener(this);
       #if JUCE_MAC
        svgMonitor = svg_export::installKeyboardShortcut(*this);
       #endif
    }

    ~HostShortcutDocumentWindow() override
    {
        juce::Desktop::getInstance().removeFocusChangeListener(this);
       #if JUCE_MAC
        svg_export::removeKeyboardShortcut(svgMonitor);
        if (previousKeyWindow != nullptr)
            setAuxiliaryTextInputActive(*this, false, previousKeyWindow);
       #endif
    }

    void expandContentForSvgExport() override {}
    void restoreContentAfterSvgExport() override {}
    int getSvgExportCanvasHeight() const override
    { return getHeight() + gui_export_layout::viewportOverflow(*this); }
    void prepareSvgExportLayout(int height) override
    {
        if (auto* content = getContentComponent())
        {
            exportContent = content;
            originalContentBounds = content->getBounds();
            content->setSize(content->getWidth(), content->getHeight() + juce::jmax(0, height - getHeight()));
        }
    }
    void finishSvgExportLayout() override
    {
        if (exportContent != nullptr) exportContent->setBounds(originalContentBounds);
        exportContent = nullptr;
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        const auto mods = key.getModifiers();
        if (mods.isCtrlDown() && mods.isAltDown() && mods.isShiftDown()
           #if JUCE_MAC
            && !mods.isCommandDown()
           #endif
            && (key.getKeyCode() == 'E' || key.getKeyCode() == 'e'))
        { svg_export::chooseFile(*this); return true; }
        return juce::DocumentWindow::keyPressed(key);
    }

    juce::BorderSize<int> getBorderThickness() const override
    {
        return {};
    }

protected:
    juce::BorderSize<int> getContentComponentBorder() const override
    {
        return {};
    }

    int getDesktopWindowStyleFlags() const override
    {
        auto flags = juce::DocumentWindow::getDesktopWindowStyleFlags();
       #if JUCE_MAC
        flags |= juce::ComponentPeer::windowIgnoresKeyPresses;
       #else
        if (passShortcutsToHost)
            flags |= juce::ComponentPeer::windowIgnoresKeyPresses;
       #endif
        return flags;
    }

private:
    void childBoundsChanged(juce::Component* child) override
    {
        // Expanding owned export content must not resize the native host window.
        if (exportContent == nullptr) juce::DocumentWindow::childBoundsChanged(child);
    }
    juce::Component::SafePointer<juce::Component> exportContent;
    juce::Rectangle<int> originalContentBounds;
   #if JUCE_MAC
    void* svgMonitor = nullptr;
   #endif
    void globalFocusChanged(juce::Component* focused) override
    {
        const auto active = focused != nullptr && isParentOf(focused)
            && dynamic_cast<juce::TextEditor*>(focused) != nullptr;
        if (active == !passShortcutsToHost) return;
        passShortcutsToHost = !active;
       #if JUCE_MAC
        setAuxiliaryTextInputActive(*this, active, previousKeyWindow);
       #else
        if (isOnDesktop())
            recreateDesktopWindow();
       #endif
        if (active)
            if (auto* peer = getPeer()) peer->grabFocus();
    }
    bool passShortcutsToHost = true;
   #if JUCE_MAC
    void* previousKeyWindow = nullptr;
   #endif
};

