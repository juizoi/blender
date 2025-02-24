/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/overlay_edit_mode_info.hh"

VERTEX_SHADER_CREATE_INFO(overlay_edit_curve_point)

#include "draw_model_lib.glsl"
#include "draw_view_clipping_lib.glsl"
#include "draw_view_lib.glsl"

void main()
{
  /* Reuse the FREESTYLE flag to determine is GPencil. */
  bool is_gpencil = ((data & EDGE_FREESTYLE) != 0u);
  if ((data & VERT_SELECTED) != 0u) {
    if ((data & VERT_ACTIVE) != 0u) {
      finalColor = colorEditMeshActive;
    }
    else {
      finalColor = (!is_gpencil) ? colorVertexSelect : colorGpencilVertexSelect;
    }
  }
  else {
    finalColor = (!is_gpencil) ? colorVertex : colorGpencilVertex;
  }

  vec3 world_pos = drw_point_object_to_world(pos);
  gl_Position = drw_point_world_to_homogenous(world_pos);
  gl_PointSize = (!is_gpencil) ? sizeVertex * 2.0 : sizeVertexGpencil * 2.0;
  view_clipping_distances(world_pos);

  bool show_handle = showCurveHandles;
  if ((uint(curveHandleDisplay) == CURVE_HANDLE_SELECTED) &&
      ((data & VERT_SELECTED_BEZT_HANDLE) == 0u))
  {
    show_handle = false;
  }

  if (!show_handle && ((data & BEZIER_HANDLE) != 0u)) {
    /* We set the vertex at the camera origin to generate 0 fragments. */
    gl_Position = vec4(0.0, 0.0, -3e36, 0.0);
  }
}
