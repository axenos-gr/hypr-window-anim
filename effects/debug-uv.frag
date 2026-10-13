// SPDX-License-Identifier: GPL-3.0-or-later
// Orientation check: red grows to the right, green grows downwards.

void main() {
  vec4 color = getInputColor(iTexCoord);

  color.rgb = mix(color.rgb, vec3(iTexCoord, 0.0), 0.6);
  color.a   = max(color.a, 0.6) * (1.0 - uProgress);

  setOutputColor(color);
}
