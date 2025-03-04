/* SPDX-FileCopyrightText: 2020 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup editors
 */

#pragma once

#include <string>

#include "BLI_generic_pointer.hh"
#include "BLI_string_ref.hh"

#include "DNA_customdata_types.h"

#include "BKE_screen.hh"

struct ReportList;
struct PointerRNA;
struct PropertyRNA;
namespace blender::bke {
enum class AttrDomain : int8_t;
class MutableAttributeAccessor;
}  // namespace blender::bke
namespace blender::nodes::geo_eval_log {
class GeoModifierLog;
}

namespace blender::ed::geometry {

/* -------------------------------------------------------------------- */
/** \name Attribute Value RNA Property Helpers
 *
 * Functions to make it easier to register RNA properties for the various attribute types and
 * retrieve/set their values.
 * \{ */

StringRefNull rna_property_name_for_type(eCustomDataType type);
PropertyRNA *rna_property_for_type(PointerRNA &ptr, const eCustomDataType type);
void register_rna_properties_for_attribute_types(StructRNA &srna);
GPointer rna_property_for_attribute_type_retrieve_value(PointerRNA &ptr,
                                                        const eCustomDataType type,
                                                        void *buffer);
void rna_property_for_attribute_type_set_value(PointerRNA &ptr, PropertyRNA &prop, GPointer value);
bool attribute_set_poll(bContext &C, const ID &object_data);

/** \} */

void operatortypes_geometry();

/**
 * Convert an attribute with the given name to a new type and domain.
 * The attribute must already exist.
 *
 * \note Does not support meshes in edit mode.
 */
bool convert_attribute(bke::MutableAttributeAccessor attributes,
                       StringRef name,
                       bke::AttrDomain dst_domain,
                       eCustomDataType dst_type,
                       ReportList *reports);

struct GeoOperatorLog {
  std::string node_group_name;
  std::unique_ptr<nodes::geo_eval_log::GeoModifierLog> log;

  GeoOperatorLog() = default;
  ~GeoOperatorLog();
};

const GeoOperatorLog &node_group_operator_static_eval_log();

MenuType node_group_operator_assets_menu();
MenuType node_group_operator_assets_menu_unassigned();

void clear_operator_asset_trees();

void ui_template_node_operator_asset_menu_items(uiLayout &layout,
                                                const bContext &C,
                                                StringRef catalog_path);
void ui_template_node_operator_asset_root_items(uiLayout &layout, const bContext &C);

}  // namespace blender::ed::geometry
