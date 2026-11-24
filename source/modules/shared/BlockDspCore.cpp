#include "BlockDspCore.h"

namespace ava::modules::chain::dsp {
void DspCore::prepare(double sampleRate,int,int) {
    if(sampleRate<=0.)sampleRate=44100.;
    listen.prepare(sampleRate);gainStage.prepare(sampleRate);delay.prepare(sampleRate);
    phase.prepare(sampleRate);panorama.prepare(sampleRate);shear.prepare(sampleRate);
    balance.prepare(sampleRate);ortho.prepare(sampleRate);rect.prepare(sampleRate);
    comb.prepare(sampleRate);shift.prepare(sampleRate);varispeed.prepare(sampleRate);sibil.prepare(sampleRate);
    hasProcessedSamples=false;
    setParameters(parameters);
    reset();
}
void DspCore::reset() {
    listen.reset();gainStage.reset();delay.reset();phase.reset();panorama.reset();
    shear.reset();balance.reset();ortho.reset();rect.reset();comb.reset();shift.reset();varispeed.reset();sibil.reset();
    gainStage.setOrder(parameters.gainOrder);
    hasProcessedSamples=false;
}
void DspCore::setParameters(const Parameters& next) {
    parameters=next;
    if(!gain::valid(parameters.gainOrder))parameters.gainOrder=gain::defaultOrder;
    listen.setParameters(parameters.listen,hasProcessedSamples);
    gainStage.setParameters(parameters.gain,hasProcessedSamples);
    gainStage.setOrder(parameters.gainOrder);
    delay.setParameters(parameters.delay,hasProcessedSamples);
    phase.setParameters(parameters.phase,hasProcessedSamples);
    panorama.setParameters(parameters.panorama,hasProcessedSamples);
    shear.setParameters(parameters.shear,hasProcessedSamples);
    balance.setParameters(parameters.balance,hasProcessedSamples);
    ortho.setParameters(parameters.ortho,hasProcessedSamples);
    rect.setParameters(parameters.rect,hasProcessedSamples);
    comb.setParameters(parameters.comb,hasProcessedSamples);
    shift.setParameters(parameters.shift,hasProcessedSamples);
    varispeed.setParameters(parameters.varispeed,hasProcessedSamples);
    sibil.setParameters(parameters.sibil,hasProcessedSamples);
}
void DspCore::processStage(const blocks::Id id,double& left,double& right) noexcept {
    switch(id) {
    case blocks::Id::listen: listen.process(left,right);break;
    case blocks::Id::gain: gainStage.process(left,right);break;
    case blocks::Id::delay: delay.process(left,right);break;
    case blocks::Id::phase: phase.process(left,right);break;
    case blocks::Id::panorama: panorama.process(left,right);break;
    case blocks::Id::shear: shear.process(left,right);break;
    case blocks::Id::balance: balance.process(left,right);break;
    case blocks::Id::ortho: ortho.process(left,right);break;
    case blocks::Id::rect: rect.process(left,right);break;
    case blocks::Id::comb: comb.process(left,right);break;
    case blocks::Id::shift: shift.process(left,right);break;
    case blocks::Id::varispeed: varispeed.process(left,right);break;
    case blocks::Id::sibil: sibil.process(left,right);break;
    }
}
bool DspCore::stageActive(const blocks::Id id) const noexcept {
    switch(id) {
    case blocks::Id::listen: return listen.isActive();
    case blocks::Id::gain: return gainStage.isActive();
    case blocks::Id::delay: return delay.isActive();
    case blocks::Id::phase: return phase.isActive();
    case blocks::Id::panorama: return panorama.isActive();
    case blocks::Id::shear: return shear.isActive();
    case blocks::Id::balance: return balance.isActive();
    case blocks::Id::ortho: return ortho.isActive();
    case blocks::Id::rect: return rect.isActive();
    case blocks::Id::comb: return comb.isActive();
    case blocks::Id::shift: return shift.isActive();
    case blocks::Id::varispeed: return varispeed.isActive();
    case blocks::Id::sibil: return sibil.isActive();
    }
    return false;
}
DspCore::StereoSample DspCore::processBlockSample(blocks::Id block,double left,double right) {
    hasProcessedSamples=true;
    if(stageActive(block))processStage(block,left,right);
    return {left,right};
}
int DspCore::getLatencySamples() const noexcept {
    return delay.getLatencySamples()+phase.getLatencySamples()+comb.getLatencySamples()
        +shift.getLatencySamples()+varispeed.getLatencySamples()+sibil.getLatencySamples();
}
int DspCore::getBlockLatencySamples(const blocks::Id id) const noexcept {
    if(!stageActive(id))return 0;
    switch(id) {
    case blocks::Id::delay: return delay.getLatencySamples();
    case blocks::Id::phase: return phase.getLatencySamples();
    case blocks::Id::comb: return comb.getLatencySamples();
    case blocks::Id::shift: return shift.getLatencySamples();
    case blocks::Id::varispeed: return varispeed.getLatencySamples();
    case blocks::Id::sibil: return sibil.getLatencySamples();
    case blocks::Id::listen:
    case blocks::Id::gain:
    case blocks::Id::panorama:
    case blocks::Id::shear:
    case blocks::Id::balance:
    case blocks::Id::ortho:
    case blocks::Id::rect: return 0;
    }
    return 0;
}
}
