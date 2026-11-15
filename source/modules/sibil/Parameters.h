#pragma once
namespace ava::modules::sibil {
struct Parameters {
    bool hidden=true, bypassed=false, soloS=false, soloRest=false, rangeEnabled=false;
    float level=-50.f, brightness=50.f, noise=20.f, window=20.f, fade=5.f, pre=0.f, post=0.f;
    float gainS=0.f, gainRest=0.f;
    float minDb=-30.f, maxDb=-20.f;
};
}
