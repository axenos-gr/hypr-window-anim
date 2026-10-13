// SPDX-License-Identifier: GPL-3.0-or-later
// Doom's screen melt: columns slide down, each starting a little after its neighbour.

uniform float uSeed;
uniform float uColumn; // column width, logical px
uniform float uSpread; // how far apart the columns start, progress units

void main() {
  vec2  px  = iTexCoord * uSize;
  float col = floor(px.x / max(1.0, uColumn * uMonitorScale));

  // smoothed random walk, like the original
  float walk  = sin(col * 0.37 + uSeed * 31.0) * 0.5 + sin(col * 0.13 + uSeed * 17.0) * 0.35 + sin(col * 1.7 + uSeed * 7.0) * 0.15;
  float delay = uSpread * clamp(0.5 + 0.45 * walk + 0.05 * hash11(col + uSeed * 91.0), 0.0, 1.0);
  float fall  = clamp((uForOpening ? 1.0 - uProgress : uProgress) - delay, 0.0, 1.0 - uSpread) / (1.0 - uSpread);

  // closing melts down and out; opening drops the columns in from above
  float offset = fall * fall * uSize.y;
  vec2  src    = vec2(px.x, uForOpening ? px.y + offset : px.y - offset);

  vec4 color = getInputColor(src / uSize);
  color.rgb *= 1.0 - 0.25 * smoothstep(0.0, 0.3, fall) * (1.0 - fall); // falling columns dim a bit

  setOutputColor(color);
}
