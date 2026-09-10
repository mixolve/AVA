#include "DspCore.h"

namespace tls::dsp {
DspCore::StereoSample DspCore::processSample(const double leftInput,
                                             const double rightInput) {
  auto left = leftInput;
  auto right = rightInput;

  if (derived.signalTransformEnabled) {
    left = leftInput * derived.linkedGain;
    right = rightInput * derived.linkedGain;

    for (const auto operation : derived.gainOrder) {
      if (operation == GainOperation::left) {
        left *= derived.leftGain;
      } else if (operation == GainOperation::right) {
        right *= derived.rightGain;
      } else {
        auto mid = 0.5 * (left + right);
        auto side = 0.5 * (left - right);

        if (operation == GainOperation::mid)
          mid *= derived.midGain;
        else
          side *= derived.sideGain;

        left = mid + side;
        right = mid - side;
      }
    }

    const auto panoramaLeft = (left * derived.gLL) + (right * derived.gRL);
    const auto panoramaRight = (left * derived.gLR) + (right * derived.gRR);

    if (parameters.impactToRight) {
      left = panoramaLeft;
      right = panoramaRight + (derived.impactAmount * panoramaLeft);
    } else {
      left = panoramaLeft + (derived.impactAmount * panoramaRight);
      right = panoramaRight;
    }

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

    const auto orthogonalLeft =
        (derived.orthogonalM11 * left) + (derived.orthogonalM12 * right);
    const auto orthogonalRight =
        (derived.orthogonalM21 * left) + (derived.orthogonalM22 * right);
    left = orthogonalLeft;
    right = orthogonalRight;

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
        left = listenMid;
        right = listenMid;
      } else if (derived.listenMode == ListenMode::sideCenter) {
        left = listenSide;
        right = listenSide;
      } else if (derived.listenMode == ListenMode::leftLeft) {
        left = listenLeft;
        right = 0.0;
      } else if (derived.listenMode == ListenMode::rightRight) {
        left = 0.0;
        right = listenRight;
      } else if (derived.listenMode == ListenMode::sideStereo) {
        left = listenSide;
        right = -listenSide;
      }
    }
  }

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

  return {left, right};
}
} // namespace tls::dsp
