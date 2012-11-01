/*
 * libInstPatch
 * Copyright (C) 1999-2010 Joshua "Element" Green <jgreen@users.sourceforge.net>
 *
 * Author of this file: (C) 2012 BALATON Zoltan <balaton@eik.bme.hu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2.1
 * of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA or on the web at http://www.gnu.org.
 */
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include "misc.h"
#include "IpatchConvert_SLI.h"
#include "IpatchConverter.h"
#include "IpatchConverter_priv.h"
#include "IpatchSndFile.h"
#include "IpatchSLI.h"
#include "IpatchSLIWriter.h"
#include "IpatchSLIReader.h"
#include "IpatchSample.h"
#include "IpatchSampleData.h"
#include "IpatchSampleStoreSndFile.h"
#include "IpatchBase.h"
#include "IpatchFile.h"
#include "i18n.h"

/*
 * Spectralis conversion handlers
 * IpatchSLI <==> IpatchSLIFile
 * IpatchSndFile => IpatchSLISample
 */

/* init routine for SLI conversion types */
void
_ipatch_convert_SLI_init (void)
{
  /*  g_type_class_ref (IPATCH_TYPE_CONVERTER_SLI_TO_FILE); */
  g_type_class_ref (IPATCH_TYPE_CONVERTER_FILE_TO_SLI);
  g_type_class_ref (IPATCH_TYPE_CONVERTER_FILE_TO_SLI_SAMPLE);

  ipatch_register_converter_map (IPATCH_TYPE_CONVERTER_SLI_TO_FILE, 0,
				 IPATCH_TYPE_SLI, 0, 1,
				 IPATCH_TYPE_SLI_FILE, IPATCH_TYPE_FILE, 1);
  ipatch_register_converter_map (IPATCH_TYPE_CONVERTER_FILE_TO_SLI, 0,
				 IPATCH_TYPE_SLI_FILE, IPATCH_TYPE_FILE, 1,
				 IPATCH_TYPE_SLI, IPATCH_TYPE_BASE, 0);
  ipatch_register_converter_map (IPATCH_TYPE_CONVERTER_FILE_TO_SLI_SAMPLE, 0,
				 IPATCH_TYPE_SND_FILE, IPATCH_TYPE_FILE, 1,
				 IPATCH_TYPE_SLI_SAMPLE, 0, 1);
}

/* ===============
 * Convert methods
 * =============== */

static gboolean
_sli_to_file_convert (IpatchConverter *converter, GError **err)
{
  IpatchSLI *sli;
  IpatchSLIFile *file;
  IpatchFileHandle *handle;
  IpatchSLIWriter *writer;
  int retval;

  sli = IPATCH_SLI (IPATCH_CONVERTER_INPUT (converter));
  file = IPATCH_SLI_FILE (IPATCH_CONVERTER_OUTPUT (converter));

  handle = ipatch_file_open (IPATCH_FILE (file), NULL, "w", err);
  if (!handle) return (FALSE);

  writer = ipatch_sli_writer_new (handle, sli); /* ++ ref new writer */
  retval = ipatch_sli_writer_save (writer, err);
  g_object_unref (writer); /* -- unref writer */

  return (retval);
}

static gboolean
_file_to_sli_convert (IpatchConverter *converter, GError **err)
{
  IpatchSLI *sli;
  IpatchSLIFile *file;
  IpatchFileHandle *handle;
  IpatchSLIReader *reader;

  file = IPATCH_SLI_FILE (IPATCH_CONVERTER_INPUT (converter));

  handle = ipatch_file_open (IPATCH_FILE (file), NULL, "r", err);
  if (!handle) return (FALSE);

  reader = ipatch_sli_reader_new (handle); /* ++ ref new reader */
  sli = ipatch_sli_reader_load (reader, err); /* ++ ref loaded SLI object */
  g_object_unref (reader);	/* -- unref reader */

  if (sli)
    {
      ipatch_converter_add_output (converter, G_OBJECT (sli));
      g_object_unref (sli);	/* -- unref loaded SLI */
      return (TRUE);
    }
  else return (FALSE);
}

static gboolean
_file_to_sli_sample_convert (IpatchConverter *converter, GError **err)
{
  IpatchSndFile *file;
  IpatchSLISample *slisample;
  IpatchSampleStoreSndFile *store;
  IpatchSampleData *sampledata;
  int format, rate, loop_type, root_note, fine_tune;
  guint length, loop_start, loop_end;
  char *filename, *title;

  file = IPATCH_SND_FILE (IPATCH_CONVERTER_INPUT (converter));
  slisample = IPATCH_SLI_SAMPLE (IPATCH_CONVERTER_OUTPUT (converter));

  filename = ipatch_file_get_name (IPATCH_FILE (file)); /* ++ alloc file name */
  if (!filename)
  {
    g_set_error (err, IPATCH_ERROR, IPATCH_ERROR_PROGRAM,
                 _("Sample file object must have a file name"));
    return (FALSE);
  }

  /* ++ ref store */
  store = IPATCH_SAMPLE_STORE_SND_FILE (ipatch_sample_store_snd_file_new (filename));
  if (!ipatch_sample_store_snd_file_init_read (store))
  {
    g_set_error (err, IPATCH_ERROR, IPATCH_ERROR_UNSUPPORTED,
                 _("Sample file '%s' is invalid or unsupported"), filename);
    g_free (filename);    /* -- free filename */
    g_object_unref (store);     /* -- unref store */
    return (FALSE);
  }
  g_free (filename);    /* -- free filename */

  g_object_get (store,
		"title", &title, /* ++ alloc title */
		"sample-size", &length,
		"sample-format", &format,
		"sample-rate", &rate,
		"loop-type", &loop_type,
		"loop-start", &loop_start,
		"loop-end", &loop_end,
		"root-note", &root_note,
		"fine-tune", &fine_tune,
		NULL);

  if (length < 4)
  {
    g_set_error (err, IPATCH_ERROR, IPATCH_ERROR_INVALID,
                 _("Sample '%s' is too small"),
                 title ? title : _("<no name>"));
    goto bad;
  }

  /* FIXME: may need to convert unsupported formats here */
  if (!(IPATCH_SAMPLE_FORMAT_GET_WIDTH(format) == IPATCH_SAMPLE_16BIT &&
        IPATCH_SAMPLE_FORMAT_IS_SIGNED(format) &&
        IPATCH_SAMPLE_FORMAT_IS_LENDIAN(format)))
  {
    g_set_error (err, IPATCH_ERROR, IPATCH_ERROR_UNSUPPORTED,
                 _("Unsupported sample format in sample '%s'"), title);
    goto bad;
  }

  sampledata = ipatch_sample_data_new (); /* ++ ref sampledata */
  ipatch_sample_data_add (sampledata, IPATCH_SAMPLE_STORE (store));
  g_object_unref (store); /* -- unref store */

  g_object_set (slisample,
		"name", title,
                "sample-data", sampledata,
		"sample-rate", rate,
		"root-note", (root_note != -1) ? root_note : 60,
		"fine-tune", fine_tune,
		"loop-start", loop_start,
		"loop-end", loop_end,
		NULL);

  g_object_unref (sampledata);  /* -- unref sampledata */
  g_free (title);               /* -- free title */

  return (TRUE);

bad:
  g_free (title);             /* -- free title */
  g_object_unref (store);     /* -- unref store */
  return (FALSE);
}

CONVERTER_CLASS_INIT(sli_to_file);
CONVERTER_CLASS_INIT(file_to_sli);
CONVERTER_CLASS_INIT(file_to_sli_sample);


CONVERTER_GET_TYPE(sli_to_file, SLIToFile);
CONVERTER_GET_TYPE(file_to_sli, FileToSLI);
CONVERTER_GET_TYPE(file_to_sli_sample, FileToSLISample);