#include "AuxiliaryWindowAttachment.h"

#if JUCE_WINDOWS
#include <windows.h>

namespace auxiliary_window
{
void attachToOwner(juce::Component& component, juce::Component& owner)
{
    auto* window = static_cast<HWND>(component.getWindowHandle());
    auto* ownerView = static_cast<HWND>(owner.getWindowHandle());
    if (window == nullptr || ownerView == nullptr)
        return;

    auto* ownerWindow = GetAncestor(ownerView, GA_ROOT);
    if (ownerWindow == nullptr || ownerWindow == window)
        return;

    SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(ownerWindow));
    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void detachFromOwner(juce::Component& component)
{
    if (auto* window = static_cast<HWND>(component.getWindowHandle()))
        SetWindowLongPtrW(window, GWLP_HWNDPARENT, 0);
}
}
#endif
