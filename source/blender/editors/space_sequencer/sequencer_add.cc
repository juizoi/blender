/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup spseq
 */

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_path_utils.hh"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BLT_translation.hh"

#include "DNA_scene_types.h"
#include "DNA_space_types.h"

#include "BKE_context.hh"
#include "BKE_global.hh"
#include "BKE_main.hh"
#include "BKE_report.hh"
#include "BKE_scene.hh"

#include "IMB_imbuf_enums.h"

#include "WM_api.hh"
#include "WM_types.hh"

#include "RNA_define.hh"
#include "RNA_enum_types.hh"
#include "RNA_prototypes.hh"

#include "SEQ_add.hh"
#include "SEQ_connect.hh"
#include "SEQ_effects.hh"
#include "SEQ_proxy.hh"
#include "SEQ_select.hh"
#include "SEQ_sequencer.hh"
#include "SEQ_time.hh"
#include "SEQ_transform.hh"

#include "ED_scene.hh"
/* For menu, popup, icons, etc. */
#include "ED_screen.hh"
#include "ED_sequencer.hh"

#include "UI_interface.hh"
#include "UI_view2d.hh"

#ifdef WITH_AUDASPACE
#  include <AUD_Sequence.h>
#endif

#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"

/* Own include. */
#include "sequencer_intern.hh"

namespace blender::ed::vse {

struct SequencerAddData {
  ImageFormatData im_format;
};

/* Generic functions, reused by add strip operators. */

/* Avoid passing multiple args and be more verbose. */
#define SEQPROP_STARTFRAME (1 << 0)
#define SEQPROP_ENDFRAME (1 << 1)
#define SEQPROP_NOPATHS (1 << 2)
#define SEQPROP_NOCHAN (1 << 3)
#define SEQPROP_FIT_METHOD (1 << 4)
#define SEQPROP_VIEW_TRANSFORM (1 << 5)
#define SEQPROP_PLAYBACK_RATE (1 << 6)

static const EnumPropertyItem scale_fit_methods[] = {
    {SEQ_SCALE_TO_FIT, "FIT", 0, "Scale to Fit", "Scale image to fit within the canvas"},
    {SEQ_SCALE_TO_FILL, "FILL", 0, "Scale to Fill", "Scale image to completely fill the canvas"},
    {SEQ_STRETCH_TO_FILL, "STRETCH", 0, "Stretch to Fill", "Stretch image to fill the canvas"},
    {SEQ_USE_ORIGINAL_SIZE, "ORIGINAL", 0, "Use Original Size", "Keep image at its original size"},
    {0, nullptr, 0, nullptr, nullptr},
};

static void sequencer_generic_props__internal(wmOperatorType *ot, int flag)
{
  PropertyRNA *prop;

  if (flag & SEQPROP_STARTFRAME) {
    RNA_def_int(ot->srna,
                "frame_start",
                0,
                INT_MIN,
                INT_MAX,
                "Start Frame",
                "Start frame of the sequence strip",
                -MAXFRAME,
                MAXFRAME);
  }

  if (flag & SEQPROP_ENDFRAME) {
    /* Not usual since most strips have a fixed length. */
    RNA_def_int(ot->srna,
                "frame_end",
                0,
                INT_MIN,
                INT_MAX,
                "End Frame",
                "End frame for the color strip",
                -MAXFRAME,
                MAXFRAME);
  }

  RNA_def_int(ot->srna,
              "channel",
              1,
              1,
              seq::MAX_CHANNELS,
              "Channel",
              "Channel to place this strip into",
              1,
              seq::MAX_CHANNELS);

  RNA_def_boolean(
      ot->srna, "replace_sel", true, "Replace Selection", "Deselect previously selected strips");

  /* Only for python scripts which import strips and place them after. */
  prop = RNA_def_boolean(
      ot->srna, "overlap", false, "Allow Overlap", "Don't correct overlap on new sequence strips");
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);

  prop = RNA_def_boolean(
      ot->srna,
      "overlap_shuffle_override",
      false,
      "Override Overlap Shuffle Behavior",
      "Use the overlap_mode tool settings to determine how to shuffle overlapping strips");
  RNA_def_property_flag(prop, PROP_HIDDEN | PROP_SKIP_SAVE);

  if (flag & SEQPROP_FIT_METHOD) {
    ot->prop = RNA_def_enum(ot->srna,
                            "fit_method",
                            scale_fit_methods,
                            SEQ_SCALE_TO_FIT,
                            "Fit Method",
                            "Scale fit method");
  }

  if (flag & SEQPROP_VIEW_TRANSFORM) {
    ot->prop = RNA_def_boolean(ot->srna,
                               "set_view_transform",
                               true,
                               "Set View Transform",
                               "Set appropriate view transform based on media color space");
  }

  if (flag & SEQPROP_PLAYBACK_RATE) {
    ot->prop = RNA_def_boolean(ot->srna,
                               "adjust_playback_rate",
                               true,
                               "Adjust Playback Rate",
                               "Play at normal speed regardless of scene FPS");
  }
}

static void sequencer_generic_invoke_path__internal(bContext *C,
                                                    wmOperator *op,
                                                    const char *identifier)
{
  if (RNA_struct_find_property(op->ptr, identifier)) {
    Scene *scene = CTX_data_scene(C);
    Strip *last_seq = seq::select_active_get(scene);
    if (last_seq && last_seq->data && STRIP_HAS_PATH(last_seq)) {
      Main *bmain = CTX_data_main(C);
      char dirpath[FILE_MAX];
      STRNCPY(dirpath, last_seq->data->dirpath);
      BLI_path_abs(dirpath, BKE_main_blendfile_path(bmain));
      RNA_string_set(op->ptr, identifier, dirpath);
    }
  }
}

static int sequencer_generic_invoke_xy_guess_channel(bContext *C, int type)
{
  Strip *tgt = nullptr;
  Scene *scene = CTX_data_scene(C);
  Editing *ed = seq::editing_ensure(scene);
  int timeline_frame = scene->r.cfra;
  int proximity = INT_MAX;

  if (!ed || !ed->seqbasep) {
    return 1;
  }

  LISTBASE_FOREACH (Strip *, strip, ed->seqbasep) {
    const int strip_end = seq::time_right_handle_frame_get(scene, strip);
    if (ELEM(type, -1, strip->type) && (strip_end <= timeline_frame) &&
        (timeline_frame - strip_end < proximity))
    {
      tgt = strip;
      proximity = timeline_frame - strip_end;
    }
  }

  if (tgt) {
    return (type == STRIP_TYPE_MOVIE) ? tgt->machine - 1 : tgt->machine;
  }
  return 1;
}

/* Sets `channel` and `frame_start` properties when the operator is likely to have been invoked
 * with drag-and-drop data. */
static void sequencer_file_drop_channel_frame_set(bContext *C,
                                                  wmOperator *op,
                                                  const wmEvent *event)
{
  BLI_assert((RNA_struct_property_is_set(op->ptr, "files") &&
              !RNA_collection_is_empty(op->ptr, "files")) ||
             RNA_struct_property_is_set(op->ptr, "filepath"));

  if (RNA_struct_property_is_set(op->ptr, "channel") ||
      RNA_struct_property_is_set(op->ptr, "frame_start"))
  {
    return;
  }

  ARegion *region = CTX_wm_region(C);
  if (!region || region->regiontype != RGN_TYPE_WINDOW) {
    return;
  }

  float frame_start, channel;
  UI_view2d_region_to_view(&region->v2d, event->mval[0], event->mval[1], &frame_start, &channel);
  RNA_int_set(op->ptr, "channel", int(channel));
  RNA_int_set(op->ptr, "frame_start", int(frame_start));
}

static void sequencer_generic_invoke_xy__internal(
    bContext *C, wmOperator *op, int flag, int type, const wmEvent *event = nullptr)
{
  Scene *scene = CTX_data_scene(C);

  int timeline_frame = scene->r.cfra;
  if ((flag & SEQPROP_NOPATHS) && event) {
    sequencer_file_drop_channel_frame_set(C, op, event);
  }

  /* Effect strips don't need a channel initialized from the mouse. */
  if (!(flag & SEQPROP_NOCHAN) && RNA_struct_property_is_set(op->ptr, "channel") == 0) {
    RNA_int_set(op->ptr, "channel", sequencer_generic_invoke_xy_guess_channel(C, type));
  }

  if (!RNA_struct_property_is_set(op->ptr, "frame_start")) {
    RNA_int_set(op->ptr, "frame_start", timeline_frame);
  }

  if ((flag & SEQPROP_ENDFRAME) && RNA_struct_property_is_set(op->ptr, "frame_end") == 0) {
    RNA_int_set(
        op->ptr, "frame_end", RNA_int_get(op->ptr, "frame_start") + DEFAULT_IMG_STRIP_LENGTH);
  }

  if (!(flag & SEQPROP_NOPATHS)) {
    sequencer_generic_invoke_path__internal(C, op, "filepath");
    sequencer_generic_invoke_path__internal(C, op, "directory");
  }
}

static bool load_data_init_from_operator(seq::LoadData *load_data, bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);

  PropertyRNA *prop;
  const bool relative = (prop = RNA_struct_find_property(op->ptr, "relative_path")) &&
                        RNA_property_boolean_get(op->ptr, prop);
  memset(load_data, 0, sizeof(seq::LoadData));

  load_data->start_frame = RNA_int_get(op->ptr, "frame_start");
  load_data->channel = RNA_int_get(op->ptr, "channel");
  load_data->image.end_frame = load_data->start_frame;
  load_data->image.len = 1;

  if ((prop = RNA_struct_find_property(op->ptr, "fit_method"))) {
    load_data->fit_method = eSeqImageFitMethod(RNA_enum_get(op->ptr, "fit_method"));
    seq::tool_settings_fit_method_set(CTX_data_scene(C), load_data->fit_method);
  }

  if ((prop = RNA_struct_find_property(op->ptr, "adjust_playback_rate"))) {
    load_data->adjust_playback_rate = RNA_boolean_get(op->ptr, "adjust_playback_rate");
  }

  if ((prop = RNA_struct_find_property(op->ptr, "filepath"))) {
    RNA_property_string_get(op->ptr, prop, load_data->path);
    /* File basename might be too long, better report it now than silently
     * truncating the basename later. */
    const char *basename = BLI_path_basename(load_data->path);
    if (strlen(basename) >= sizeof(StripElem::filename)) {
      BKE_reportf(op->reports,
                  RPT_ERROR,
                  "Filename '%s' too long (max length %zu, was %zu)",
                  basename,
                  sizeof(StripElem::filename),
                  strlen(basename));
      return false;
    }
    STRNCPY(load_data->name, basename);
  }
  else if ((prop = RNA_struct_find_property(op->ptr, "directory"))) {
    char *directory = RNA_string_get_alloc(op->ptr, "directory", nullptr, 0, nullptr);

    if ((prop = RNA_struct_find_property(op->ptr, "files"))) {
      RNA_PROP_BEGIN (op->ptr, itemptr, prop) {
        char *filename = RNA_string_get_alloc(&itemptr, "name", nullptr, 0, nullptr);
        STRNCPY(load_data->name, filename);
        BLI_path_join(load_data->path, sizeof(load_data->path), directory, filename);
        MEM_freeN(filename);
        break;
      }
      RNA_PROP_END;
    }
    MEM_freeN(directory);
  }

  if (relative) {
    BLI_path_rel(load_data->path, BKE_main_blendfile_path(bmain));
  }

  if ((prop = RNA_struct_find_property(op->ptr, "frame_end"))) {
    load_data->image.end_frame = RNA_property_int_get(op->ptr, prop);
    load_data->effect.end_frame = load_data->image.end_frame;
  }

  if ((prop = RNA_struct_find_property(op->ptr, "cache")) &&
      RNA_property_boolean_get(op->ptr, prop))
  {
    load_data->flags |= seq::SEQ_LOAD_SOUND_CACHE;
  }

  if ((prop = RNA_struct_find_property(op->ptr, "mono")) &&
      RNA_property_boolean_get(op->ptr, prop))
  {
    load_data->flags |= seq::SEQ_LOAD_SOUND_MONO;
  }

  if ((prop = RNA_struct_find_property(op->ptr, "use_framerate")) &&
      RNA_property_boolean_get(op->ptr, prop))
  {
    load_data->flags |= seq::SEQ_LOAD_MOVIE_SYNC_FPS;
  }

  if ((prop = RNA_struct_find_property(op->ptr, "set_view_transform")) &&
      RNA_property_boolean_get(op->ptr, prop))
  {
    load_data->flags |= seq::SEQ_LOAD_SET_VIEW_TRANSFORM;
  }

  if ((prop = RNA_struct_find_property(op->ptr, "use_multiview")) &&
      RNA_property_boolean_get(op->ptr, prop))
  {
    if (op->customdata) {
      SequencerAddData *sad = static_cast<SequencerAddData *>(op->customdata);
      ImageFormatData *imf = &sad->im_format;

      load_data->use_multiview = true;
      load_data->views_format = imf->views_format;
      load_data->stereo3d_format = &imf->stereo3d_format;
    }
  }
  return true;
}

static void seq_load_apply_generic_options(bContext *C, wmOperator *op, Strip *strip)
{
  Scene *scene = CTX_data_scene(C);
  Editing *ed = seq::editing_get(scene);

  if (strip == nullptr) {
    return;
  }

  if (RNA_boolean_get(op->ptr, "replace_sel")) {
    strip->flag |= SELECT;
    seq::select_active_set(scene, strip);
  }

  if (RNA_boolean_get(op->ptr, "overlap") == true ||
      !seq::transform_test_overlap(scene, ed->seqbasep, strip))
  {
    /* No overlap should be handled or the strip is not overlapping, exit early. */
    return;
  }

  if (RNA_boolean_get(op->ptr, "overlap_shuffle_override")) {
    /* Use set overlap_mode to fix overlaps. */
    blender::VectorSet<Strip *> strip_col;
    strip_col.add(strip);

    ScrArea *area = CTX_wm_area(C);
    const bool use_sync_markers = (((SpaceSeq *)area->spacedata.first)->flag & SEQ_MARKER_TRANS) !=
                                  0;
    seq::transform_handle_overlap(scene, ed->seqbasep, strip_col, use_sync_markers);
  }
  else {
    /* Shuffle strip channel to fix overlaps. */
    seq::transform_seqbase_shuffle(ed->seqbasep, strip, scene);
  }
}

/* In this alternative version we only check for overlap, but do not do anything about them. */
static bool seq_load_apply_generic_options_only_test_overlap(bContext *C,
                                                             wmOperator *op,
                                                             Strip *strip)
{
  Scene *scene = CTX_data_scene(C);
  Editing *ed = seq::editing_get(scene);

  if (strip == nullptr) {
    return false;
  }

  if (RNA_boolean_get(op->ptr, "replace_sel")) {
    strip->flag |= SELECT;
    seq::select_active_set(scene, strip);
  }

  return seq::transform_test_overlap(scene, ed->seqbasep, strip);
}

static bool seq_effect_add_properties_poll(const bContext * /*C*/,
                                           wmOperator *op,
                                           const PropertyRNA *prop)
{
  const char *prop_id = RNA_property_identifier(prop);
  int type = RNA_enum_get(op->ptr, "type");

  /* Hide start/end frames for effect strips that are locked to their parents' location. */
  if (seq::effect_get_num_inputs(type) != 0) {
    if (STR_ELEM(prop_id, "frame_start", "frame_end")) {
      return false;
    }
  }
  if ((type != STRIP_TYPE_COLOR) && STREQ(prop_id, "color")) {
    return false;
  }

  return true;
}

static wmOperatorStatus sequencer_add_scene_strip_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  const Editing *ed = seq::editing_ensure(scene);
  Scene *sce_seq = static_cast<Scene *>(
      BLI_findlink(&bmain->scenes, RNA_enum_get(op->ptr, "scene")));

  if (sce_seq == nullptr) {
    BKE_report(op->reports, RPT_ERROR, "Scene not found");
    return OPERATOR_CANCELLED;
  }

  if (RNA_boolean_get(op->ptr, "replace_sel")) {
    deselect_all_strips(scene);
  }

  seq::LoadData load_data;
  load_data_init_from_operator(&load_data, C, op);
  load_data.scene = sce_seq;

  Strip *strip = seq::add_scene_strip(scene, ed->seqbasep, &load_data);
  seq_load_apply_generic_options(C, op, strip);

  DEG_id_tag_update(&scene->id, ID_RECALC_SEQUENCER_STRIPS);
  DEG_relations_tag_update(bmain);
  sequencer_select_do_updates(C, scene);

  return OPERATOR_FINISHED;
}

static void sequencer_disable_one_time_properties(bContext *C, wmOperator *op)
{
  Editing *ed = seq::editing_get(CTX_data_scene(C));
  /* Disable following properties if there are any existing strips, unless overridden by user. */
  if (ed && ed->seqbasep && ed->seqbasep->first) {
    if (RNA_struct_find_property(op->ptr, "use_framerate")) {
      RNA_boolean_set(op->ptr, "use_framerate", false);
    }
    if (RNA_struct_find_property(op->ptr, "set_view_transform")) {
      RNA_boolean_set(op->ptr, "set_view_transform", false);
    }
  }
}

static wmOperatorStatus sequencer_add_scene_strip_invoke(bContext *C,
                                                         wmOperator *op,
                                                         const wmEvent *event)
{
  sequencer_disable_one_time_properties(C, op);
  if (!RNA_struct_property_is_set(op->ptr, "scene")) {
    return WM_enum_search_invoke(C, op, event);
  }

  sequencer_generic_invoke_xy__internal(C, op, 0, STRIP_TYPE_SCENE);
  return sequencer_add_scene_strip_exec(C, op);
}

void SEQUENCER_OT_scene_strip_add(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* Identifiers. */
  ot->name = "Add Scene Strip";
  ot->idname = "SEQUENCER_OT_scene_strip_add";
  ot->description = "Add a strip to the sequencer using a Blender scene as a source";

  /* Api callbacks. */
  ot->invoke = sequencer_add_scene_strip_invoke;
  ot->exec = sequencer_add_scene_strip_exec;
  ot->poll = ED_operator_sequencer_active_editable;

  /* Flags. */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  sequencer_generic_props__internal(ot, SEQPROP_STARTFRAME);
  prop = RNA_def_enum(ot->srna, "scene", rna_enum_dummy_NULL_items, 0, "Scene", "");
  RNA_def_enum_funcs(prop, RNA_scene_without_active_itemf);
  RNA_def_property_flag(prop, PROP_ENUM_NO_TRANSLATE);
  ot->prop = prop;
}

static EnumPropertyItem strip_new_scene_items[] = {
    {SCE_COPY_NEW, "NEW", 0, "New", "Add new Strip with a new empty Scene with default settings"},
    {SCE_COPY_EMPTY,
     "EMPTY",
     0,
     "Copy Settings",
     "Add a new Strip, with an empty scene, and copy settings from the current scene"},
    {SCE_COPY_LINK_COLLECTION,
     "LINK_COPY",
     0,
     "Linked Copy",
     "Add a Strip and link in the collections from the current scene (shallow copy)"},
    {SCE_COPY_FULL,
     "FULL_COPY",
     0,
     "Full Copy",
     "Add a Strip and make a full copy of the current scene"},
    {0, nullptr, 0, nullptr, nullptr},
};

static wmOperatorStatus sequencer_add_scene_strip_new_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  const Editing *ed = seq::editing_ensure(scene);

  if (RNA_boolean_get(op->ptr, "replace_sel")) {
    deselect_all_strips(scene);
  }

  seq::LoadData load_data;
  load_data_init_from_operator(&load_data, C, op);

  int type = RNA_enum_get(op->ptr, "type");
  Scene *scene_new = ED_scene_sequencer_add(bmain, C, eSceneCopyMethod(type), false);
  if (scene_new == nullptr) {
    return OPERATOR_CANCELLED;
  }
  load_data.scene = scene_new;

  Strip *strip = seq::add_scene_strip(scene, ed->seqbasep, &load_data);
  seq_load_apply_generic_options(C, op, strip);

  DEG_id_tag_update(&scene->id, ID_RECALC_SEQUENCER_STRIPS);
  DEG_relations_tag_update(bmain);
  sequencer_select_do_updates(C, scene);

  return OPERATOR_FINISHED;
}

static wmOperatorStatus sequencer_add_scene_strip_new_invoke(bContext *C,
                                                             wmOperator *op,
                                                             const wmEvent * /*event*/)
{
  sequencer_disable_one_time_properties(C, op);
  sequencer_generic_invoke_xy__internal(C, op, 0, STRIP_TYPE_SCENE);
  return sequencer_add_scene_strip_new_exec(C, op);
}

static const EnumPropertyItem *strip_new_sequencer_enum_itemf(bContext *C,
                                                              PointerRNA * /*ptr*/,
                                                              PropertyRNA * /*prop*/,
                                                              bool *r_free)
{
  EnumPropertyItem *item = nullptr;
  int totitem = 0;
  uint item_index;

  item_index = RNA_enum_from_value(strip_new_scene_items, SCE_COPY_NEW);
  RNA_enum_item_add(&item, &totitem, &strip_new_scene_items[item_index]);

  bool has_scene_or_no_context = false;
  if (C == nullptr) {
    /* For documentation generation. */
    has_scene_or_no_context = true;
  }
  else {
    Scene *scene = CTX_data_scene(C);
    Strip *strip = seq::select_active_get(scene);
    if (strip && (strip->type == STRIP_TYPE_SCENE) && (strip->scene != nullptr)) {
      has_scene_or_no_context = true;
    }
  }

  if (has_scene_or_no_context) {
    int values[] = {SCE_COPY_EMPTY, SCE_COPY_LINK_COLLECTION, SCE_COPY_FULL};
    for (int i = 0; i < ARRAY_SIZE(values); i++) {
      item_index = RNA_enum_from_value(strip_new_scene_items, values[i]);
      RNA_enum_item_add(&item, &totitem, &strip_new_scene_items[item_index]);
    }
  }

  RNA_enum_item_end(&item, &totitem);
  *r_free = true;
  return item;
}

void SEQUENCER_OT_scene_strip_add_new(wmOperatorType *ot)
{
  /* Identifiers. */
  ot->name = "Add Strip with a new Scene";
  ot->idname = "SEQUENCER_OT_scene_strip_add_new";
  ot->description = "Create a new Strip and assign a new Scene as source";

  /* Api callbacks. */
  ot->invoke = sequencer_add_scene_strip_new_invoke;
  ot->exec = sequencer_add_scene_strip_new_exec;
  ot->poll = ED_operator_sequencer_active_editable;

  /* Flags. */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  sequencer_generic_props__internal(ot, SEQPROP_STARTFRAME);

  ot->prop = RNA_def_enum(ot->srna, "type", strip_new_scene_items, SCE_COPY_NEW, "Type", "");
  RNA_def_enum_funcs(ot->prop, strip_new_sequencer_enum_itemf);
  RNA_def_property_flag(ot->prop, PROP_ENUM_NO_TRANSLATE);
}

static wmOperatorStatus sequencer_add_movieclip_strip_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  const Editing *ed = seq::editing_ensure(scene);
  MovieClip *clip = static_cast<MovieClip *>(
      BLI_findlink(&bmain->movieclips, RNA_enum_get(op->ptr, "clip")));

  if (clip == nullptr) {
    BKE_report(op->reports, RPT_ERROR, "Movie clip not found");
    return OPERATOR_CANCELLED;
  }

  if (RNA_boolean_get(op->ptr, "replace_sel")) {
    deselect_all_strips(scene);
  }

  seq::LoadData load_data;
  if (!load_data_init_from_operator(&load_data, C, op)) {
    return OPERATOR_CANCELLED;
  }
  load_data.clip = clip;

  Strip *strip = seq::add_movieclip_strip(scene, ed->seqbasep, &load_data);
  seq_load_apply_generic_options(C, op, strip);

  DEG_id_tag_update(&scene->id, ID_RECALC_SEQUENCER_STRIPS);
  sequencer_select_do_updates(C, scene);

  return OPERATOR_FINISHED;
}

static wmOperatorStatus sequencer_add_movieclip_strip_invoke(bContext *C,
                                                             wmOperator *op,
                                                             const wmEvent *event)
{
  if (!RNA_struct_property_is_set(op->ptr, "clip")) {
    return WM_enum_search_invoke(C, op, event);
  }

  sequencer_generic_invoke_xy__internal(C, op, 0, STRIP_TYPE_MOVIECLIP);
  return sequencer_add_movieclip_strip_exec(C, op);
}

void SEQUENCER_OT_movieclip_strip_add(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* Identifiers. */
  ot->name = "Add MovieClip Strip";
  ot->idname = "SEQUENCER_OT_movieclip_strip_add";
  ot->description = "Add a movieclip strip to the sequencer";

  /* Api callbacks. */
  ot->invoke = sequencer_add_movieclip_strip_invoke;
  ot->exec = sequencer_add_movieclip_strip_exec;
  ot->poll = ED_operator_sequencer_active_editable;

  /* Flags. */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  sequencer_generic_props__internal(ot, SEQPROP_STARTFRAME);
  prop = RNA_def_enum(ot->srna, "clip", rna_enum_dummy_NULL_items, 0, "Clip", "");
  RNA_def_enum_funcs(prop, RNA_movieclip_itemf);
  RNA_def_property_translation_context(prop, BLT_I18NCONTEXT_ID_MOVIECLIP);
  RNA_def_property_flag(prop, PROP_ENUM_NO_TRANSLATE);
  ot->prop = prop;
}

static wmOperatorStatus sequencer_add_mask_strip_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  const Editing *ed = seq::editing_ensure(scene);
  Mask *mask = static_cast<Mask *>(BLI_findlink(&bmain->masks, RNA_enum_get(op->ptr, "mask")));

  if (mask == nullptr) {
    BKE_report(op->reports, RPT_ERROR, "Mask not found");
    return OPERATOR_CANCELLED;
  }

  if (RNA_boolean_get(op->ptr, "replace_sel")) {
    deselect_all_strips(scene);
  }

  seq::LoadData load_data;
  load_data_init_from_operator(&load_data, C, op);
  load_data.mask = mask;

  Strip *strip = seq::add_mask_strip(scene, ed->seqbasep, &load_data);
  seq_load_apply_generic_options(C, op, strip);

  DEG_id_tag_update(&scene->id, ID_RECALC_SEQUENCER_STRIPS);
  sequencer_select_do_updates(C, scene);

  return OPERATOR_FINISHED;
}

static wmOperatorStatus sequencer_add_mask_strip_invoke(bContext *C,
                                                        wmOperator *op,
                                                        const wmEvent *event)
{
  if (!RNA_struct_property_is_set(op->ptr, "mask")) {
    return WM_enum_search_invoke(C, op, event);
  }

  sequencer_generic_invoke_xy__internal(C, op, 0, STRIP_TYPE_MASK);
  return sequencer_add_mask_strip_exec(C, op);
}

void SEQUENCER_OT_mask_strip_add(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* Identifiers. */
  ot->name = "Add Mask Strip";
  ot->idname = "SEQUENCER_OT_mask_strip_add";
  ot->description = "Add a mask strip to the sequencer";

  /* Api callbacks. */
  ot->invoke = sequencer_add_mask_strip_invoke;
  ot->exec = sequencer_add_mask_strip_exec;
  ot->poll = ED_operator_sequencer_active_editable;

  /* Flags. */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  sequencer_generic_props__internal(ot, SEQPROP_STARTFRAME);
  prop = RNA_def_enum(ot->srna, "mask", rna_enum_dummy_NULL_items, 0, "Mask", "");
  RNA_def_enum_funcs(prop, RNA_mask_itemf);
  RNA_def_property_flag(prop, PROP_ENUM_NO_TRANSLATE);
  ot->prop = prop;
}

static void sequencer_add_init(bContext * /*C*/, wmOperator *op)
{
  op->customdata = MEM_callocN(sizeof(SequencerAddData), __func__);
}

static void sequencer_add_cancel(bContext * /*C*/, wmOperator *op)
{
  MEM_SAFE_FREE(op->customdata);
}

static bool sequencer_add_draw_check_fn(PointerRNA * /*ptr*/,
                                        PropertyRNA *prop,
                                        void * /*user_data*/)
{
  const char *prop_id = RNA_property_identifier(prop);

  return !STR_ELEM(prop_id, "filepath", "directory", "filename");
}

/* Strips are added in context of timeline which has different preview size than actual preview. We
 * must search for preview area. In most cases there will be only one preview area, but there can
 * be more with different preview sizes. */
static IMB_Proxy_Size seq_get_proxy_size_flags(bContext *C)
{
  bScreen *screen = CTX_wm_screen(C);
  IMB_Proxy_Size proxy_sizes = IMB_Proxy_Size(0);
  LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
    LISTBASE_FOREACH (SpaceLink *, sl, &area->spacedata) {
      switch (sl->spacetype) {
        case SPACE_SEQ: {
          SpaceSeq *sseq = (SpaceSeq *)sl;
          if (!ELEM(sseq->view, SEQ_VIEW_PREVIEW, SEQ_VIEW_SEQUENCE_PREVIEW)) {
            continue;
          }
          proxy_sizes |= IMB_Proxy_Size(seq::rendersize_to_proxysize(sseq->render_size));
        }
      }
    }
  }
  return proxy_sizes;
}

static void seq_build_proxy(bContext *C, blender::Span<Strip *> movie_strips)
{
  if (U.sequencer_proxy_setup != USER_SEQ_PROXY_SETUP_AUTOMATIC) {
    return;
  }

  wmJob *wm_job = seq::ED_seq_proxy_wm_job_get(C);
  seq::ProxyJob *pj = seq::ED_seq_proxy_job_get(C, wm_job);

  for (Strip *strip : movie_strips) {
    /* Enable and set proxy size. */
    seq::proxy_set(strip, true);
    strip->data->proxy->build_size_flags = seq_get_proxy_size_flags(C);
    strip->data->proxy->build_flags |= SEQ_PROXY_SKIP_EXISTING;
    seq::proxy_rebuild_context(
        pj->main, pj->depsgraph, pj->scene, strip, nullptr, &pj->queue, true);
  }

  if (!WM_jobs_is_running(wm_job)) {
    G.is_break = false;
    WM_jobs_start(CTX_wm_manager(C), wm_job);
  }
  ED_area_tag_redraw(CTX_wm_area(C));
}

static void sequencer_add_movie_sync_sound_strip(
    Main *bmain, Scene *scene, Strip *strip_movie, Strip *strip_sound, seq::LoadData *load_data)
{
  if (ELEM(nullptr, strip_movie, strip_sound)) {
    return;
  }

  /* Make sure that the sound strip start time relative to the movie is taken into account. */
  seq::add_sound_av_sync(bmain, scene, strip_sound, load_data);

  /* Ensure that the sound strip start/end matches the movie strip even if the actual
   * length and true position of the sound doesn't match up exactly.
   */
  seq::time_right_handle_frame_set(
      scene, strip_sound, seq::time_right_handle_frame_get(scene, strip_movie));
  seq::time_left_handle_frame_set(
      scene, strip_sound, seq::time_left_handle_frame_get(scene, strip_movie));
}

static void sequencer_add_movie_multiple_strips(bContext *C,
                                                wmOperator *op,
                                                seq::LoadData *load_data,
                                                blender::VectorSet<Strip *> &r_movie_strips)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  const Editing *ed = seq::editing_ensure(scene);
  bool overlap_shuffle_override = RNA_boolean_get(op->ptr, "overlap") == false &&
                                  RNA_boolean_get(op->ptr, "overlap_shuffle_override");
  bool has_seq_overlap = false;
  blender::Vector<Strip *> added_strips;

  RNA_BEGIN (op->ptr, itemptr, "files") {
    char dir_only[FILE_MAX];
    char file_only[FILE_MAX];
    RNA_string_get(op->ptr, "directory", dir_only);
    RNA_string_get(&itemptr, "name", file_only);
    BLI_path_join(load_data->path, sizeof(load_data->path), dir_only, file_only);
    STRNCPY(load_data->name, file_only);
    Strip *strip_movie = nullptr;
    Strip *strip_sound = nullptr;

    strip_movie = seq::add_movie_strip(bmain, scene, ed->seqbasep, load_data);

    if (strip_movie == nullptr) {
      BKE_reportf(op->reports, RPT_ERROR, "File '%s' could not be loaded", load_data->path);
    }
    else {
      if (RNA_boolean_get(op->ptr, "sound")) {
        strip_sound = seq::add_sound_strip(bmain, scene, ed->seqbasep, load_data);
        sequencer_add_movie_sync_sound_strip(bmain, scene, strip_movie, strip_sound, load_data);
        added_strips.append(strip_movie);

        if (strip_sound) {
          /* The video has sound, shift the video strip up a channel to make room for the sound
           * strip. */
          added_strips.append(strip_sound);
          seq::strip_channel_set(strip_movie, strip_movie->machine + 1);
        }
      }

      load_data->start_frame += seq::time_right_handle_frame_get(scene, strip_movie) -
                                seq::time_left_handle_frame_get(scene, strip_movie);
      if (overlap_shuffle_override) {
        has_seq_overlap |= seq_load_apply_generic_options_only_test_overlap(C, op, strip_sound);
        has_seq_overlap |= seq_load_apply_generic_options_only_test_overlap(C, op, strip_movie);
      }
      else {
        seq_load_apply_generic_options(C, op, strip_sound);
        seq_load_apply_generic_options(C, op, strip_movie);
      }

      if (U.sequencer_editor_flag & USER_SEQ_ED_CONNECT_STRIPS_BY_DEFAULT) {
        seq::connect(strip_movie, strip_sound);
      }

      r_movie_strips.add(strip_movie);
    }
  }
  RNA_END;

  if (overlap_shuffle_override) {
    if (has_seq_overlap) {
      ScrArea *area = CTX_wm_area(C);
      const bool use_sync_markers = (((SpaceSeq *)area->spacedata.first)->flag &
                                     SEQ_MARKER_TRANS) != 0;
      seq::transform_handle_overlap(scene, ed->seqbasep, added_strips, use_sync_markers);
    }
  }
}

static bool sequencer_add_movie_single_strip(bContext *C,
                                             wmOperator *op,
                                             seq::LoadData *load_data,
                                             blender::VectorSet<Strip *> &r_movie_strips)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  const Editing *ed = seq::editing_ensure(scene);

  Strip *strip_movie = nullptr;
  Strip *strip_sound = nullptr;
  blender::Vector<Strip *> added_strips;

  strip_movie = seq::add_movie_strip(bmain, scene, ed->seqbasep, load_data);

  if (strip_movie == nullptr) {
    BKE_reportf(op->reports, RPT_ERROR, "File '%s' could not be loaded", load_data->path);
    return false;
  }
  if (RNA_boolean_get(op->ptr, "sound")) {
    strip_sound = seq::add_sound_strip(bmain, scene, ed->seqbasep, load_data);
    sequencer_add_movie_sync_sound_strip(bmain, scene, strip_movie, strip_sound, load_data);
    added_strips.append(strip_movie);

    if (strip_sound) {
      added_strips.append(strip_sound);
      /* The video has sound, shift the video strip up a channel to make room for the sound
       * strip. */
      seq::strip_channel_set(strip_movie, strip_movie->machine + 1);
    }
  }

  bool overlap_shuffle_override = RNA_boolean_get(op->ptr, "overlap") == false &&
                                  RNA_boolean_get(op->ptr, "overlap_shuffle_override");
  if (overlap_shuffle_override) {
    bool has_seq_overlap = false;

    has_seq_overlap |= seq_load_apply_generic_options_only_test_overlap(C, op, strip_sound);
    has_seq_overlap |= seq_load_apply_generic_options_only_test_overlap(C, op, strip_movie);

    if (has_seq_overlap) {
      ScrArea *area = CTX_wm_area(C);
      const bool use_sync_markers = (((SpaceSeq *)area->spacedata.first)->flag &
                                     SEQ_MARKER_TRANS) != 0;
      seq::transform_handle_overlap(scene, ed->seqbasep, added_strips, use_sync_markers);
    }
  }
  else {
    seq_load_apply_generic_options(C, op, strip_sound);
    seq_load_apply_generic_options(C, op, strip_movie);
  }

  if (U.sequencer_editor_flag & USER_SEQ_ED_CONNECT_STRIPS_BY_DEFAULT) {
    seq::connect(strip_movie, strip_sound);
  }

  r_movie_strips.add(strip_movie);

  return true;
}

static wmOperatorStatus sequencer_add_movie_strip_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  seq::LoadData load_data;

  if (!load_data_init_from_operator(&load_data, C, op)) {
    return OPERATOR_CANCELLED;
  }

  if (RNA_boolean_get(op->ptr, "replace_sel")) {
    deselect_all_strips(scene);
  }

  blender::VectorSet<Strip *> movie_strips;
  const int tot_files = RNA_property_collection_length(op->ptr,
                                                       RNA_struct_find_property(op->ptr, "files"));

  char vt_old[64];
  STRNCPY(vt_old, scene->view_settings.view_transform);
  float fps_old = scene->r.frs_sec / scene->r.frs_sec_base;

  if (tot_files > 1) {
    sequencer_add_movie_multiple_strips(C, op, &load_data, movie_strips);
  }
  else {
    sequencer_add_movie_single_strip(C, op, &load_data, movie_strips);
  }

  if (!STREQ(vt_old, scene->view_settings.view_transform)) {
    BKE_reportf(op->reports,
                RPT_WARNING,
                "View transform was automatically converted from %s to %s",
                vt_old,
                scene->view_settings.view_transform);
  }

  if (fps_old != scene->r.frs_sec / scene->r.frs_sec_base) {
    BKE_reportf(op->reports,
                RPT_WARNING,
                "Scene frame rate was automatically converted from %.4g to %.4g",
                fps_old,
                scene->r.frs_sec / scene->r.frs_sec_base);
  }

  if (movie_strips.is_empty()) {
    sequencer_add_cancel(C, op);
    return OPERATOR_CANCELLED;
  }

  seq_build_proxy(C, movie_strips);
  DEG_relations_tag_update(bmain);
  DEG_id_tag_update(&scene->id, ID_RECALC_SEQUENCER_STRIPS);
  sequencer_select_do_updates(C, scene);

  /* Free custom data. */
  sequencer_add_cancel(C, op);

  return OPERATOR_FINISHED;
}

static wmOperatorStatus sequencer_add_movie_strip_invoke(bContext *C,
                                                         wmOperator *op,
                                                         const wmEvent *event)
{
  PropertyRNA *prop;
  Scene *scene = CTX_data_scene(C);

  sequencer_disable_one_time_properties(C, op);

  RNA_enum_set(op->ptr, "fit_method", seq::tool_settings_fit_method_get(scene));
  RNA_boolean_set(op->ptr, "adjust_playback_rate", true);

  /* This is for drag and drop. */
  if ((RNA_struct_property_is_set(op->ptr, "files") &&
       !RNA_collection_is_empty(op->ptr, "files")) ||
      RNA_struct_property_is_set(op->ptr, "filepath"))
  {
    sequencer_generic_invoke_xy__internal(C, op, SEQPROP_NOPATHS, STRIP_TYPE_MOVIE, event);
    return sequencer_add_movie_strip_exec(C, op);
  }

  sequencer_generic_invoke_xy__internal(C, op, 0, STRIP_TYPE_MOVIE);
  sequencer_add_init(C, op);

  /* Show multiview save options only if scene use multiview. */
  prop = RNA_struct_find_property(op->ptr, "show_multiview");
  RNA_property_boolean_set(op->ptr, prop, (scene->r.scemode & R_MULTIVIEW) != 0);

  WM_event_add_fileselect(C, op);
  return OPERATOR_RUNNING_MODAL;
}

static void sequencer_add_draw(bContext * /*C*/, wmOperator *op)
{
  uiLayout *layout = op->layout;
  SequencerAddData *sad = static_cast<SequencerAddData *>(op->customdata);
  ImageFormatData *imf = &sad->im_format;

  /* Main draw call. */
  uiDefAutoButsRNA(layout,
                   op->ptr,
                   sequencer_add_draw_check_fn,
                   nullptr,
                   nullptr,
                   UI_BUT_LABEL_ALIGN_NONE,
                   false);

  /* Image template. */
  PointerRNA imf_ptr = RNA_pointer_create_discrete(nullptr, &RNA_ImageFormatSettings, imf);

  /* Multiview template. */
  if (RNA_boolean_get(op->ptr, "show_multiview")) {
    uiTemplateImageFormatViews(layout, &imf_ptr, op->ptr);
  }
}

void SEQUENCER_OT_movie_strip_add(wmOperatorType *ot)
{

  /* Identifiers. */
  ot->name = "Add Movie Strip";
  ot->idname = "SEQUENCER_OT_movie_strip_add";
  ot->description = "Add a movie strip to the sequencer";

  /* Api callbacks. */
  ot->invoke = sequencer_add_movie_strip_invoke;
  ot->exec = sequencer_add_movie_strip_exec;
  ot->cancel = sequencer_add_cancel;
  ot->ui = sequencer_add_draw;
  ot->poll = ED_operator_sequencer_active_editable;

  /* Flags. */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_MOVIE,
                                 FILE_SPECIAL,
                                 FILE_OPENFILE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_RELPATH | WM_FILESEL_FILES |
                                     WM_FILESEL_SHOW_PROPS | WM_FILESEL_DIRECTORY,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);
  sequencer_generic_props__internal(ot,
                                    SEQPROP_STARTFRAME | SEQPROP_FIT_METHOD |
                                        SEQPROP_VIEW_TRANSFORM | SEQPROP_PLAYBACK_RATE);
  RNA_def_boolean(ot->srna, "sound", true, "Sound", "Load sound with the movie");
  RNA_def_boolean(ot->srna,
                  "use_framerate",
                  true,
                  "Set Scene Frame Rate",
                  "Set frame rate of the current scene to the frame rate of the movie");
}

static void sequencer_add_sound_multiple_strips(bContext *C,
                                                wmOperator *op,
                                                seq::LoadData *load_data)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  Editing *ed = seq::editing_ensure(scene);

  RNA_BEGIN (op->ptr, itemptr, "files") {
    char dir_only[FILE_MAX];
    char file_only[FILE_MAX];
    RNA_string_get(op->ptr, "directory", dir_only);
    RNA_string_get(&itemptr, "name", file_only);
    BLI_path_join(load_data->path, sizeof(load_data->path), dir_only, file_only);
    STRNCPY(load_data->name, file_only);
    Strip *strip = seq::add_sound_strip(bmain, scene, ed->seqbasep, load_data);
    if (strip == nullptr) {
      BKE_reportf(op->reports, RPT_ERROR, "File '%s' could not be loaded", load_data->path);
    }
    else {
      seq_load_apply_generic_options(C, op, strip);
      load_data->start_frame += seq::time_right_handle_frame_get(scene, strip) -
                                seq::time_left_handle_frame_get(scene, strip);
    }
  }
  RNA_END;
}

static bool sequencer_add_sound_single_strip(bContext *C, wmOperator *op, seq::LoadData *load_data)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  Editing *ed = seq::editing_ensure(scene);

  Strip *strip = seq::add_sound_strip(bmain, scene, ed->seqbasep, load_data);
  if (strip == nullptr) {
    BKE_reportf(op->reports, RPT_ERROR, "File '%s' could not be loaded", load_data->path);
    return false;
  }
  seq_load_apply_generic_options(C, op, strip);

  return true;
}

static wmOperatorStatus sequencer_add_sound_strip_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  seq::LoadData load_data;
  load_data_init_from_operator(&load_data, C, op);

  if (RNA_boolean_get(op->ptr, "replace_sel")) {
    deselect_all_strips(scene);
  }

  const int tot_files = RNA_property_collection_length(op->ptr,
                                                       RNA_struct_find_property(op->ptr, "files"));
  if (tot_files > 1) {
    sequencer_add_sound_multiple_strips(C, op, &load_data);
  }
  else {
    if (!sequencer_add_sound_single_strip(C, op, &load_data)) {
      return OPERATOR_CANCELLED;
    }
  }

  if (op->customdata) {
    MEM_freeN(op->customdata);
  }

  DEG_relations_tag_update(bmain);
  DEG_id_tag_update(&scene->id, ID_RECALC_SEQUENCER_STRIPS);
  sequencer_select_do_updates(C, scene);

  return OPERATOR_FINISHED;
}

static wmOperatorStatus sequencer_add_sound_strip_invoke(bContext *C,
                                                         wmOperator *op,
                                                         const wmEvent *event)
{
  /* This is for drag and drop. */
  if ((RNA_struct_property_is_set(op->ptr, "files") &&
       !RNA_collection_is_empty(op->ptr, "files")) ||
      RNA_struct_property_is_set(op->ptr, "filepath"))
  {
    sequencer_generic_invoke_xy__internal(C, op, SEQPROP_NOPATHS, STRIP_TYPE_SOUND_RAM, event);
    return sequencer_add_sound_strip_exec(C, op);
  }

  sequencer_generic_invoke_xy__internal(C, op, 0, STRIP_TYPE_SOUND_RAM);

  WM_event_add_fileselect(C, op);
  return OPERATOR_RUNNING_MODAL;
}

void SEQUENCER_OT_sound_strip_add(wmOperatorType *ot)
{

  /* Identifiers. */
  ot->name = "Add Sound Strip";
  ot->idname = "SEQUENCER_OT_sound_strip_add";
  ot->description = "Add a sound strip to the sequencer";

  /* Api callbacks. */
  ot->invoke = sequencer_add_sound_strip_invoke;
  ot->exec = sequencer_add_sound_strip_exec;
  ot->poll = ED_operator_sequencer_active_editable;

  /* Flags. */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_SOUND,
                                 FILE_SPECIAL,
                                 FILE_OPENFILE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_RELPATH | WM_FILESEL_FILES |
                                     WM_FILESEL_SHOW_PROPS | WM_FILESEL_DIRECTORY,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);
  sequencer_generic_props__internal(ot, SEQPROP_STARTFRAME);
  RNA_def_boolean(ot->srna, "cache", false, "Cache", "Cache the sound in memory");
  RNA_def_boolean(ot->srna, "mono", false, "Mono", "Merge all the sound's channels into one");
}

int sequencer_image_seq_get_minmax_frame(wmOperator *op,
                                         int sfra,
                                         int *r_minframe,
                                         int *r_numdigits)
{
  int minframe = INT32_MAX, maxframe = INT32_MIN;
  int numdigits = 0;

  RNA_BEGIN (op->ptr, itemptr, "files") {
    char *filename;
    int frame;
    filename = RNA_string_get_alloc(&itemptr, "name", nullptr, 0, nullptr);

    if (filename) {
      if (BLI_path_frame_get(filename, &frame, &numdigits)) {
        minframe = min_ii(minframe, frame);
        maxframe = max_ii(maxframe, frame);
      }

      MEM_freeN(filename);
    }
  }
  RNA_END;

  if (minframe == INT32_MAX) {
    minframe = sfra;
    maxframe = minframe + 1;
  }

  *r_minframe = minframe;
  *r_numdigits = numdigits;

  return maxframe - minframe + 1;
}

void sequencer_image_seq_reserve_frames(
    wmOperator *op, StripElem *se, int len, int minframe, int numdigits)
{
  char *filename = nullptr;
  RNA_BEGIN (op->ptr, itemptr, "files") {
    filename = RNA_string_get_alloc(&itemptr, "name", nullptr, 0, nullptr);
    break;
  }
  RNA_END;

  if (filename) {
    char ext[FILE_MAX];
    char filename_stripped[FILE_MAX];
    /* Strip the frame from filename and substitute with `#`. */
    BLI_path_frame_strip(filename, ext, sizeof(ext));

    for (int i = 0; i < len; i++, se++) {
      STRNCPY(filename_stripped, filename);
      BLI_path_frame(filename_stripped, sizeof(filename_stripped), minframe + i, numdigits);
      SNPRINTF(se->filename, "%s%s", filename_stripped, ext);
    }

    MEM_freeN(filename);
  }
}

static int sequencer_add_image_strip_calculate_length(wmOperator *op,
                                                      const int start_frame,
                                                      int *minframe,
                                                      int *numdigits)
{
  const bool use_placeholders = RNA_boolean_get(op->ptr, "use_placeholders");

  if (use_placeholders) {
    return sequencer_image_seq_get_minmax_frame(op, start_frame, minframe, numdigits);
  }
  return RNA_property_collection_length(op->ptr, RNA_struct_find_property(op->ptr, "files"));
}

static void sequencer_add_image_strip_load_files(wmOperator *op,
                                                 Scene *scene,
                                                 Strip *strip,
                                                 seq::LoadData *load_data,
                                                 const int minframe,
                                                 const int numdigits)
{
  const bool use_placeholders = RNA_boolean_get(op->ptr, "use_placeholders");
  char dirpath[sizeof(strip->data->dirpath)];
  BLI_path_split_dir_part(load_data->path, dirpath, sizeof(dirpath));
  seq::add_image_set_directory(strip, dirpath);

  if (use_placeholders) {
    sequencer_image_seq_reserve_frames(
        op, strip->data->stripdata, load_data->image.len, minframe, numdigits);
  }
  else {
    size_t strip_frame = 0;
    RNA_BEGIN (op->ptr, itemptr, "files") {
      char *filename = RNA_string_get_alloc(&itemptr, "name", nullptr, 0, nullptr);
      seq::add_image_load_file(scene, strip, strip_frame, filename);
      MEM_freeN(filename);
      strip_frame++;
    }
    RNA_END;
  }
}

static wmOperatorStatus sequencer_add_image_strip_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  Editing *ed = seq::editing_ensure(scene);

  seq::LoadData load_data;
  if (!load_data_init_from_operator(&load_data, C, op)) {
    return OPERATOR_CANCELLED;
  }

  int minframe, numdigits;
  load_data.image.len = sequencer_add_image_strip_calculate_length(
      op, load_data.start_frame, &minframe, &numdigits);
  if (load_data.image.len == 0) {
    sequencer_add_cancel(C, op);
    return OPERATOR_CANCELLED;
  }

  if (RNA_boolean_get(op->ptr, "replace_sel")) {
    deselect_all_strips(scene);
  }

  char vt_old[64];
  STRNCPY(vt_old, scene->view_settings.view_transform);

  Strip *strip = seq::add_image_strip(CTX_data_main(C), scene, ed->seqbasep, &load_data);

  if (!STREQ(vt_old, scene->view_settings.view_transform)) {
    BKE_reportf(op->reports,
                RPT_WARNING,
                "View transform was automatically converted from %s to %s",
                vt_old,
                scene->view_settings.view_transform);
  }

  sequencer_add_image_strip_load_files(op, scene, strip, &load_data, minframe, numdigits);
  seq::add_image_init_alpha_mode(strip);

  /* Adjust length. */
  if (load_data.image.len == 1) {
    seq::time_right_handle_frame_set(scene, strip, load_data.image.end_frame);
  }

  seq_load_apply_generic_options(C, op, strip);

  DEG_id_tag_update(&scene->id, ID_RECALC_SEQUENCER_STRIPS);
  sequencer_select_do_updates(C, scene);

  /* Free custom data. */
  sequencer_add_cancel(C, op);

  return OPERATOR_FINISHED;
}

static wmOperatorStatus sequencer_add_image_strip_invoke(bContext *C,
                                                         wmOperator *op,
                                                         const wmEvent *event)
{
  PropertyRNA *prop;
  Scene *scene = CTX_data_scene(C);

  sequencer_disable_one_time_properties(C, op);

  RNA_enum_set(op->ptr, "fit_method", seq::tool_settings_fit_method_get(scene));

  /* Name set already by drag and drop. */
  if (RNA_struct_property_is_set(op->ptr, "files") && !RNA_collection_is_empty(op->ptr, "files")) {
    sequencer_generic_invoke_xy__internal(
        C, op, SEQPROP_ENDFRAME | SEQPROP_NOPATHS, STRIP_TYPE_IMAGE, event);
    return sequencer_add_image_strip_exec(C, op);
  }

  sequencer_generic_invoke_xy__internal(C, op, SEQPROP_ENDFRAME, STRIP_TYPE_IMAGE);
  sequencer_add_init(C, op);

  /* Show multiview save options only if scene use multiview. */
  prop = RNA_struct_find_property(op->ptr, "show_multiview");
  RNA_property_boolean_set(op->ptr, prop, (scene->r.scemode & R_MULTIVIEW) != 0);

  WM_event_add_fileselect(C, op);
  return OPERATOR_RUNNING_MODAL;
}

void SEQUENCER_OT_image_strip_add(wmOperatorType *ot)
{

  /* Identifiers. */
  ot->name = "Add Image Strip";
  ot->idname = "SEQUENCER_OT_image_strip_add";
  ot->description = "Add an image or image sequence to the sequencer";

  /* Api callbacks. */
  ot->invoke = sequencer_add_image_strip_invoke;
  ot->exec = sequencer_add_image_strip_exec;
  ot->cancel = sequencer_add_cancel;
  ot->ui = sequencer_add_draw;
  ot->poll = ED_operator_sequencer_active_editable;

  /* Flags. */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  WM_operator_properties_filesel(
      ot,
      FILE_TYPE_FOLDER | FILE_TYPE_IMAGE,
      FILE_SPECIAL,
      FILE_OPENFILE,
      (WM_FILESEL_DIRECTORY | WM_FILESEL_RELPATH | WM_FILESEL_FILES | WM_FILESEL_SHOW_PROPS),
      FILE_DEFAULTDISPLAY,
      FILE_SORT_DEFAULT);
  sequencer_generic_props__internal(
      ot, SEQPROP_STARTFRAME | SEQPROP_ENDFRAME | SEQPROP_FIT_METHOD | SEQPROP_VIEW_TRANSFORM);

  RNA_def_boolean(ot->srna,
                  "use_placeholders",
                  false,
                  "Use Placeholders",
                  "Use placeholders for missing frames of the strip");
}

static wmOperatorStatus sequencer_add_effect_strip_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  Editing *ed = seq::editing_ensure(scene);

  seq::LoadData load_data;
  load_data_init_from_operator(&load_data, C, op);
  load_data.effect.type = RNA_enum_get(op->ptr, "type");
  const int num_inputs = seq::effect_get_num_inputs(load_data.effect.type);

  VectorSet<Strip *> inputs = strip_effect_get_new_inputs(scene);
  StringRef error_msg = effect_inputs_validate(inputs, num_inputs);

  if (!error_msg.is_empty()) {
    BKE_report(op->reports, RPT_ERROR, error_msg.data());
    return OPERATOR_CANCELLED;
  }

  if (RNA_boolean_get(op->ptr, "replace_sel")) {
    deselect_all_strips(scene);
  }

  Strip *seq1 = inputs.size() > 0 ? inputs[0] : nullptr;
  Strip *seq2 = inputs.size() == 2 ? inputs[1] : nullptr;

  load_data.effect.seq1 = seq1;
  load_data.effect.seq2 = seq2;

  /* Set channel. If unset, use lowest free one above strips. */
  if (!RNA_struct_property_is_set(op->ptr, "channel")) {
    if (seq1 != nullptr) {
      int chan = max_ii(seq1 ? seq1->machine : 0, seq2 ? seq2->machine : 0);
      if (chan < seq::MAX_CHANNELS) {
        load_data.channel = chan;
      }
    }
  }

  Strip *strip = seq::add_effect_strip(scene, ed->seqbasep, &load_data);
  seq_load_apply_generic_options(C, op, strip);

  if (strip->type == STRIP_TYPE_COLOR) {
    SolidColorVars *colvars = (SolidColorVars *)strip->effectdata;
    RNA_float_get_array(op->ptr, "color", colvars->col);
  }

  DEG_id_tag_update(&scene->id, ID_RECALC_SEQUENCER_STRIPS);
  sequencer_select_do_updates(C, scene);

  return OPERATOR_FINISHED;
}

static wmOperatorStatus sequencer_add_effect_strip_invoke(bContext *C,
                                                          wmOperator *op,
                                                          const wmEvent * /*event*/)
{
  bool is_type_set = RNA_struct_property_is_set(op->ptr, "type");
  int type = -1;
  int prop_flag = SEQPROP_ENDFRAME | SEQPROP_NOPATHS;

  if (is_type_set) {
    type = RNA_enum_get(op->ptr, "type");

    /* When invoking an effect strip which uses inputs, skip initializing the channel from the
     * mouse. */
    if (seq::effect_get_num_inputs(type) != 0) {
      prop_flag |= SEQPROP_NOCHAN;
    }
  }

  sequencer_generic_invoke_xy__internal(C, op, prop_flag, type);

  return sequencer_add_effect_strip_exec(C, op);
}

static std::string sequencer_add_effect_strip_get_description(bContext * /*C*/,
                                                              wmOperatorType * /*ot*/,
                                                              PointerRNA *ptr)
{
  const int type = RNA_enum_get(ptr, "type");

  switch (type) {
    case STRIP_TYPE_CROSS:
      return TIP_("Add a crossfade transition strip for two selected strips with video content");
    case STRIP_TYPE_ADD:
      return TIP_("Add an add blend mode effect strip for two selected strips with video content");
    case STRIP_TYPE_SUB:
      return TIP_(
          "Add a subtract blend mode effect strip for two selected strips with video content");
    case STRIP_TYPE_ALPHAOVER:
      return TIP_(
          "Add an alpha over blend mode effect strip for two selected strips with video content");
    case STRIP_TYPE_ALPHAUNDER:
      return TIP_(
          "Add an alpha under blend mode effect strip for two selected strips with video content");
    case STRIP_TYPE_GAMCROSS:
      return TIP_("Add a gamma cross transition strip for two selected strips with video content");
    case STRIP_TYPE_MUL:
      return TIP_(
          "Add a multiply blend mode effect strip for two selected strips with video content");
    case STRIP_TYPE_WIPE:
      return TIP_("Add a wipe transition strip for two selected strips with video content");
    case STRIP_TYPE_GLOW:
      return TIP_("Add a glow effect strip for a single selected strip with video content");
    case STRIP_TYPE_TRANSFORM:
      return TIP_("Add a transform effect strip for a single selected strip with video content");
    case STRIP_TYPE_COLOR:
      return TIP_("Add a color strip to the sequencer");
    case STRIP_TYPE_SPEED:
      return TIP_("Add a video speed effect strip for a single selected strip with video content");
    case STRIP_TYPE_MULTICAM:
      return TIP_("Add a multicam selector effect strip to the sequencer");
    case STRIP_TYPE_ADJUSTMENT:
      return TIP_("Add an adjustment layer effect strip to the sequencer");
    case STRIP_TYPE_GAUSSIAN_BLUR:
      return TIP_(
          "Add a gaussian blur effect strip for a single selected strip with video content");
    case STRIP_TYPE_TEXT:
      return TIP_("Add a text strip to the sequencer");
    case STRIP_TYPE_COLORMIX:
      return TIP_("Add a color mix effect strip to the sequencer");
    default:
      break;
  }

  /* Use default description. */
  return "";
}

void SEQUENCER_OT_effect_strip_add(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* Identifiers. */
  ot->name = "Add Effect Strip";
  ot->idname = "SEQUENCER_OT_effect_strip_add";
  ot->description = "Add an effect to the sequencer, most are applied on top of existing strips";

  /* Api callbacks. */
  ot->invoke = sequencer_add_effect_strip_invoke;
  ot->exec = sequencer_add_effect_strip_exec;
  ot->poll = ED_operator_sequencer_active_editable;
  ot->poll_property = seq_effect_add_properties_poll;
  ot->get_description = sequencer_add_effect_strip_get_description;

  /* Flags. */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  prop = RNA_def_enum(ot->srna,
                      "type",
                      sequencer_prop_effect_types,
                      STRIP_TYPE_CROSS,
                      "Type",
                      "Sequencer effect type");
  RNA_def_property_translation_context(prop, BLT_I18NCONTEXT_ID_SEQUENCE);
  sequencer_generic_props__internal(ot, SEQPROP_STARTFRAME | SEQPROP_ENDFRAME);
  /* Only used when strip is of the Color type. */
  prop = RNA_def_float_color(ot->srna,
                             "color",
                             3,
                             nullptr,
                             0.0f,
                             1.0f,
                             "Color",
                             "Initialize the strip with this color",
                             0.0f,
                             1.0f);
  RNA_def_property_subtype(prop, PROP_COLOR_GAMMA);
}

}  // namespace blender::ed::vse
