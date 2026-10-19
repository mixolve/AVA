#include "SampleProcessor.h"

namespace tls::dsp {
DspCore::StereoSample DspCore::processSample(const double leftInput, const double rightInput)
{
  hasProcessedSamples = true;
  auto left = leftInput;
  auto right = rightInput;
  for (const auto block : activeOrder) {
    if(!derived.blockEnabled[size_t(block)]) continue;
    switch (block) {
    case blocks::Id::listen: {
      if (derived.listenMode != ListenMode::neutral) {
        const auto listenLeft = left;
        const auto listenRight = right;
        const auto listenMid = 0.5 * (left + right);
        const auto listenSide = 0.5 * (left - right);

        if (derived.listenMode == ListenMode::leftCenter) {
          left = listenLeft;
          right = listenLeft;
        } else if (derived.listenMode == ListenMode::rightCenter) {
          left = listenRight;
          right = listenRight;
        } else if (derived.listenMode == ListenMode::midCenter) {
          left = listenMid * derived.listenMsGain;
          right = left;
        } else if (derived.listenMode == ListenMode::sideCenter) {
          left = listenSide * derived.listenMsGain;
          right = left;
        } else if (derived.listenMode == ListenMode::leftLeft) {
          left = listenLeft;
          right = 0.0;
        } else if (derived.listenMode == ListenMode::rightRight) {
          left = 0.0;
          right = listenRight;
        } else if (derived.listenMode == ListenMode::sideStereo) {
          left = listenSide * derived.listenMsGain;
          right = -left;
        }
      }
      break;
    }
    case blocks::Id::gain: {
      if (derived.signalTransformEnabled) {
        for(const auto stage:activeGainOrder) {
          if(parameters.gainBypass[size_t(stage)]) continue;
          switch(stage) {
          case gain::Id::stereo:
            left *= derived.linkedGain;
            right *= derived.linkedGain;
            break;
          case gain::Id::left:
            left *= derived.leftGain;
            break;
          case gain::Id::right:
            right *= derived.rightGain;
            break;
          case gain::Id::mid: {
            const auto mid=0.5*(left+right)*derived.midGain;
            const auto side=0.5*(left-right);
            left=mid+side; right=mid-side;
            break;
          }
          case gain::Id::side: {
            const auto mid=0.5*(left+right);
            const auto side=0.5*(left-right)*derived.sideGain;
            left=mid+side; right=mid-side;
            break;
          }
          }
        }
      }
      break;
    }
    case blocks::Id::delay: {
      if (derived.delayEnabled && !leftDelayBuffer.empty() &&
          !rightDelayBuffer.empty()) {
        leftDelayBuffer[static_cast<size_t>(delayWritePosition)] = left;
        rightDelayBuffer[static_cast<size_t>(delayWritePosition)] = right;

        auto readLeftPosition = delayWritePosition - derived.leftDelaySamples;
        auto readRightPosition = delayWritePosition - derived.rightDelaySamples;

        if (readLeftPosition < 0)
          readLeftPosition += delayBufferSize;
        if (readRightPosition < 0)
          readRightPosition += delayBufferSize;

        left = leftDelayBuffer[static_cast<size_t>(readLeftPosition)];
        right = rightDelayBuffer[static_cast<size_t>(readRightPosition)];

        if (++delayWritePosition == delayBufferSize)
          delayWritePosition = 0;
      }
      break;
    }
    case blocks::Id::phase: {
      if (derived.phaseEnabled) {
        leftPhaseBuffer[static_cast<size_t>(phaseWritePosition)] = left;
        rightPhaseBuffer[static_cast<size_t>(phaseWritePosition)] = right;

        const auto delayedLeft = leftPhaseBuffer[static_cast<size_t>(
            wrapIndex(phaseWritePosition - phaseFilterLatency, phaseBufferSize))];
        const auto delayedRight = rightPhaseBuffer[static_cast<size_t>(
            wrapIndex(phaseWritePosition - phaseFilterLatency, phaseBufferSize))];
        auto hilbertLeft = 0.0;
        auto hilbertRight = 0.0;

        for (int tapIndex = 0; tapIndex < phaseFilterTaps; ++tapIndex) {
          const auto readPosition =
              wrapIndex(phaseWritePosition - tapIndex, phaseBufferSize);
          const auto coefficient =
              phaseFilterCoefficients[static_cast<size_t>(tapIndex)];
          hilbertLeft +=
              coefficient * leftPhaseBuffer[static_cast<size_t>(readPosition)];
          hilbertRight +=
              coefficient * rightPhaseBuffer[static_cast<size_t>(readPosition)];
        }

        left = (delayedLeft * derived.leftPhaseCosine) +
               (hilbertLeft * derived.leftPhaseSine);
        right = (delayedRight * derived.rightPhaseCosine) +
                (hilbertRight * derived.rightPhaseSine);
        phaseWritePosition = wrapIndex(phaseWritePosition + 1, phaseBufferSize);
      }
      break;
    }
    case blocks::Id::panorama: {
      if (derived.signalTransformEnabled) {
        const auto panoramaLeft = (left * derived.gLL) + (right * derived.gRL);
        const auto panoramaRight = (left * derived.gLR) + (right * derived.gRR);

        left = panoramaLeft;
        right = panoramaRight;
      }
      break;
    }
    case blocks::Id::shear: {
      if (derived.signalTransformEnabled) {
        if (parameters.impactToRight) {
          right = right + (derived.impactAmount * left);
        } else {
          left = left + (derived.impactAmount * right);
        }

      }
      break;
    }
    case blocks::Id::balance: {
      if (derived.signalTransformEnabled) {
        const auto linMid = (left + right) * 0.7071067811865476;
        const auto linSide = (left - right) * 0.7071067811865476;
        const auto midLeft =
            linMid * (derived.midAmount > 0.0 ? 1.0 - derived.midAmount : 1.0);
        const auto midRight =
            linMid * (derived.midAmount < 0.0 ? 1.0 + derived.midAmount : 1.0);
        const auto sideLeft =
            linSide * (derived.sideAmount > 0.0 ? 1.0 - derived.sideAmount : 1.0);
        const auto sideRight =
            linSide * (derived.sideAmount < 0.0 ? 1.0 + derived.sideAmount : 1.0);
        left = (midLeft + sideLeft) * 0.7071067811865476;
        right = (midRight - sideRight) * 0.7071067811865476;

      }
      break;
    }
    case blocks::Id::ortho: {
      if (derived.signalTransformEnabled) {
        const auto orthogonalLeft =
            (derived.orthogonalM11 * left) + (derived.orthogonalM12 * right);
        const auto orthogonalRight =
            (derived.orthogonalM21 * left) + (derived.orthogonalM22 * right);
        left = orthogonalLeft;
        right = orthogonalRight;

      }
      break;
    }
    case blocks::Id::rect: {
      if (derived.signalTransformEnabled) {
        if (parameters.halfPositive) {
          left = juce::jmax(left, 0.0);
          right = juce::jmax(right, 0.0);
        } else if (parameters.halfNegative) {
          left = juce::jmin(left, 0.0);
          right = juce::jmin(right, 0.0);
        } else if (parameters.fullPositive) {
          left = std::abs(left);
          right = std::abs(right);
        } else if (parameters.fullNegative) {
          left = -std::abs(left);
          right = -std::abs(right);
        }

      }
      break;
    }
    case blocks::Id::comb: {
      if (derived.combEnabled) {
        const auto output=combEngine.process(static_cast<float>(0.5*(left+right)));
        left=output[0]; right=output[1];
      }
      break;
    }
    }
  }
  if (orderFadeRemaining > 0) {
    const auto gain = orderFadingOut ? double(orderFadeRemaining - 1) / orderFadeLength
                                     : 1.0 - double(orderFadeRemaining - 1) / orderFadeLength;
    left *= gain;
    right *= gain;
    if (--orderFadeRemaining == 0 && orderFadingOut) {
      activeOrder = parameters.blockOrder;
      activeGainOrder = parameters.gainOrder;
      orderFadingOut = false;
      orderFadeRemaining = orderFadeLength;
    }
  }
  return {left, right};
}
}
