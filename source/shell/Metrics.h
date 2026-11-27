#pragma once

inline constexpr int uiGap = 8;
inline constexpr int uiGapDouble = uiGap * 2;
inline constexpr int editorScrollBarWidth = 12;
inline constexpr int editorScrollBarArea = editorScrollBarWidth + uiGap;
inline constexpr int minimumControlColumns = 4;
inline constexpr int viewportControlColumns = 6;
inline constexpr int maximumEditorHeight = 2000;

inline constexpr int controlBlockHeaderHeight = 22;
inline constexpr int controlBlockFrameGap = 4;
inline constexpr int controlBlockFrameThickness = 2;
inline constexpr int controlBlockFrameOutset = controlBlockFrameGap + controlBlockFrameThickness;
inline constexpr int controlBlockPadding = uiGap;

inline constexpr int controlInnerGap = 2;
// The 1px frame occupies the first pixel of the edge inset.
inline constexpr int controlEdgeInset = controlInnerGap + 1;
inline constexpr int controlBlockHeaderButtonInset = 4;
inline constexpr int unitIndicatorSize = 12;
