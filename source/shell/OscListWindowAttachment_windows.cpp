#include "OscListWindowAttachment.h"

#if JUCE_WINDOWS
#include <windows.h>

namespace osc_list_window
{
void attachToOwner(juce::Component& list, juce::Component& owner)
{
    auto* listWindow = static_cast<HWND>(list.getWindowHandle());
    auto* ownerView = static_cast<HWND>(owner.getWindowHandle());
    if (listWindow == nullptr || ownerView == nullptr)
        return;

    auto* ownerWindow = GetAncestor(ownerView, GA_ROOT);
    if (ownerWindow == nullptr || ownerWindow == listWindow)
        return;

    SetWindowLongPtrW(listWindow, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(ownerWindow));
    SetWindowPos(listWindow, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void detachFromOwner(juce::Component& list)
{
    if (auto* listWindow = static_cast<HWND>(list.getWindowHandle()))
        SetWindowLongPtrW(listWindow, GWLP_HWNDPARENT, 0);
}
}
#endif
