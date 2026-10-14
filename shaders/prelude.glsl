// SPDX-License-Identifier: GPL-3.0-or-later
// Hyprland platform block: same contract as Burn-My-Windows common.glsl.

in vec2 v_texcoord;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 hwaMirrorColor; // unmodified-copy target, ignored when unbound

uniform bool  uForOpening;
uniform bool  uIsFullscreen;
uniform float uProgress;
uniform float uDuration;

uniform sampler2D uTexture;
uniform vec2      uSize;
uniform float     uPadding;
uniform vec4      uEffectRect; // effect box inside uTexture (xy offset, zw size)
uniform float     uTime;
uniform float     uMonitorScale;

// Window blur, drawn behind it like Hyprland does: effect ends match the screen
uniform sampler2D hwaBlurTex;   // blurred background, same space as uTexture
uniform int       hwaBlur;
uniform vec4      hwaBlurRect;  // window surface in the effect box, px
uniform vec2      hwaBlurRound; // radius px, rounding power
uniform float     hwaBlurAlpha;

#define iTexCoord v_texcoord

// Hyprland's rounding.glsl, as coverage
float hwaBlurMask(vec2 px) {
  vec2 hs = hwaBlurRect.zw * 0.5;
  vec2 q  = abs(px - hwaBlurRect.xy - hs);
  if (any(greaterThan(q, hs))) {
    return 0.0;
  }

  float r = hwaBlurRound.x;
  q -= hs - r;
  if (r <= 0.0 || q.x + q.y <= r) {
    return 1.0;
  }

  const float SMOOTH = 3.1415926535 / 5.34665792551;
  q                  = max(q, 0.0);
  float d            = pow(pow(q.x, hwaBlurRound.y) + pow(q.y, hwaBlurRound.y), 1.0 / hwaBlurRound.y);
  return 1.0 - smoothstep(0.0, 1.0, (d - r + SMOOTH) / (SMOOTH * 2.0));
}

vec4 getInputColor(vec2 coords) {
  vec2 uv = uEffectRect.xy + coords * uEffectRect.zw;

  if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
    return vec4(0.0);
  }

  vec4 color = texture(uTexture, uv);

  if (hwaBlur == 1) {
    float m = hwaBlurMask(coords * uSize) * hwaBlurAlpha;
    color += texture(hwaBlurTex, uv) * m * (1.0 - color.a); // premultiplied: window over blur
  }

  if (color.a > 0.0) {
    color.rgb /= color.a;
  }

  return color;
}

void setOutputColor(vec4 outColor) {
  fragColor      = vec4(outColor.rgb * outColor.a, outColor.a);
  hwaMirrorColor = fragColor;
}
