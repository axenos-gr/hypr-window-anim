// SPDX-License-Identifier: GPL-3.0-or-later
// A transporter beam: the window dissolves into shimmering light and sparkles.

uniform float uSeed;
uniform vec4  uColor;    // beam
uniform float uSparkle;  // sparkle size, logical px

void main() {
  float t  = uForOpening ? 1.0 - uProgress : uProgress;
  float S  = uMonitorScale;
  vec2  px = iTexCoord * uSize;

  vec4 window = getInputColor(iTexCoord);

  // the beam builds up, the window fades into it, then the light goes out
  float beam  = smoothstep(0.0, 0.45, t) * (1.0 - smoothstep(0.65, 1.0, t));
  float solid = 1.0 - smoothstep(0.25, 0.7, t);

  // vertical shimmer rising through the beam
  float shimmer = simplex2D(vec2(px.x / (6.0 * S), px.y / (60.0 * S) + t * 6.0) + uSeed * 10.0);
  vec4  color   = window;
  color.rgb     = mix(color.rgb, uColor.rgb, beam * (0.45 + 0.4 * shimmer) * uColor.a);
  color.a       = window.a * max(solid, beam * 0.55 * shimmer);

  // twinkling sparkles, one per cell, only over the window
  float cell = uSparkle * 2.5 * S;
  vec2  c    = floor(px / cell);
  vec4  rnd  = hash42(c + uSeed * 17.0);
  vec2  q    = px - (c + 0.2 + 0.6 * rnd.xy) * cell;
  float on   = sin(clamp((t - rnd.z * 0.45) / 0.45, 0.0, 1.0) * 3.14159);
  float size = uSparkle * S * (0.5 + rnd.w);
  float star = exp(-dot(q, q) / (size * size * 0.15)) + 0.6 * exp(-abs(q.x) / (size * 0.08)) * exp(-abs(q.y) / size) +
               0.6 * exp(-abs(q.y) / (size * 0.08)) * exp(-abs(q.x) / size);
  float twinkle = on * on * star * step(0.35, rnd.z + rnd.w * 0.3) * getInputColor((c + 0.5) * cell / uSize).a;

  color = alphaOver(color, vec4(mix(uColor.rgb, vec3(1.0), 0.6), clamp(twinkle, 0.0, 1.0)));
  setOutputColor(color);
}
