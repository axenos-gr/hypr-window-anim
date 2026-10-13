// SPDX-License-Identifier: GPL-3.0-or-later
// The window breaks into hexagons that shrink away from a random point, edges glowing.

uniform float uSeed;
uniform vec4  uColor; // edge glow
uniform float uHex;   // hexagon size, logical px

const vec2 HEX = vec2(1.0, 1.7320508);

// Nearest hexagon centre and the offset from it
vec4 hexCell(vec2 p) {
  vec2 a = mod(p, HEX) - HEX * 0.5;
  vec2 b = mod(p - HEX * 0.5, HEX) - HEX * 0.5;
  return dot(a, a) < dot(b, b) ? vec4(a, p - a) : vec4(b, p - b);
}

// 0 at the centre, 0.5 on the edge
float hexDist(vec2 q) {
  q = abs(q);
  return max(dot(q, HEX * 0.5), q.x);
}

void main() {
  float t  = uForOpening ? 1.0 - uProgress : uProgress;
  float S  = uMonitorScale;
  vec2  px = iTexCoord * uSize;
  float size = uHex * S;

  vec4 cell   = hexCell(px / size);
  vec2 center = cell.zw * size;

  // the wave starts at a random point inside the window
  vec2  origin = mix(vec2(uPadding), uSize - uPadding, hash22(vec2(uSeed * 23.0, uSeed * 59.0)));
  float far    = length(max(origin - uPadding, uSize - uPadding - origin));
  float delay  = 0.55 * length(center - origin) / far + 0.12 * hash12(cell.zw + uSeed * 3.0);
  float h      = clamp((t - delay) / 0.33, 0.0, 1.0);

  // each hexagon shrinks and sinks into its glowing outline
  float shrink = 1.0 - easeInQuad(h);
  float d      = hexDist(cell.xy);
  vec4  color  = vec4(0.0);
  if (shrink > 0.0 && d < 0.5 * shrink) {
    color = getInputColor((center + cell.xy * size / shrink) / uSize);
    color.rgb = mix(color.rgb, uColor.rgb, h * 0.6 * uColor.a);
  }

  float outline = 1.0 - smoothstep(0.0, 1.5 * S / size, abs(d - 0.5 * shrink));
  float lit     = sin(h * 3.14159) * getInputColor(center / uSize).a;
  color         = alphaOver(color, vec4(uColor.rgb, outline * lit * uColor.a));

  setOutputColor(color);
}
