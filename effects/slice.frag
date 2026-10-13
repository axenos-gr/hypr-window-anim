// SPDX-License-Identifier: GPL-3.0-or-later
// A blade slashes through the window; the pieces slide apart and fall.

uniform float uSeed;
uniform vec4  uColor; // slash and cut edges
uniform float uCuts;  // 1..3 slashes

const float SLASH_END = 0.16; // slashes drawn, pieces start to move

vec3 gCut[3]; // line: dot(normal, p) - offset

// Bit k set: p is on the positive side of cut k
int pieceOf(vec2 p, int cuts) {
  int id = 0;
  for (int k = 0; k < 3; k++) {
    if (k < cuts && dot(gCut[k].xy, p) > gCut[k].z) {
      id |= 1 << k;
    }
  }
  return id;
}

float cutDistance(vec2 p, int cuts) {
  float best = 1e9;
  for (int k = 0; k < 3; k++) {
    if (k < cuts) {
      best = min(best, abs(dot(gCut[k].xy, p) - gCut[k].z));
    }
  }
  return best;
}

void main() {
  float t    = uForOpening ? 1.0 - uProgress : uProgress;
  int   cuts = int(clamp(uCuts, 1.0, 3.0));
  float S    = uMonitorScale;

  vec2 gMin   = vec2(uPadding);
  vec2 gMax   = uSize - uPadding;
  vec2 center = (gMin + gMax) * 0.5;
  vec2 extent = (gMax - gMin) * 0.5;
  vec2 px     = iTexCoord * uSize;

  vec2 points[3], dirs[3];
  for (int k = 0; k < 3; k++) {
    vec4  h     = hash41(float(k) * 3.7 + uSeed * 29.0);
    float angle = (h.x < 0.5 ? 1.0 : -1.0) * mix(0.35, 1.1, h.y); // never quite horizontal or vertical
    dirs[k]     = vec2(cos(angle), sin(angle));
    points[k]   = center + (h.zw - 0.5) * extent * 0.6;
    gCut[k]     = vec3(-dirs[k].y, dirs[k].x, dot(vec2(-dirs[k].y, dirs[k].x), points[k]));
  }

  float move  = clamp((t - SLASH_END) / (1.0 - SLASH_END), 0.0, 1.0);
  float ease  = easeOutQuad(move);
  vec4  color = vec4(0.0); // premultiplied

  // each piece slides away from the cuts, tips over and falls
  int pieces = 1 << cuts;
  for (int id = 0; id < 8; id++) {
    if (id >= pieces) {
      break;
    }

    vec2 away = vec2(0.0);
    for (int k = 0; k < 3; k++) {
      if (k < cuts) {
        away += ((id >> k) & 1) == 1 ? gCut[k].xy : -gCut[k].xy;
      }
    }
    away = away / max(length(away), 1e-3);

    vec3  h     = hash31(float(id) + uSeed * 13.0);
    float spin  = (h.x - 0.5) * 0.9 * move * move;
    vec2  pivot = center + away * extent * 0.5;
    vec2  shift = S * (away * 70.0 * ease + vec2((h.y - 0.5) * 120.0 * move, (700.0 + 300.0 * h.z) * move * move));

    vec2 p = pivot + rotate(px - pivot - shift, -spin);
    if (any(lessThan(p, gMin)) || any(greaterThan(p, gMax)) || pieceOf(p, cuts) != id) {
      continue;
    }

    vec4 c = getInputColor(p / uSize);
    // freshly cut edges glow, then cool
    float edge = 1.0 - smoothstep(0.0, 3.0 * S, cutDistance(p, cuts));
    c.rgb      = mix(c.rgb, uColor.rgb, edge * smoothstep(0.0, SLASH_END, t) * (1.0 - move) * uColor.a);
    c.a       *= 1.0 - smoothstep(0.6, 1.0, move);
    color      = vec4(c.rgb * c.a, c.a) + color * (1.0 - c.a);
  }

  // the slashes: a bright streak sweeps along each cut, one after another
  if (t < SLASH_END * 2.0) {
    vec3 slash = mix(uColor.rgb, vec3(1.0), 0.5);
    for (int k = 0; k < 3; k++) {
      if (k < cuts) {
        float start = float(k) * SLASH_END * 0.25;
        float sweep = clamp((t - start) / (SLASH_END * 0.5), 0.0, 1.0);
        vec2  d     = px - points[k];
        float along = dot(d, dirs[k]) / length(extent) * 0.5 + 0.5; // ~0..1 across the window
        float fade  = 1.0 - smoothstep(SLASH_END * 0.6, SLASH_END * 2.0, t - start);
        float width = S * mix(4.0, 1.0, sweep);
        float line  = t > start ? exp(-abs(dot(gCut[k].xy, d)) / width) * step(along, sweep * 1.4) * fade : 0.0;
        float a     = clamp(line * uColor.a, 0.0, 1.0);
        color       = vec4(slash * a, a) + color * (1.0 - a);
      }
    }
  }

  vec2 box = min(px, uSize - px);
  color *= smoothstep(0.0, 40.0 * S, min(box.x, box.y)); // fade, don't clip, at the box
  setOutputColor(color.a > 0.0 ? vec4(color.rgb / color.a, color.a) : vec4(0.0));
}
