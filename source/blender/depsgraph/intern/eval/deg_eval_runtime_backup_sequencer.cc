/* SPDX-FileCopyrightText: 2019 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup depsgraph
 */

#include "BLI_session_uid.h"

#include "intern/eval/deg_eval_runtime_backup_sequencer.h"

#include "DNA_scene_types.h"
#include "DNA_sequence_types.h"

#include "BLI_assert.h"

#include "BKE_sound.h"

#include "SEQ_iterator.hh"

namespace blender::deg {

SequencerBackup::SequencerBackup(const Depsgraph *depsgraph) : depsgraph(depsgraph) {}

static bool strip_init_cb(Strip *strip, void *user_data)
{
  SequencerBackup *sb = (SequencerBackup *)user_data;
  SequenceBackup sequence_backup(sb->depsgraph);
  sequence_backup.init_from_sequence(strip);
  if (!sequence_backup.isEmpty()) {
    const SessionUID &session_uid = strip->runtime.session_uid;
    BLI_assert(BLI_session_uid_is_generated(&session_uid));
    sb->sequences_backup.add(session_uid, sequence_backup);
  }
  return true;
}

void SequencerBackup::init_from_scene(Scene *scene)
{
  if (scene->ed != nullptr) {
    seq::for_each_callback(&scene->ed->seqbase, strip_init_cb, this);
  }
}

static bool strip_restore_cb(Strip *strip, void *user_data)
{
  SequencerBackup *sb = (SequencerBackup *)user_data;
  const SessionUID &session_uid = strip->runtime.session_uid;
  BLI_assert(BLI_session_uid_is_generated(&session_uid));
  SequenceBackup *sequence_backup = sb->sequences_backup.lookup_ptr(session_uid);
  if (sequence_backup != nullptr) {
    sequence_backup->restore_to_sequence(strip);
  }
  return true;
}

void SequencerBackup::restore_to_scene(Scene *scene)
{
  if (scene->ed != nullptr) {
    seq::for_each_callback(&scene->ed->seqbase, strip_restore_cb, this);
  }
  /* Cleanup audio while the scene is still known. */
  for (SequenceBackup &sequence_backup : sequences_backup.values()) {
    if (sequence_backup.scene_sound != nullptr) {
      BKE_sound_remove_scene_sound(scene, sequence_backup.scene_sound);
    }
  }
}

}  // namespace blender::deg
