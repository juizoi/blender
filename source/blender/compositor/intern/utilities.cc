/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BLI_assert.h"
#include "BLI_math_vector.hh"
#include "BLI_math_vector_types.hh"

#include "DNA_node_types.h"

#include "NOD_derived_node_tree.hh"
#include "NOD_node_declaration.hh"

#include "GPU_compute.hh"
#include "GPU_shader.hh"

#include "COM_result.hh"
#include "COM_utilities.hh"

namespace blender::compositor {

using namespace nodes::derived_node_tree_types;
using TargetSocketPathInfo = DOutputSocket::TargetSocketPathInfo;

DSocket get_input_origin_socket(DInputSocket input)
{
  /* The input is unlinked. Return the socket itself. */
  if (!input->is_logically_linked()) {
    return input;
  }

  /* Only a single origin socket is guaranteed to exist. */
  DSocket socket;
  input.foreach_origin_socket([&](const DSocket origin) { socket = origin; });
  return socket;
}

DOutputSocket get_output_linked_to_input(DInputSocket input)
{
  /* Get the origin socket of this input, which will be an output socket if the input is linked
   * to an output. */
  const DSocket origin = get_input_origin_socket(input);

  /* If the origin socket is an input, that means the input is unlinked, so return a null output
   * socket. */
  if (origin->is_input()) {
    return DOutputSocket();
  }

  /* Now that we know the origin is an output, return a derived output from it. */
  return DOutputSocket(origin);
}

ResultType get_node_socket_result_type(const bNodeSocket *socket)
{
  switch (socket->type) {
    case SOCK_FLOAT:
      return ResultType::Float;
    case SOCK_INT:
      return ResultType::Int;
    case SOCK_VECTOR:
      return ResultType::Vector;
    case SOCK_RGBA:
      return ResultType::Color;
    default:
      BLI_assert_unreachable();
      return ResultType::Float;
  }
}

bool is_output_linked_to_node_conditioned(DOutputSocket output, FunctionRef<bool(DNode)> condition)
{
  bool condition_satisfied = false;
  output.foreach_target_socket(
      [&](DInputSocket target, const TargetSocketPathInfo & /*path_info*/) {
        if (condition(target.node())) {
          condition_satisfied = true;
          return;
        }
      });
  return condition_satisfied;
}

int number_of_inputs_linked_to_output_conditioned(DOutputSocket output,
                                                  FunctionRef<bool(DInputSocket)> condition)
{
  if (!output->is_logically_linked()) {
    return 0;
  }

  int count = 0;
  output.foreach_target_socket(
      [&](DInputSocket target, const TargetSocketPathInfo & /*path_info*/) {
        if (condition(target)) {
          count++;
        }
      });
  return count;
}

bool is_pixel_node(DNode node)
{
  return node->typeinfo->get_compositor_shader_node;
}

InputDescriptor input_descriptor_from_input_socket(const bNodeSocket *socket)
{
  using namespace nodes;
  InputDescriptor input_descriptor;
  input_descriptor.type = get_node_socket_result_type(socket);
  const NodeDeclaration *node_declaration = socket->owner_node().declaration();
  /* Not every node has a declaration, in which case we assume the default values for the rest of
   * the properties. */
  if (!node_declaration) {
    return input_descriptor;
  }
  const SocketDeclaration *socket_declaration = node_declaration->inputs[socket->index()];
  input_descriptor.domain_priority = socket_declaration->compositor_domain_priority();
  input_descriptor.expects_single_value = socket_declaration->compositor_expects_single_value();
  input_descriptor.realization_mode = static_cast<InputRealizationMode>(
      socket_declaration->compositor_realization_mode());

  return input_descriptor;
}

void compute_dispatch_threads_at_least(GPUShader *shader, int2 threads_range, int2 local_size)
{
  /* If the threads range is divisible by the local size, dispatch the number of needed groups,
   * which is their division. If it is not divisible, then dispatch an extra group to cover the
   * remaining invocations, which means the actual threads range of the dispatch will be a bit
   * larger than the given one. */
  const int2 groups_to_dispatch = math::divide_ceil(threads_range, local_size);
  GPU_compute_dispatch(shader, groups_to_dispatch.x, groups_to_dispatch.y, 1);
}

bool is_node_preview_needed(const DNode &node)
{
  if (!(node->flag & NODE_PREVIEW)) {
    return false;
  }

  if (node->flag & NODE_HIDDEN) {
    return false;
  }

  /* Only compute previews for nodes in the active context. */
  if (node.context()->instance_key().value !=
      node.context()->derived_tree().active_context().instance_key().value)
  {
    return false;
  }

  return true;
}

DOutputSocket find_preview_output_socket(const DNode &node)
{
  if (!is_node_preview_needed(node)) {
    return DOutputSocket();
  }

  for (const bNodeSocket *output : node->output_sockets()) {
    if (output->is_logically_linked()) {
      return DOutputSocket(node.context(), output);
    }
  }

  return DOutputSocket();
}

}  // namespace blender::compositor
