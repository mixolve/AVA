#pragma once
#include "../shared/BlockParameterTypes.h"
namespace ava::modules::sibil {
using namespace ava::modules::chain::parameters;
inline constexpr auto parameterCatalog=std::to_array<SlotDefinition>({
    {ParameterSlot::sibilLevel, {"sibil.level",ParameterType::floating,-96.0f,0.0f,0.01f,-50.0f,"dB",2}, {"SBL","LEVEL DETECT"}},
    {ParameterSlot::sibilBrightness, {"sibil.brightness",ParameterType::floating,0.0f,100.0f,0.01f,50.0f,"",2}, {"SBL","BRIGHT DETECT"}},
    {ParameterSlot::sibilNoise, {"sibil.noise",ParameterType::floating,0.0f,100.0f,0.01f,20.0f,"",2}, {"SBL","NOISINESS DETECT"}},
    {ParameterSlot::sibilFade, {"sibil.fade",ParameterType::floating,0.0f,100.0f,0.01f,5.0f,"ms",2}, {"SBL","CROSS FADE"}},
    {ParameterSlot::sibilGainS, {"sibil.gain-s",ParameterType::floating,-48.0f,48.0f,0.01f,0.0f,"dB",2}, {"SBL","GAIN S"}},
    {ParameterSlot::sibilGainRest, {"sibil.gain-rest",ParameterType::floating,-48.0f,48.0f,0.01f,0.0f,"dB",2}, {"SBL","GAIN TONAL"}},
    {ParameterSlot::sibilSoloS, {"sibil.solo-s",ParameterType::boolean,0.f,1.f,1.f,0.f,""}, {"SBL","SOLO S"}},
    {ParameterSlot::sibilSoloRest, {"sibil.solo-rest",ParameterType::boolean,0.f,1.f,1.f,0.f,""}, {"SBL","SOLO TONAL"}},
    {ParameterSlot::sibilBypass, {"sibil.bypass",ParameterType::boolean,0.f,1.f,1.f,0.f,""}, {"SBL","BYPASS"}},
    {ParameterSlot::sibilHidden, {"sibil.hidden",ParameterType::boolean,0.f,1.f,1.f,0.f,""}, {"SBL","HIDDEN"}},
    {ParameterSlot::sibilEdgeDetect, {"sibil.edge-detect",ParameterType::floating,0.f,100.f,0.01f,50.f,"",2}, {"SBL","EDGE DETECT"}},
});
}
