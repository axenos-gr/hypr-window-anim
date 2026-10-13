// SPDX-License-Identifier: GPL-3.0-or-later
// Fade out with a slight shrink. Built-in fallback effect.

void main() {
  float progress = easeOutQuad(uForOpening ? 1.0 - uProgress : uProgress);
  float scale    = mix(1.0, 0.94, progress);

  vec4 color = getInputColor((iTexCoord - 0.5) / scale + 0.5);
  color.a *= 1.0 - progress;

  setOutputColor(color);
}
