// SPDX-License-Identifier: GPL-3.0-or-later
// The window spirals into a black hole that collapses behind it.

uniform float uSeed;
uniform vec4  uColor;  // accretion glow
uniform float uTwist;  // turns of the spiral

void main() {
  float t  = uForOpening ? 1.0 - uProgress : uProgress;
  float S  = uMonitorScale;
  vec2  px = iTexCoord * uSize;

  vec2  center = mix(vec2(uPadding), uSize - uPadding, 0.4 + 0.2 * hash22(vec2(uSeed * 11.0, uSeed * 5.0)));
  float radius = length(uSize - 2.0 * uPadding) * 0.5;

  // shrink towards the centre, spinning faster the closer a point gets
  float pull  = easeInQuad(t);
  float scale = max(1.0 - pull * 1.05, 1e-3);
  vec2  d     = px - center;
  float r     = length(d);
  float swirl = uTwist * 6.28318 * pull * exp(-r / (radius * 0.6));
  vec2  src   = center + rotate(d, -swirl) / scale;

  vec4 color = getInputColor(src / uSize);

  // stretched and darkened near the horizon
  float horizon = 1.0 - smoothstep(0.0, radius * 0.5 * (1.0 - pull * 0.6), r);
  color.rgb    *= 1.0 - 0.75 * pull * horizon;
  color.a      *= 1.0 - smoothstep(0.85, 1.0, t);

  // accretion ring: tightens and brightens, then the hole collapses
  float ringR = mix(radius * 0.35, 4.0 * S, pull);
  float ring  = exp(-abs(r - ringR) / (6.0 * S + ringR * 0.08)) * smoothstep(0.08, 0.35, t) * (1.0 - smoothstep(0.88, 1.0, t));
  float spark = 0.6 + 0.4 * sin(atan(d.y, d.x) * 7.0 + swirl * 2.0 + t * 25.0);
  vec4  glow  = vec4(uColor.rgb, clamp(ring * spark * uColor.a, 0.0, 1.0));
  vec4  core  = vec4(vec3(0.0), (1.0 - smoothstep(0.0, ringR * 0.8, r)) * smoothstep(0.05, 0.3, t) * (1.0 - smoothstep(0.9, 1.0, t)));

  setOutputColor(alphaOver(alphaOver(color, core), glow));
}
