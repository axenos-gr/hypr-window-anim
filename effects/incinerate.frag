// SPDX-License-Identifier: GPL-3.0-or-later
// The window burns like paper from a random spot; ash flakes and embers drift up.

uniform float uSeed;
uniform vec4  uColor;    // flames
uniform vec4  uAshColor;
uniform float uAsh;      // particle density, 0..1
uniform float uScale;    // size of the burn pattern

const float BURN_END = 0.6;  // the front has crossed the window
const float CHAR_W   = 42.0; // browning ahead of the flames, px
const float EMBER_W  = 7.0;  // glowing rim, px

vec2  gMin, gMax, gOrigin;
float gS, gAmp, gFront, gK, gLift;

float burnDistance(vec2 p) {
  vec2 d = p - gOrigin;
  d.y *= d.y < 0.0 ? 0.55 : 1.35; // flames climb faster
  return length(d);
}

// Cheap noise in [-1, 1]; hot path
float wave(vec2 p) {
  return (sin(dot(p, vec2(1.0, 0.31)) + uSeed * 11.0) + sin(dot(p, vec2(-0.47, 0.88)) * 1.37 + uSeed * 23.0) +
          sin(dot(p, vec2(0.74, -0.67)) * 0.83 + uSeed * 5.0)) * 0.4;
}

// Distance the front travels before reaching p, px
float burnCoarse(vec2 p) {
  float d = burnDistance(p);
  return d + wave(p / (gS * 75.0 * uScale)) * 0.5 * min(gAmp, d * 1.6);
}

float burnFine(vec2 p) {
  float n = simplex2DFractal(p / (gS * 55.0 * uScale) + uSeed * 7.0) - 0.47;
  return burnCoarse(p) + n * gS * 120.0;
}

// Particle age at p, progress units
float ageAt(vec2 p) { return (gFront - burnCoarse(p) - EMBER_W * gS) / gK; }

// Shared updraft; smooth in p so it inverts
vec2 drift(vec2 p0, float a) {
  float phase = (p0.x * 0.019 + p0.y * 0.011) / gS;
  float sway  = 26.0 * sin(a * 11.0 + phase) * min(a * 10.0, 1.0);
  float wind  = 70.0 * a * sin(p0.y * 0.004 / gS + uSeed * 6.3);
  return gS * gLift * vec2(sway + wind, -(380.0 * a + 550.0 * a * a));
}

// One particle per cell near s; ages linearised around s
vec4 particles(vec2 px, vec2 s, float as, vec2 ag, float cell, float prob, float radius, float life,
               float kind) { // kind 0: specks and embers, 1: flakes
  cell *= gS;

  vec4 result = vec4(0.0);
  vec2 base   = floor(s / cell - 0.5);
  for (int j = 0; j < 4; j++) {
    vec2 c   = base + vec2(float(j % 2), float(j / 2));
    vec4 rnd = hash42(c + kind * 37.0 + uSeed * 113.0);
    if (rnd.x > prob * uAsh) {
      continue;
    }

    vec2 p0 = (c + 0.5 + (rnd.yz - 0.5) * 0.6) * cell;
    if (any(lessThan(p0, gMin)) || any(greaterThan(p0, gMax))) {
      continue;
    }

    float a = as + dot(ag, p0 - s) - rnd.w * 0.04;
    float f = a / life;
    if (f < 0.0 || f > 1.0) {
      continue;
    }

    vec2 q = rotate(px - p0 - drift(p0, a), rnd.w * 6.3 + a * 9.0);
    float r = radius * gS * (0.6 + 0.8 * rnd.y) * (1.0 - 0.5 * f);

    vec4 col;
    if (kind < 0.5 && rnd.w < 0.15) {
      // embers: hot core with a soft glow
      float dist = length(q);
      col.rgb    = mix(vec3(1.0, 0.9, 0.6), uColor.rgb, smoothstep(0.0, 0.4, f));
      col.rgb    = mix(col.rgb, vec3(0.5, 0.06, 0.02), smoothstep(0.55, 1.0, f));
      col.a      = (1.0 - f * f) * (1.0 - smoothstep(r * 0.5, r * 1.8, dist));
    } else {
      // tumbling flakes: smoulder briefly, char, then grey into ash
      q.x /= max(0.3, abs(cos(a * 13.0 + rnd.z * 6.3)));
      vec2  aq   = abs(q) * vec2(1.0, 1.6);
      float dist = max(max(aq.x, aq.y), (aq.x + aq.y) * (0.68 + 0.14 * rnd.z)); // chipped corners
      float glow = (1.0 - smoothstep(0.0, 0.18, f)) * (0.5 + 0.5 * smoothstep(0.0, r, dist));
      vec3  ash  = uAshColor.rgb * (0.55 + 0.45 * rnd.y);
      col.rgb    = mix(vec3(0.09, 0.06, 0.05), ash, smoothstep(0.08, 0.45, f));
      col.rgb    = mix(col.rgb, uColor.rgb, glow);
      col.a      = (1.0 - smoothstep(0.7, 1.0, f)) * clamp((r - dist) / gS + 0.5, 0.0, 1.0);
    }

    if (col.a > 0.0) {
      result = alphaOver(result, col);
    }
  }
  return result;
}

void main() {
  float t = uForOpening ? 1.0 - uProgress : uProgress;

  gS   = uMonitorScale;
  gMin = vec2(uPadding);
  gMax = uSize - uPadding;

  vec2 px  = iTexCoord * uSize;
  vec2 rnd = hash22(vec2(uSeed * 91.7, uSeed * 13.1));
  gOrigin  = mix(gMin, gMax, vec2(0.15 + 0.7 * rnd.x, 0.55 + 0.4 * rnd.y));

  float far = max(max(burnDistance(gMin), burnDistance(gMax)),
                  max(burnDistance(vec2(gMin.x, gMax.y)), burnDistance(vec2(gMax.x, gMin.y))));
  gAmp      = 0.25 * far;
  float lo  = -(CHAR_W + 60.0) * gS; // nothing scorched at t = 0
  float hi  = far + gAmp + (EMBER_W + 70.0) * gS;
  gK        = (hi - lo) / BURN_END;
  gFront    = lo + gK * t;
  gLift     = clamp(gK / (1500.0 * gS), 0.35, 1.0); // small windows: less lift, inversion converges

  // burning sheet
  float dc = burnCoarse(px) - gFront;
  vec4  color;

  if (dc > (CHAR_W + 60.0) * gS) {
    color = getInputColor(iTexCoord); // untouched yet
  } else if (dc < -(EMBER_W + 90.0) * gS) {
    color = vec4(0.0); // burnt
  } else {
    float d    = burnFine(px) - gFront;
    float heat = 1.0 - smoothstep(0.0, 50.0 * gS, d);

    vec2 shimmer = vec2(simplex2D(px / (12.0 * gS) + t * 35.0), simplex2D(px / (12.0 * gS) - t * 35.0 + 17.0));
    vec4 paper   = getInputColor((px + (shimmer - 0.5) * 5.0 * gS * heat) / uSize);
    float paperA = paper.a;

    float scorch = 1.0 - smoothstep(0.0, CHAR_W * gS, d);
    vec3  sepia  = vec3(dot(paper.rgb, vec3(0.3, 0.59, 0.11))) * vec3(0.95, 0.66, 0.38);
    paper.rgb    = mix(paper.rgb, sepia * 0.7, smoothstep(0.0, 0.55, scorch));
    paper.rgb    = mix(paper.rgb, vec3(0.05, 0.03, 0.02), smoothstep(0.5, 0.95, scorch));

    float flicker = smoothstep(0.3, 0.8, simplex2D(px / (9.0 * gS) + vec2(t * 4.0, t * 30.0)));
    float rim     = smoothstep(-EMBER_W * gS, 0.0, d) * (1.0 - smoothstep(0.0, 3.0 * gS, d));
    vec3  flame   = mix(uColor.rgb, vec3(1.0, 0.95, 0.75), flicker * rim);
    paper.rgb     = mix(paper.rgb, flame, rim * (0.55 + 0.45 * flicker));
    paper.a      *= smoothstep(-EMBER_W * gS, -EMBER_W * gS + 1.5 * gS, d);

    // light from the rim spills into the hole
    float halo = exp(min(0.0, d + EMBER_W * gS) / (5.0 * gS)) * (1.0 - paper.a / max(paperA, 0.001));
    color      = alphaOver(paper, vec4(flame, halo * paperA * (0.3 + 0.5 * flicker)));
  }

  // ash and embers
  if (uAsh > 0.0 && t > 0.0) {
    // invert the drift by fixed-point iteration
    vec2 s = px;
    for (int i = 0; i < 2; i++) {
      s = px - drift(s, max(ageAt(s), 0.0));
    }

    float as = ageAt(s);
    if (as > -0.06 && as < 0.48) {
      vec2 ag  = vec2(ageAt(s + vec2(gS, 0.0)) - as, ageAt(s + vec2(0.0, gS)) - as) / gS;
      vec4 ash = particles(px, s, as, ag, 17.0, 0.3, 6.0, 0.40, 1.0);
      ash      = alphaOver(ash, particles(px, s, as, ag, 10.0, 0.42, 2.8, 0.36, 0.0));

      vec2 edge = min(px, uSize - px);
      ash.a *= smoothstep(0.0, 40.0 * gS, min(edge.x, edge.y)); // don't clip at the box
      color = alphaOver(color, ash);
    }
  }

  setOutputColor(color);
}
