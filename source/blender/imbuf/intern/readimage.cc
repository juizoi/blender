/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup imbuf
 */

#ifdef _WIN32
#  include <io.h>
#  include <stddef.h>
#  include <sys/types.h>
#endif

#include "BLI_fileops.h"
#include "BLI_mmap.h"
#include "BLI_path_utils.hh" /* For assertions. */
#include "BLI_string.h"
#include <cstdlib>

#include "IMB_allocimbuf.hh"
#include "IMB_filetype.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "IMB_metadata.hh"
#include "IMB_thumbs.hh"
#include "imbuf.hh"

#include "IMB_colormanagement.hh"
#include "IMB_colormanagement_intern.hh"

static void imb_handle_colorspace_and_alpha(ImBuf *ibuf,
                                            int flags,
                                            const ImFileColorSpace &file_colorspace,
                                            char r_colorspace[IM_MAX_SPACE])
{
  char new_colorspace[IM_MAX_SPACE];

  if (r_colorspace && r_colorspace[0]) {
    /* Existing configured colorspace has priority. */
    STRNCPY(new_colorspace, r_colorspace);
  }
  else if (file_colorspace.metadata_colorspace[0] &&
           colormanage_colorspace_get_named(file_colorspace.metadata_colorspace))
  {
    /* Use colorspace from file metadata if provided. */
    STRNCPY(new_colorspace, file_colorspace.metadata_colorspace);
  }
  else {
    /* Use float colorspace if the image may contain HDR colors, byte otherwise. */
    const char *role_colorspace = IMB_colormanagement_role_colorspace_name_get(
        file_colorspace.is_hdr_float ? COLOR_ROLE_DEFAULT_FLOAT : COLOR_ROLE_DEFAULT_BYTE);
    STRNCPY(new_colorspace, role_colorspace);
  }

  if (r_colorspace) {
    if (ibuf->byte_buffer.data != nullptr && ibuf->float_buffer.data == nullptr) {
      /* byte buffer is never internally converted to some standard space,
       * store pointer to its color space descriptor instead
       */
      ibuf->byte_buffer.colorspace = colormanage_colorspace_get_named(new_colorspace);
    }
  }

  bool is_data = (r_colorspace && IMB_colormanagement_space_name_is_data(new_colorspace));
  int alpha_flags = (flags & IB_alphamode_detect) ? ibuf->flags : flags;

  if (is_data || (flags & IB_alphamode_channel_packed)) {
    /* Don't touch alpha. */
    ibuf->flags |= IB_alphamode_channel_packed;
  }
  else if (flags & IB_alphamode_ignore) {
    /* Make opaque. */
    IMB_rectfill_alpha(ibuf, 1.0f);
    ibuf->flags |= IB_alphamode_ignore;
  }
  else {
    if (alpha_flags & IB_alphamode_premul) {
      if (ibuf->byte_buffer.data) {
        IMB_unpremultiply_alpha(ibuf);
      }
      else {
        /* pass, floats are expected to be premul */
      }
    }
    else {
      if (ibuf->float_buffer.data) {
        IMB_premultiply_alpha(ibuf);
      }
      else {
        /* pass, bytes are expected to be straight */
      }
    }
  }

  colormanage_imbuf_make_linear(ibuf, new_colorspace);
  if (r_colorspace) {
    BLI_strncpy(r_colorspace, new_colorspace, IM_MAX_SPACE);
  }
}

ImBuf *IMB_ibImageFromMemory(
    const uchar *mem, size_t size, int flags, char colorspace[IM_MAX_SPACE], const char *descr)
{
  ImBuf *ibuf;
  const ImFileType *type;

  if (mem == nullptr) {
    fprintf(stderr, "%s: nullptr pointer\n", __func__);
    return nullptr;
  }

  ImFileColorSpace file_colorspace;

  for (type = IMB_FILE_TYPES; type < IMB_FILE_TYPES_LAST; type++) {
    if (type->load) {
      ibuf = type->load(mem, size, flags, file_colorspace);
      if (ibuf) {
        imb_handle_colorspace_and_alpha(ibuf, flags, file_colorspace, colorspace);
        return ibuf;
      }
    }
  }

  if ((flags & IB_test) == 0) {
    fprintf(stderr, "%s: unknown file-format (%s)\n", __func__, descr);
  }

  return nullptr;
}

ImBuf *IMB_loadifffile(int file, int flags, char colorspace[IM_MAX_SPACE], const char *descr)
{
  ImBuf *ibuf;

  if (file == -1) {
    return nullptr;
  }

  imb_mmap_lock();
  BLI_mmap_file *mmap_file = BLI_mmap_open(file);
  imb_mmap_unlock();
  if (mmap_file == nullptr) {
    fprintf(stderr, "%s: couldn't get mapping %s\n", __func__, descr);
    return nullptr;
  }

  const uchar *mem = static_cast<const uchar *>(BLI_mmap_get_pointer(mmap_file));
  const size_t size = BLI_mmap_get_length(mmap_file);

  ibuf = IMB_ibImageFromMemory(mem, size, flags, colorspace, descr);

  imb_mmap_lock();
  BLI_mmap_free(mmap_file);
  imb_mmap_unlock();

  return ibuf;
}

ImBuf *IMB_loadiffname(const char *filepath, int flags, char colorspace[IM_MAX_SPACE])
{
  ImBuf *ibuf;
  int file;

  BLI_assert(!BLI_path_is_rel(filepath));

  file = BLI_open(filepath, O_BINARY | O_RDONLY, 0);
  if (file == -1) {
    return nullptr;
  }

  ibuf = IMB_loadifffile(file, flags, colorspace, filepath);

  if (ibuf) {
    STRNCPY(ibuf->filepath, filepath);
  }

  close(file);

  return ibuf;
}

ImBuf *IMB_thumb_load_image(const char *filepath,
                            size_t max_thumb_size,
                            char colorspace[IM_MAX_SPACE],
                            IMBThumbLoadFlags load_flags)
{
  const ImFileType *type = IMB_file_type_from_ftype(IMB_ispic_type(filepath));
  if (type == nullptr) {
    return nullptr;
  }

  ImBuf *ibuf = nullptr;
  int flags = IB_byte_data | IB_metadata;
  /* Size of the original image. */
  size_t width = 0;
  size_t height = 0;

  if (type->load_filepath_thumbnail) {
    ImFileColorSpace file_colorspace;
    ibuf = type->load_filepath_thumbnail(
        filepath, flags, max_thumb_size, file_colorspace, &width, &height);
    if (ibuf) {
      imb_handle_colorspace_and_alpha(ibuf, flags, file_colorspace, colorspace);
    }
  }
  else {
    /* Skip images of other types if over 100MB. */
    if ((load_flags & IMBThumbLoadFlags::LoadLargeFiles) == IMBThumbLoadFlags::Zero) {
      const size_t file_size = BLI_file_size(filepath);
      if (file_size != size_t(-1) && file_size > THUMB_SIZE_MAX) {
        return nullptr;
      }
    }
    ibuf = IMB_loadiffname(filepath, flags, colorspace);
    if (ibuf) {
      width = ibuf->x;
      height = ibuf->y;
    }
  }

  if (ibuf) {
    if (width > 0 && height > 0) {
      /* Save dimensions of original image into the thumbnail metadata. */
      char cwidth[40];
      char cheight[40];
      SNPRINTF(cwidth, "%zu", width);
      SNPRINTF(cheight, "%zu", height);
      IMB_metadata_ensure(&ibuf->metadata);
      IMB_metadata_set_field(ibuf->metadata, "Thumb::Image::Width", cwidth);
      IMB_metadata_set_field(ibuf->metadata, "Thumb::Image::Height", cheight);
    }
  }

  return ibuf;
}

ImBuf *IMB_testiffname(const char *filepath, int flags)
{
  ImBuf *ibuf;
  int file;
  char colorspace[IM_MAX_SPACE] = "\0";

  BLI_assert(!BLI_path_is_rel(filepath));

  file = BLI_open(filepath, O_BINARY | O_RDONLY, 0);
  if (file == -1) {
    return nullptr;
  }

  ibuf = IMB_loadifffile(file, flags | IB_test | IB_multilayer, colorspace, filepath);

  if (ibuf) {
    STRNCPY(ibuf->filepath, filepath);
  }

  close(file);

  return ibuf;
}
