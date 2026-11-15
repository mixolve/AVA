#pragma once

#include "Style.h"

namespace popup_list
{
inline int contentHeight(int count, const juce::Component& component)
{
    return count > 0 ? count * (getSquareControlSize(component) + getUiRowGap(component)) - getUiRowGap(component) : 0;
}
}
