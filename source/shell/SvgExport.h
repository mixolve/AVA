#pragma once
#include <JuceHeader.h>

namespace svg_export
{
// Export expands collapsed/overflowing content without resizing native windows.
// Implementations must restore the live layout and UI state after rendering.
class ExpandableContent
{
public:
    virtual ~ExpandableContent() = default;
    virtual void expandContentForSvgExport() = 0;
    virtual void restoreContentAfterSvgExport() = 0;
    /** Canvas height that holds all of the content; 0 keeps the current size. */
    virtual int getSvgExportCanvasHeight() const { return 0; }
    virtual void prepareSvgExportLayout(int) {}
    virtual void finishSvgExportLayout() {}
};

juce::String render(juce::Component& component);
void chooseFile(juce::Component& component);
#if JUCE_MAC
void* installKeyboardShortcut(juce::Component& component);
void removeKeyboardShortcut(void* monitor);
#endif
}
