// SPDX-License-Identifier: GPL-3.0-or-later
// The window crumbles into grains of itself that the wind blows away.

uniform float uSeed;
uniform vec4  uColor; // dust the grains fade into
uniform float uGrain; // grain size, logical px
uniform float uWind;  // how far the dust flies

const float SWEEP_END = 0.55; // the front has crossed the window
const float LIFE      = 0.45; // grain lifetime, progress units

vec2  gMin, gMax, gStart, gDir, gWind;
float gS, gK, gFront, gAmp, gCell, gLift;

// Cheap smooth noise in [-1, 1]
float wave(vec2 p) {
  return (sin(dot(p, vec2(1.0, 0.31)) + uSeed * 11.0) + sin(dot(p, vec2(-0.47, 0.88)) * 1.37 + uSeed * 23.0) +
          sin(dot(p, vec2(0.74, -0.67)) * 0.83 + uSeed * 5.0)) * 0.4;
}

float ageAt(vec2 p) {
  float t = dot(p - gStart, gDir) + wave(p / (90.0 * gS)) * gAmp;
  return (gFront - t) / gK;
}

// Smooth in p so the inversion holds
vec2 drift(vec2 p0, float a) {
  vec2  q    = p0 / gS;
  vec2  gust = vec2(sin(q.y * 0.021 + a * 7.0), cos(q.x * 0.017 + a * 6.0));
  float fast = 1.0 + 0.3 * sin(q.x * 0.011 + q.y * 0.007 + uSeed * 9.0);
  float far  = fast * (70.0 * a + 1450.0 * a * a);
  return gS * gLift * uWind * (gWind * far + gust * 45.0 * a);
}

// Grains around s, the drift-inverted source of px
vec4 grains(vec2 px) {
  vec2 s = px;
  for (int i = 0; i < 3; i++) {
    s = px - drift(s, max(ageAt(s), 0.0));
  }

  float as = ageAt(s);
  if (as < -0.02 || as > LIFE + 0.08) {
    return vec4(0.0);
  }
  vec2 ag = vec2(ageAt(s + vec2(gS, 0.0)) - as, ageAt(s + vec2(0.0, gS)) - as) / gS;

  vec4 result = vec4(0.0);
  vec2 base   = floor(s / gCell - 0.5);
  for (int j = 0; j < 4; j++) {
    vec2 c   = base + vec2(float(j % 2), float(j / 2));
    vec4 rnd = hash42(c + uSeed * 71.0);

    vec2  center = (c + 0.5) * gCell;
    float a      = as + dot(ag, center - s) - rnd.x * 0.05; // crumbly edge
    float f      = a / LIFE;
    if (f <= 0.0 || f > 1.0) {
      continue;
    }

    // a grain is its own patch of the window, shrinking and tumbling
    float size   = 1.0 - 0.75 * f * f;
    vec2  jitter = (rnd.yw - 0.5) * gCell * 1.2 * f; // breaks up the lattice
    vec2  local  = rotate(px - center - jitter - drift(center, a), (rnd.z - 0.5) * a * 25.0) / size;
    if (max(abs(local.x), abs(local.y)) > 0.5 * gCell) {
      continue;
    }

    vec4 col = getInputColor((center + local) / uSize);
    col.rgb  = mix(col.rgb, uColor.rgb, smoothstep(0.05, 0.6, f) * uColor.a);
    col.a   *= 1.0 - smoothstep(0.55, 1.0, f);
    result   = alphaOver(result, col);
  }
  return result;
}

void main() {
  float t = uForOpening ? 1.0 - uProgress : uProgress;

  gS    = uMonitorScale;
  gCell = max(1.0, uGrain * gS);
  gMin  = vec2(uPadding);
  gMax  = uSize - uPadding;

  vec2 rnd = hash22(vec2(uSeed * 53.1, uSeed * 7.3));
  gDir     = normalize(vec2(rnd.x < 0.5 ? 1.0 : -1.0, (rnd.y - 0.5) * 0.6));
  gStart   = vec2(gDir.x > 0.0 ? gMin.x : gMax.x, gDir.y > 0.0 ? gMin.y : gMax.y);
  gWind    = normalize(vec2(-0.45 * gDir.x, -1.0)); // up, away from what's left

  float span = dot(gMax - gMin, abs(gDir));
  gAmp       = 0.12 * span + 25.0 * gS;
  // wave() spans +-1.2: nothing darkened at t = 0
  gK     = (span + 2.4 * gAmp) / (SWEEP_END - 0.05);
  gFront = -1.2 * gAmp - 0.05 * gK + gK * t;
  gLift      = clamp(gK / (1600.0 * gS), 0.35, 1.0); // small windows: less wind, inversion converges

  vec2 px = iTexCoord * uSize;

  // still intact: this pixel's own grain hasn't left
  vec2  own  = floor(px / gCell);
  float aOwn = ageAt((own + 0.5) * gCell) - hash42(own + uSeed * 71.0).x * 0.05;
  vec4  color = vec4(0.0);
  if (aOwn <= 0.0) {
    color = getInputColor(iTexCoord);
    color.rgb *= 1.0 - 0.35 * smoothstep(-0.05, 0.0, aOwn); // darkens just before crumbling
  }

  vec4 dust = grains(px);
  vec2 edge = min(px, uSize - px);
  dust.a *= smoothstep(0.0, 40.0 * gS, min(edge.x, edge.y));

  setOutputColor(alphaOver(color, dust));
}
