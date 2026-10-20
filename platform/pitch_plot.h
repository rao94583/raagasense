// Draws recent pitch against a grid of svaras with Core Graphics, for the
// AppKit and UIKit views alike.
#pragma once

#include <CoreGraphics/CoreGraphics.h>

#include <vector>

#include "core/pitch.h"

namespace raagasense {

struct PitchPlot {
  double sa_frequency = 138.59;
  // The stream time at the plot's right edge, and the seconds it spans.
  double now = 0;
  double window = 10;
  // Frames below this confidence aren't drawn.
  double min_confidence = 0.5;
  bool dark = false;
};

// Draws into bounds, in a context whose y axis points up (Core Graphics'
// default; UIKit callers flip their context first).
void DrawPitchPlot(CGContextRef context, CGRect bounds, const PitchPlot& plot,
                   const std::vector<PitchFrame>& frames);

}  // namespace raagasense
