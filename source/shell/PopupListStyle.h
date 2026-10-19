#pragma once

#include <JuceHeader.h>

namespace popup_list
{
inline constexpr int itemHeight=30;
inline constexpr float lineThickness=1.0f;

inline void fillLine(juce::Graphics& graphics,float left,float top,float right,float bottom)
{
    graphics.fillRect(juce::Rectangle<float>(left,top,right-left,bottom-top));
}

// Internal boundaries only. Outer edges belong exclusively to paintFrame().
inline void paintSeparators(juce::Graphics& graphics,int width,int itemCount)
{
    for(int index=1;index<itemCount;++index) {
        const auto y=float(index*itemHeight);
        fillLine(graphics,0.0f,y-lineThickness,float(width),y);
    }
}

inline void paintFrame(juce::Graphics& graphics,juce::Rectangle<float> bounds,
                       bool top=true,bool bottom=true,bool left=true,bool right=true)
{
    if(top) fillLine(graphics,bounds.getX(),bounds.getY(),bounds.getRight(),bounds.getY()+lineThickness);
    if(bottom) fillLine(graphics,bounds.getX(),bounds.getBottom()-lineThickness,bounds.getRight(),bounds.getBottom());
    if(left) fillLine(graphics,bounds.getX(),bounds.getY(),bounds.getX()+lineThickness,bounds.getBottom());
    if(right) fillLine(graphics,bounds.getRight()-lineThickness,bounds.getY(),bounds.getRight(),bounds.getBottom());
}
}
