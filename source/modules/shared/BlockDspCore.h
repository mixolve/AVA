#pragma once

#include "BlockDspParameters.h"
#include "SampleRangeBank.h"
#include "../listen/Processor.h"
#include "../gain/Processor.h"
#include "../delay/Processor.h"
#include "../phase/Processor.h"
#include "../panorama/Processor.h"
#include "../shear/Processor.h"
#include "../balance/Processor.h"
#include "../ortho/Processor.h"
#include "../rect/Processor.h"
#include "../comb/Processor.h"
#include "../shift/Processor.h"
#include "../varispeed/Processor.h"
#include "../sibil/Processor.h"

#include <optional>
#include <cstdint>

namespace ava::modules::chain::dsp {
// Dispatches independently ordered blocks. Each block owns its configuration,
// smoothing and delay/phase/engine state; the render graph owns stack transitions.
class DspCore final {
public:
    using Parameters=BlockParameters;
    struct StereoSample { double left=0.,right=0.; };
    void prepare(double sampleRate,int maxBlockSize,int numChannels);
    void reset();
    void setParameters(const Parameters& next);
    StereoSample processBlockSample(blocks::Id block,double left,double right);
    int getLatencySamples() const noexcept;
    int getBlockLatencySamples(blocks::Id id) const noexcept;
    int getShiftLatencySamples() const noexcept { return shift.getShiftLatencySamples(); }
    int getMaximumVarispeedLatencySamples() const noexcept { return varispeed.getMaximumLatencySamples(); }
    void beginVarispeedBlock(std::optional<int64_t> sample,std::optional<bool> playing=std::nullopt,bool offline=false) noexcept {
        varispeed.beginBlock(sample,playing,offline);
    }
private:
    void processStage(blocks::Id id,double& left,double& right) noexcept;
    bool stageActive(blocks::Id id) const noexcept;
    ListenProcessor listen;
    GainProcessor gainStage;
    DelayProcessor delay;
    PhaseProcessor phase;
    PanoramaProcessor panorama;
    ShearProcessor shear;
    BalanceProcessor balance;
    OrthoProcessor ortho;
    RectProcessor rect;
    CombProcessor comb;
    ShiftProcessor shift;
    VarispeedProcessor varispeed;
    SibilProcessor sibil;
    Parameters parameters;
    bool hasProcessedSamples=false;
};
using ProcessorBank=ava::modules::SampleRangeBank<DspCore>;
}
