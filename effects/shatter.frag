// SPDX-License-Identifier: GPL-3.0-or-later
// The window cracks like glass from an impact point, then the shards fly apart and fall.

uniform float uSeed;
uniform vec4  uColor;  // cracks and shard rims
uniform float uShards; // radial cracks around the impact
uniform float uBlow;   // how hard shards fly outwards
uniform float uGravity;

const float CRACK_END = 0.14; // cracks spread, then the fall starts
const int   LAYERS    = 4;    // shards of a layer move together

// Per layer: outward burst, spin, sideways drift, upward kick
const vec4 MOTION[LAYERS] = vec4[LAYERS](vec4(0.35, -0.45, -0.30, 0.55), vec4(0.95, 0.30, 0.35, 0.20),
                                         vec4(0.60, 0.55, -0.10, 0.85), vec4(0.20, -0.20, 0.20, 0.35));

vec2  gImpact, gMin, gMax;
float gS, gAngular;

float fastAtan2(float y, float x) {
  float ax = abs(x), ay = abs(y);
  float a  = min(ax, ay) / max(max(ax, ay), 1e-6);
  float s  = a * a;
  float r  = ((-0.0464965 * s + 0.15931422) * s - 0.32762276) * s * a + a;
  r        = ay > ax ? 1.5707964 - r : r;
  r        = x < 0.0 ? 3.1415927 - r : r;
  return y < 0.0 ? -r : r;
}

float sinApprox(float x) { return x - x * x * x / 6.0; } // |x| < 0.7 here

// Jittered angle of radial crack i, in crack units
float crackAngle(float i) { return i + 0.35 * (hash11(mod(i, gAngular) + uSeed * 13.0) - 0.5); }

// Radii where ring j crosses radial cracks i and i + 1
vec2 ringRadii(float j, float i) {
  float base = 22.0 * gS * (exp(j * 0.62) - 1.0);
  vec2  h    = vec2(hash11(mod(i, gAngular) * 7.31 + j * 13.7 + uSeed * 7.0),
                    hash11(mod(i + 1.0, gAngular) * 7.31 + j * 13.7 + uSeed * 7.0));
  return base * (0.8 + 0.4 * h);
}

// Radius at angle a on the chord between cracks a0, a1
float chord(vec2 rr, float a0, float a1, float a) {
  return rr.x * rr.y * sinApprox(a1 - a0) / max(rr.x * sinApprox(a - a0) + rr.y * sinApprox(a1 - a), 1e-4);
}

// Spider web: radial cracks + chords; xy id, z edge distance px
vec3 shardAt(vec2 p) {
  vec2  d    = p - gImpact;
  float r    = length(d);
  float unit = 6.28318 / gAngular;
  float a    = fastAtan2(d.y, d.x) / unit;

  float i  = floor(a);
  float lo = crackAngle(i), hi = crackAngle(i + 1.0);
  if (a < lo) {
    i -= 1.0;
    hi = lo;
    lo = crackAngle(i);
  } else if (a >= hi) {
    i += 1.0;
    lo = hi;
    hi = crackAngle(i + 1.0);
  }
  float a0 = lo * unit, a1 = hi * unit, ar = a * unit;

  // rings are roughly geometric; correct the estimate against the chords
  float j  = floor(log(r / (22.0 * gS) + 1.0) / 0.62);
  float rj = chord(ringRadii(j, i), a0, a1, ar);
  float rn = chord(ringRadii(j + 1.0, i), a0, a1, ar);
  if (r < rj) {
    j -= 1.0;
    rn = rj;
    rj = chord(ringRadii(j, i), a0, a1, ar);
  } else if (r >= rn) {
    j += 1.0;
    rj = rn;
    rn = chord(ringRadii(j + 1.0, i), a0, a1, ar);
  }

  vec2  id   = vec2(mod(i, gAngular), j);
  float edge = min(min(r * (ar - a0), r * (a1 - ar)), min(r - rj, rn - r));
  if (hash12(id + uSeed * 3.0) < 0.6) {
    float diag = (ar - a0) / (a1 - a0) - (r - rj) / (rn - rj);
    id += diag > 0.0 ? 0.5 : 0.0;
    edge = min(edge, abs(diag) * 0.6 * min(rn - rj, r * (a1 - a0)));
  }
  return vec3(id, edge);
}

bool inWindow(vec2 p) { return all(greaterThanEqual(p, gMin)) && all(lessThanEqual(p, gMax)); }

int layerOf(vec2 id) { return int(hash12(id + uSeed * 5.0) * float(LAYERS)); }

void main() {
  float t = uForOpening ? 1.0 - uProgress : uProgress;

  gS       = uMonitorScale;
  gAngular = floor(uShards);
  gMin     = vec2(uPadding);
  gMax     = uSize - uPadding;
  gImpact  = mix(gMin, gMax, 0.3 + 0.4 * hash22(vec2(uSeed * 17.3, uSeed * 3.9)));

  vec2  px    = iTexCoord * uSize;
  float crack = clamp(t / CRACK_END, 0.0, 1.0);
  float fall  = clamp((t - CRACK_END) / (1.0 - CRACK_END), 0.0, 1.0);
  vec4  color = vec4(0.0); // premultiplied

  if (fall <= 0.0) {
    // cracks run out from the impact
    color = getInputColor(iTexCoord);
    if (color.a > 0.0 && t > 0.0) {
      vec3  shard = shardAt(px);
      float reach = easeOutQuad(crack) * length(gMax - gMin);
      float line  = (1.0 - smoothstep(0.6 * gS, 1.8 * gS, shard.z)) * step(length(px - gImpact), reach);
      float flash = (1.0 - crack) * exp(-length(px - gImpact) / (18.0 * gS));
      color.rgb   = mix(color.rgb, uColor.rgb, clamp(line + flash, 0.0, 1.0) * uColor.a);
    }
    color.rgb *= color.a;
  } else {
    float burst = uBlow * easeOutQuad(fall);
    // top layer first; shards rarely overlap
    for (int i = LAYERS - 1; i >= 0; i--) {
      vec4 m = MOTION[i];
      m.yzw  = fract(m.yzw + uSeed * vec3(0.37, 0.61, 0.23) + 0.5) - 0.5; // vary per close

      float blow  = (0.25 + 0.75 * m.x) * burst;
      float spin  = m.y * 0.7 * fall;
      vec2  shift = gS * vec2(m.z * 520.0 * fall, uGravity * (fall * fall * 520.0 - (m.w + 0.5) * 220.0 * fall));

      // undo burst, spin and shift; small-angle sin/cos
      vec2 q = px - gImpact - shift;
      vec2 cs = vec2(1.0 - 0.5 * spin * spin, spin - spin * spin * spin / 6.0);
      vec2 p  = gImpact + vec2(q.x * cs.x + q.y * cs.y, q.y * cs.x - q.x * cs.y) / (1.0 + blow);
      if (!inWindow(p)) {
        continue;
      }

      vec3 shard = shardAt(p);
      if (layerOf(shard.xy) != i) {
        continue;
      }

      // rims catch the light, faces glint as they turn
      vec4  c     = getInputColor(p / uSize);
      float rim   = (1.0 - smoothstep(0.0, 4.0 * gS, shard.z)) * 0.35;
      float glint = pow(max(0.0, sin(fall * 6.0 + m.x * 6.3)), 6.0) * 0.25;
      c.rgb       = mix(c.rgb, uColor.rgb, clamp((rim + glint) * fall, 0.0, 1.0) * uColor.a);
      c.a        *= (1.0 - smoothstep(0.5, 1.0, fall)) * smoothstep(0.0, gS, shard.z + fall * 2.0 * gS);

      color = vec4(c.rgb * c.a, c.a);
      break;
    }

    vec2 box = min(px, uSize - px);
    color *= smoothstep(0.0, 50.0 * gS, min(box.x, box.y)); // fade, don't clip, at the box
  }

  setOutputColor(color.a > 0.0 ? vec4(color.rgb / color.a, color.a) : vec4(0.0));
}
