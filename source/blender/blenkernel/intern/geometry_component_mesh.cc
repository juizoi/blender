/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BLI_listbase.h"
#include "BLI_task.hh"

#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"

#include "BKE_attribute_math.hh"
#include "BKE_deform.hh"
#include "BKE_geometry_fields.hh"
#include "BKE_geometry_set.hh"
#include "BKE_lib_id.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_mapping.hh"

#include "FN_multi_function_builder.hh"

#include "attribute_access_intern.hh"

namespace blender::bke {

/* -------------------------------------------------------------------- */
/** \name Geometry Component Implementation
 * \{ */

MeshComponent::MeshComponent() : GeometryComponent(Type::Mesh) {}

MeshComponent::MeshComponent(Mesh *mesh, GeometryOwnershipType ownership)
    : GeometryComponent(Type::Mesh), mesh_(mesh), ownership_(ownership)
{
}

MeshComponent::~MeshComponent()
{
  this->clear();
}

GeometryComponentPtr MeshComponent::copy() const
{
  MeshComponent *new_component = new MeshComponent();
  if (mesh_ != nullptr) {
    new_component->mesh_ = BKE_mesh_copy_for_eval(*mesh_);
    new_component->ownership_ = GeometryOwnershipType::Owned;
  }
  return GeometryComponentPtr(new_component);
}

void MeshComponent::clear()
{
  BLI_assert(this->is_mutable() || this->is_expired());
  if (mesh_ != nullptr) {
    if (ownership_ == GeometryOwnershipType::Owned) {
      BKE_id_free(nullptr, mesh_);
    }
    mesh_ = nullptr;
  }
}

bool MeshComponent::has_mesh() const
{
  return mesh_ != nullptr;
}

void MeshComponent::replace(Mesh *mesh, GeometryOwnershipType ownership)
{
  BLI_assert(this->is_mutable());
  this->clear();
  mesh_ = mesh;
  ownership_ = ownership;
}

Mesh *MeshComponent::release()
{
  BLI_assert(this->is_mutable());
  Mesh *mesh = mesh_;
  mesh_ = nullptr;
  return mesh;
}

const Mesh *MeshComponent::get() const
{
  return mesh_;
}

Mesh *MeshComponent::get_for_write()
{
  BLI_assert(this->is_mutable());
  if (ownership_ == GeometryOwnershipType::ReadOnly) {
    mesh_ = BKE_mesh_copy_for_eval(*mesh_);
    ownership_ = GeometryOwnershipType::Owned;
  }
  return mesh_;
}

bool MeshComponent::is_empty() const
{
  return mesh_ == nullptr;
}

bool MeshComponent::owns_direct_data() const
{
  return ownership_ == GeometryOwnershipType::Owned;
}

void MeshComponent::ensure_owns_direct_data()
{
  BLI_assert(this->is_mutable());
  if (ownership_ != GeometryOwnershipType::Owned) {
    if (mesh_) {
      mesh_ = BKE_mesh_copy_for_eval(*mesh_);
    }
    ownership_ = GeometryOwnershipType::Owned;
  }
}

void MeshComponent::count_memory(MemoryCounter &memory) const
{
  if (mesh_) {
    mesh_->count_memory(memory);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Mesh Normals Field Input
 * \{ */

VArray<float3> mesh_normals_varray(const Mesh &mesh,
                                   const IndexMask &mask,
                                   const AttrDomain domain)
{
  switch (domain) {
    case AttrDomain::Face: {
      return VArray<float3>::ForSpan(mesh.face_normals());
    }
    case AttrDomain::Point: {
      return VArray<float3>::ForSpan(mesh.vert_normals());
    }
    case AttrDomain::Edge: {
      /* In this case, start with vertex normals and convert to the edge domain, since the
       * conversion from edges to vertices is very simple. Use "manual" domain interpolation
       * instead of the GeometryComponent API to avoid calculating unnecessary values and to
       * allow normalizing the result more simply. */
      Span<float3> vert_normals = mesh.vert_normals();
      const Span<int2> edges = mesh.edges();
      Array<float3> edge_normals(mask.min_array_size());
      mask.foreach_index([&](const int i) {
        const int2 &edge = edges[i];
        edge_normals[i] = math::normalize(
            math::interpolate(vert_normals[edge[0]], vert_normals[edge[1]], 0.5f));
      });

      return VArray<float3>::ForContainer(std::move(edge_normals));
    }
    case AttrDomain::Corner: {
      /* The normals on corners are just the mesh's face normals, so start with the face normal
       * array and copy the face normal for each of its corners. In this case using the mesh
       * component's generic domain interpolation is fine, the data will still be normalized,
       * since the face normal is just copied to every corner. */
      return mesh.attributes().adapt_domain(
          VArray<float3>::ForSpan(mesh.face_normals()), AttrDomain::Face, AttrDomain::Corner);
    }
    default:
      return {};
  }
}

/** \} */

}  // namespace blender::bke

namespace blender::bke {

/* -------------------------------------------------------------------- */
/** \name Attribute Access
 * \{ */

std::optional<AttributeAccessor> MeshComponent::attributes() const
{
  return AttributeAccessor(mesh_, mesh_attribute_accessor_functions());
}

std::optional<MutableAttributeAccessor> MeshComponent::attributes_for_write()
{
  Mesh *mesh = this->get_for_write();
  return MutableAttributeAccessor(mesh, mesh_attribute_accessor_functions());
}

/** \} */

}  // namespace blender::bke
