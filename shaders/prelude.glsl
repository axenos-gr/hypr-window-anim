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

#define iTexCoord v_texcoord

vec4 getInputColor(vec2 coords) {
  vec2 uv = uEffectRect.xy + coords * uEffectRect.zw;

  if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
    return vec4(0.0);
  }

  vec4 color = texture(uTexture, uv);

  if (color.a > 0.0) {
    color.rgb /= color.a;
  }

  return color;
}

void setOutputColor(vec4 outColor) {
  fragColor      = vec4(outColor.rgb * outColor.a, outColor.a);
  hwaMirrorColor = fragColor;
}
