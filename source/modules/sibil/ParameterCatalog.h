#pragma once
#include "../shared/BlockParameterTypes.h"
namespace ava::modules::sibil {
using namespace ava::modules::chain::parameters;
inline constexpr auto parameterCatalog=std::to_array<SlotDefinition>({
    {ParameterSlot::sibilLevel, {"sibil.level",ParameterType::floating,-96.0f,0.0f,0.01f,-50.0f,"dB",2}, {"SBL","THRESH"}},
    {ParameterSlot::sibilBrightness, {"sibil.brightness",ParameterType::floating,0.0f,100.0f,0.01f,50.0f,"",2}, {"SBL","BRIGHT"}},
    {ParameterSlot::sibilNoise, {"sibil.noise",ParameterType::floating,0.0f,100.0f,0.01f,20.0f,"",2}, {"SBL","NOISE"}},
    {ParameterSlot::sibilWindow, {"sibil.window",ParameterType::floating,0.0f,100.0f,0.01f,20.0f,"ms",2}, {"SBL","WINDOW"}},
    {ParameterSlot::sibilFade, {"sibil.fade",ParameterType::floating,0.0f,100.0f,0.01f,5.0f,"ms",2}, {"SBL","CROSS FADE"}},
    {ParameterSlot::sibilGainS, {"sibil.gain-s",ParameterType::floating,-48.0f,48.0f,0.01f,0.0f,"dB",2}, {"SBL","GAIN S"}},
    {ParameterSlot::sibilGainRest, {"sibil.gain-rest",ParameterType::floating,-48.0f,48.0f,0.01f,0.0f,"dB",2}, {"SBL","GAIN TONAL"}},
    {ParameterSlot::sibilSoloS, {"sibil.solo-s",ParameterType::boolean,0.f,1.f,1.f,0.f,""}, {"SBL","SOLO S"}},
    {ParameterSlot::sibilSoloRest, {"sibil.solo-rest",ParameterType::boolean,0.f,1.f,1.f,0.f,""}, {"SBL","SOLO TONAL"}},
    {ParameterSlot::sibilBypass, {"sibil.bypass",ParameterType::boolean,0.f,1.f,1.f,0.f,""}, {"SBL","BYPASS"}},
    {ParameterSlot::sibilHidden, {"sibil.hidden",ParameterType::boolean,0.f,1.f,1.f,1.f,""}, {"SBL","HIDDEN"}},
    {ParameterSlot::sibilRange, {"sibil.range",ParameterType::boolean,0.f,1.f,1.f,0.f,""}, {"SBL","S-RANGE"}},
    {ParameterSlot::sibilMin, {"sibil.min",ParameterType::floating,-96.f,0.f,0.01f,-30.f,"dB",2}, {"SBL","S-MIN"}},
    {ParameterSlot::sibilMax, {"sibil.max",ParameterType::floating,-96.f,0.f,0.01f,-20.f,"dB",2}, {"SBL","S-MAX"}},
    {ParameterSlot::sibilPre, {"sibil.pre",ParameterType::floating,0.f,100.f,0.01f,0.f,"ms",2}, {"SBL","S PRE"}},
    {ParameterSlot::sibilPost, {"sibil.post",ParameterType::floating,0.f,100.f,0.01f,0.f,"ms",2}, {"SBL","S POST"}},
});
}
