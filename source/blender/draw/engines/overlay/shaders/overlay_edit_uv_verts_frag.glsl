/* SPDX-FileCopyrightText: 2016-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/overlay_edit_mode_info.hh"

FRAGMENT_SHADER_CREATE_INFO(overlay_edit_uv_verts)

void main()
{
  float dist = length(gl_PointCoord - vec2(0.5));

  /* transparent outside of point
   * --- 0 ---
   * smooth transition
   * --- 1 ---
   * pure outline color
   * --- 2 ---
   * smooth transition
   * --- 3 ---
   * pure fill color
   * ...
   * dist = 0 at center of point */

  float midStroke = 0.5 * (radii[1] + radii[2]);

  if (dist > midStroke) {
    fragColor.rgb = outlineColor.rgb;
    fragColor.a = mix(outlineColor.a, 0.0, smoothstep(radii[1], radii[0], dist));
  }
  else {
    fragColor = mix(fillColor, outlineColor, smoothstep(radii[3], radii[2], dist));
  }
}
