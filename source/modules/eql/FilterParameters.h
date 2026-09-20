#pragma once

#include "Processor.h"

template <typename Callback>
void forEachFilterParameterId(const int filterIndex, Callback&& callback)
{
    callback(EqlModuleProcessor::getFilterTypeParamId(filterIndex));
    callback(EqlModuleProcessor::getFilterPlaceParamId(filterIndex));
    callback(EqlModuleProcessor::getFilterOrderParamId(filterIndex));
    callback(EqlModuleProcessor::getFilterFrequencyParamId(filterIndex));
    callback(EqlModuleProcessor::getFilterBandwidthParamId(filterIndex));
    callback(EqlModuleProcessor::getFilterGainParamId(filterIndex));
    callback(EqlModuleProcessor::getFilterBypassParamId(filterIndex));
}
