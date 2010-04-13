/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) 2003 Arwed v. Merkatz <v.merkatz@gmx.net>
 * Copyright (C) 2006 Mark Nauwelaerts <manauw@skynet.be>
 * Copyright (C) 2010 United States Government, Joshua M. Doe <oss@nvl.army.mil>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
* SECTION:element-videolevels
*
* Convert grayscale video from one bpp/depth combination to another.
*
* <refsect2>
* <title>Example launch line</title>
* |[
* gst-launch videotestsrc ! videolevels ! ffmpegcolorspace ! autovideosink
* ]|
* </refsect2>
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideolevels.h"

#include <string.h>
#include <math.h>

#include <gst/video/video.h>

/* GstVideoLevels signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LOWIN,
  PROP_HIGHIN,
  PROP_LOWOUT,
  PROP_HIGHOUT
      /* FILL ME */
};

#define DEFAULT_PROP_LOWIN  0.0
#define DEFAULT_PROP_HIGHIN  1.0
#define DEFAULT_PROP_LOWOUT  0.0
#define DEFAULT_PROP_HIGHOUT  1.0

static const GstElementDetails videolevels_details =
GST_ELEMENT_DETAILS ("Video videolevels adjustment",
    "Filter/Effect/Video",
    "Adjusts videolevels on a video stream",
    "Joshua Doe <oss@nvl.army.mil");

/* the capabilities of the inputs and outputs */
static GstStaticPadTemplate gst_videolevels_src_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      "video/x-raw-gray, "                                  \
      "bpp = (int) 16, "                                    \
      "depth = (int) 16, "                                  \
      "endianness = (int) {LITTLE_ENDIAN, BIG_ENDIAN}, "    \
      "width = " GST_VIDEO_SIZE_RANGE ", "                  \
      "height = " GST_VIDEO_SIZE_RANGE ", "                 \
      "framerate = " GST_VIDEO_FPS_RANGE                    \
      ";"                                                   \
      "video/x-raw-gray, "                                  \
      "bpp = (int) 16, "                                    \
      "depth = (int) 16, "                                  \
      "endianness = (int) {LITTLE_ENDIAN, BIG_ENDIAN}, "    \
      "signed = (bool) {true, false}, "                     \
      "width = " GST_VIDEO_SIZE_RANGE ", "                  \
      "height = " GST_VIDEO_SIZE_RANGE ", "                 \
      "framerate = " GST_VIDEO_FPS_RANGE
    )
);

static GstStaticPadTemplate gst_videolevels_sink_template =
GST_STATIC_PAD_TEMPLATE ("src",
     GST_PAD_SRC,
     GST_PAD_ALWAYS,
     GST_STATIC_CAPS (
       "video/x-raw-gray, "                                  \
       "bpp = (int) 8, "                                    \
       "depth = (int) 8, "                                  \
       "width = " GST_VIDEO_SIZE_RANGE ", "                  \
       "height = " GST_VIDEO_SIZE_RANGE ", "                 \
       "framerate = " GST_VIDEO_FPS_RANGE
     )
);

/* GObject vmethod declarations */
static void gst_videolevels_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_videolevels_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_videolevels_finalize (GObject *object);

/* GstBaseTransform vmethod declarations */
static GstCaps * gst_videolevels_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_videolevels_set_caps (GstBaseTransform * base,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_videolevels_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_videolevels_transform_ip (GstBaseTransform * base,
    GstBuffer * buf);
static gboolean gst_videolevels_get_unit_size (GstBaseTransform * base,
    GstCaps * caps, guint * size);

/* GstVideoLevels method declarations */
static void gst_videolevels_reset(GstVideoLevels* filter);
static void gst_videolevels_calculate_tables (GstVideoLevels * videolevels);
static gboolean gst_videolevels_do_levels (GstVideoLevels * videolevels,
    gpointer indata, gpointer outdata);

/* setup debug */
GST_DEBUG_CATEGORY_STATIC (videolevels_debug);
#define GST_CAT_DEFAULT videolevels_debug
#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (videolevels_debug, "videolevels", 0, \
    "Video Levels Filter");

GST_BOILERPLATE_FULL (GstVideoLevels, gst_videolevels, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER, DEBUG_INIT);

/************************************************************************/
/* GObject vmethod implementations                                      */
/************************************************************************/

/**
 * gst_videolevels_base_init:
 * @klass: #GstElementClass.
 *
 */
static void
gst_videolevels_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG ("base init");
  
  gst_element_class_set_details (element_class, &videolevels_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_videolevels_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_videolevels_src_template));
}

/**
 * gst_videolevels_finalize:
 * @object: #GObject.
 *
 */
static void
gst_videolevels_finalize (GObject *object)
{
  GstVideoLevels *videolevels = GST_VIDEOLEVELS (object);

  GST_DEBUG ("finalize");
  
  gst_videolevels_reset (videolevels);
  
  /* chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_videolevels_class_init:
 * @object: #GstVideoLevelsClass.
 *
 */
static void
gst_videolevels_class_init (GstVideoLevelsClass * object)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (object);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (object);

  GST_DEBUG ("class init");
  
 
  /* Register GObject vmethods */
  obj_class->finalize = GST_DEBUG_FUNCPTR (gst_videolevels_finalize);
  obj_class->set_property = GST_DEBUG_FUNCPTR (gst_videolevels_set_property);
  obj_class->get_property = GST_DEBUG_FUNCPTR (gst_videolevels_get_property);

  /* Install GObject properties */
  g_object_class_install_property (obj_class, PROP_LOWIN,
      g_param_spec_double ("low_in", "Lower Input Level", "Lower Input Level",
      0.0, 1.0, DEFAULT_PROP_LOWIN, G_PARAM_READWRITE));
  g_object_class_install_property (obj_class, PROP_HIGHIN,
      g_param_spec_double ("upper_in", "Upper Input Level", "Upper Input Level",
      0.0, 1.0, DEFAULT_PROP_HIGHIN, G_PARAM_READWRITE));
  g_object_class_install_property (obj_class, PROP_LOWOUT,
      g_param_spec_double ("low_out", "Lower Output Level", "Lower Output Level",
      0.0, 1.0, DEFAULT_PROP_LOWOUT, G_PARAM_READWRITE));
  g_object_class_install_property (obj_class, PROP_HIGHOUT,
      g_param_spec_double ("upper_out", "Upper Output Level", "Upper Output Level",
      0.0, 1.0, DEFAULT_PROP_HIGHOUT, G_PARAM_READWRITE));

  /* Register GstBaseTransform vmethods */
  trans_class->transform_caps = GST_DEBUG_FUNCPTR (gst_videolevels_transform_caps);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_videolevels_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_videolevels_transform);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_videolevels_transform_ip);
  trans_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_videolevels_get_unit_size);

  /* simply pass the data through if in/out caps are the same */
  trans_class->passthrough_on_same_caps = TRUE;
}

/**
* gst_videolevels_init:
* @videolevels: GstVideoLevels
* @g_class: GstVideoLevelsClass
*
* Initialize the new element
*/
static void
gst_videolevels_init (GstVideoLevels * videolevels,
    GstVideoLevelsClass * g_class)
{
  GST_DEBUG_OBJECT (videolevels, "init class instance");

  gst_videolevels_reset (videolevels);

}

/**
 * gst_videolevels_set_property:
 * @object: #GObject
 * @prop_id: guint
 * @value: #GValue
 * @pspec: #GParamSpec
 *
 */
static void
gst_videolevels_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoLevels *videolevels = GST_VIDEOLEVELS (object);

  GST_DEBUG ("setting property %s", pspec->name);

  switch (prop_id) {
    case PROP_LOWIN:
      videolevels->lower_input = g_value_get_double (value);
      gst_videolevels_calculate_tables (videolevels);
      break;
    case PROP_HIGHIN:
      videolevels->upper_input = g_value_get_double (value);
      gst_videolevels_calculate_tables (videolevels);
      break;
    case PROP_LOWOUT:
      videolevels->lower_output = g_value_get_double (value);
      gst_videolevels_calculate_tables (videolevels);
      break;
    case PROP_HIGHOUT:
      videolevels->upper_output = g_value_get_double (value);
      gst_videolevels_calculate_tables (videolevels);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * gst_videolevels_get_property:
 * @object: #GObject
 * @prop_id: guint
 * @value: #GValue
 * @pspec: #GParamSpec
 *
 */
static void
gst_videolevels_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoLevels *videolevels = GST_VIDEOLEVELS (object);

  GST_DEBUG ("getting property %s", pspec->name);

  switch (prop_id) {
    case PROP_LOWIN:
      g_value_set_double (value, videolevels->lower_input);
      break;
    case PROP_HIGHIN:
      g_value_set_double (value, videolevels->upper_input);
      break;
    case PROP_LOWOUT:
      g_value_set_double (value, videolevels->lower_output);
      break;
    case PROP_HIGHOUT:
      g_value_set_double (value, videolevels->upper_output);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/************************************************************************/
/* GstBaseTransform vmethod implementations                             */
/************************************************************************/

/**
 * gst_videolevels_transform_caps:
 * @base: #GstBaseTransform
 * @direction: #GstPadDirection
 * @caps: #GstCaps
 *
 * Given caps on one side, what caps are allowed on the other
 *
 * Returns: #GstCaps allowed on other pad
 */
static GstCaps *
gst_videolevels_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps)
{
  GstVideoLevels *videolevels;
  GstCaps *static_caps;
  GstCaps *newcaps;
  GstStructure *structure;
  const GValue * width;
  const GValue * height;

  videolevels = GST_VIDEOLEVELS (base);

  GST_DEBUG_OBJECT (caps, "transforming caps (from)");

  /* copy static pad caps to get bpp/depth/endianess */
  //if (direction == GST_PAD_SINK) {
  //  static_caps = gst_static_pad_template_get_caps (&gst_videolevels_sink_template);
  //}
  //else {
  //  static_caps = gst_static_pad_template_get_caps (&gst_videolevels_src_template);
  //}
  //structure = gst_caps_get_structure (static_caps, 0);
  //gst_structure_get_value()
  //newcaps = gst_caps_copy (caps);
  //gst_caps_unref (static_caps);

  ///* get width and height from proposed caps */
  //structure = gst_caps_get_structure (caps, 0);
  //width = gst_structure_get_value (structure, "width"); 
  //height = gst_structure_get_value (structure, "height");
  //
  ///* set width and height to new caps */
  //structure = gst_caps_get_structure (newcaps, 0);
  //gst_structure_set_value (structure, "width", width);
  //gst_structure_set_value (structure, "height", height);

  newcaps = gst_caps_copy (caps);

  /* finish settings caps of the opposite pad */
  if (direction == GST_PAD_SINK) {
    GST_DEBUG ("Pad direction is sink");
    gst_caps_set_simple (newcaps,
        "bpp", G_TYPE_INT, 8,
        "depth", G_TYPE_INT, 8,
        NULL);
    structure = gst_caps_get_structure (newcaps, 0);
    gst_structure_remove_field (structure, "endianness");
  }
  else {
    GValue endianness = {0};
    GValue signed_list = {0};
    GValue ival = {0};
    
    GST_DEBUG ("Pad direction is src");

    gst_caps_set_simple (newcaps,
      "bpp", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      NULL);
    structure = gst_caps_get_structure (newcaps, 0);

    /* add BIG/LITTLE endianness to caps */
    g_value_init (&ival, G_TYPE_INT);
    g_value_init (&endianness, GST_TYPE_LIST);
    g_value_set_int (&ival, G_LITTLE_ENDIAN);
    gst_value_list_append_value (&endianness, &ival);
    g_value_set_int (&ival, G_BIG_ENDIAN);
    gst_value_list_append_value (&endianness, &ival);
    gst_structure_set_value (structure, "endianness", &endianness);

    /* add signed/unsigned to caps */
    g_value_init (&signed_list, GST_TYPE_LIST);
    g_value_set_int (&ival, TRUE);
    gst_value_list_append_value (&signed_list, &ival);
    g_value_set_int (&ival, FALSE);
    gst_value_list_append_value (&signed_list, &ival);
    gst_structure_set_value (structure, "signed", &signed_list);
  }
  GST_DEBUG_OBJECT (newcaps, "allowed caps are");

  return newcaps;
}

/**
 * gst_videolevels_set_caps:
 * base: #GstBaseTransform
 * incaps: #GstCaps
 * outcaps: #GstCaps
 * 
 * Notification of the actual caps set.
 *
 * Returns: TRUE on success
 */
static gboolean
gst_videolevels_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoLevels *levels;
  GstStructure *structure;
  gboolean res;

  levels = GST_VIDEOLEVELS (base);

  GST_DEBUG_OBJECT (levels,
      "set_caps: in %" GST_PTR_FORMAT " out %" GST_PTR_FORMAT, incaps, outcaps);

  GST_DEBUG_OBJECT (incaps, "incaps");
  GST_DEBUG_OBJECT (outcaps, "outcaps");

  /* retrieve input caps info */
  structure = gst_caps_get_structure (incaps, 0);
  res = gst_structure_get (structure,
      "width", G_TYPE_INT, &levels->width,
      "height", G_TYPE_INT, &levels->height,
      "bpp", G_TYPE_INT, &levels->bpp_in,
      "depth", G_TYPE_INT, &levels->depth_in,
      "endianness", G_TYPE_INT, &levels->endianness_in,
      NULL);
  if (!res)
    return FALSE;

  if (!gst_structure_get (structure,
      "signed", G_TYPE_BOOLEAN, &levels->is_signed_in))
      levels->is_signed_in = FALSE;

  /* retrieve src caps bpp/depth/endianness */
  structure = gst_caps_get_structure (incaps, 0);
  res = gst_structure_get (structure,
    "width", G_TYPE_INT, &levels->width,
    "height", G_TYPE_INT, &levels->height,
    "bpp", G_TYPE_INT, &levels->bpp_in,
    "depth", G_TYPE_INT, &levels->depth_in,
    "endianness", G_TYPE_INT, &levels->endianness_in,
    NULL);
  if (!res)
    return FALSE;

  levels->stride_in = GST_ROUND_UP_4 (levels->width * levels->depth_in/8);
  levels->stride_out = GST_ROUND_UP_4 (levels->width * levels->depth_out/8);

  gst_videolevels_calculate_tables (levels);

  return res;
}

/**
 * gst_videolevels_get_unit_size:
 * @base: #GstBaseTransform
 * @caps: #GstCaps
 * @size: guint size of unit (one frame for video)
 *
 * Tells GstBaseTransform the size in bytes of an output frame from the given
 * caps.
 *
 * Returns: TRUE on success
 */
static gboolean
gst_videolevels_get_unit_size (GstBaseTransform * base, GstCaps * caps,
    guint * size)
{
  GstStructure *structure;
  gint width;
  gint height;
  gint depth;

  structure = gst_caps_get_structure (caps, 0);

  /* get proposed caps width, height, and depth to determine frame size */
  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height) &&
      gst_structure_get_int (structure, "depth", &depth)) {
    guint stride = GST_ROUND_UP_4 (width*depth/8); /* need 4-byte alignment */
    *size = stride * height;
    GST_DEBUG ("Get unit size %dx%d, stride %u, %u bytes", width, height,
        stride, *size);
    return TRUE;
  }

  GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
    ("Incomplete caps, some required field missing"));
  return FALSE;
}

/**
 * gst_videolevels_transform:
 * @base: #GstBaseTransform
 * @inbuf: #GstBuffer
 * @outbuf: #GstBuffer
 *
 * Transforms input buffer to output buffer.
 *
 * Returns: GST_FLOW_OK on success
 */
static GstFlowReturn
gst_videolevels_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVideoLevels *filter = GST_VIDEOLEVELS (base);
  gpointer input;
  gpointer output;
  gboolean ret;

  /*
  * We need to lock our filter params to prevent changing
  * caps in the middle of a transformation (nice way to get
  * segfaults)
  */
  GST_OBJECT_LOCK (filter);

  input = GST_BUFFER_DATA (inbuf);
  output = GST_BUFFER_DATA (outbuf);

  ret = gst_videolevels_do_levels (filter, input, output);

  GST_OBJECT_UNLOCK (filter);

  if (ret)
    return GST_FLOW_OK;
  else
    return GST_FLOW_ERROR;
}

GstFlowReturn gst_videolevels_transform_ip( GstBaseTransform * base, GstBuffer * buf )
{

  return GST_FLOW_OK;
}

/************************************************************************/
/* GstVideoLevels method implementations                                */
/************************************************************************/

/**
 * gst_videolevels_reset:
 * @videolevels: #GstVideoLevels
 *
 * Reset instance variables and free memory
 */
static void gst_videolevels_reset(GstVideoLevels* videolevels)
{
  videolevels->width = 0;
  videolevels->height = 0;

  videolevels->stride_in = 0;
  videolevels->bpp_in = 0;
  videolevels->depth_in = 0;
  videolevels->endianness_in = 0;
  videolevels->is_signed_in = FALSE;

  videolevels->stride_out = 0;
  videolevels->bpp_out = 0;
  videolevels->depth_out = 0;
  videolevels->endianness_out = 0;

  videolevels->lower_input = DEFAULT_PROP_LOWIN;
  videolevels->upper_input = DEFAULT_PROP_HIGHIN;
  videolevels->lower_output = DEFAULT_PROP_LOWOUT;
  videolevels->upper_output = DEFAULT_PROP_HIGHOUT;

  g_free (videolevels->lookup_table);
  videolevels->lookup_table = NULL;
}

/**
 * gst_videolevels_calculate_tables
 * @videolevels: #GstVideoLevels
 *
 * Calculate lookup tables based on input and output levels
 */
static void
gst_videolevels_calculate_tables (GstVideoLevels * videolevels)
{
  gint i;
  guint8 loIn, hiIn;
  guint8 loOut, hiOut;
  gdouble slope;
  guint8 * lut;

  GST_DEBUG ("calculating lookup table");

  if (!videolevels->lookup_table) {
    videolevels->lookup_table = g_malloc (256);
  }
  lut = (guint8*) videolevels->lookup_table;

  loIn = (guint8) videolevels->lower_input * 255;
  hiIn = (guint8) videolevels->upper_input * 255;
  loOut = (guint8) videolevels->lower_output * 255;
  hiOut = (guint8) videolevels->upper_output * 255;

  if (hiIn == loIn)
    slope = 0;
  else
    slope = (videolevels->upper_output - videolevels->lower_output) /
        (videolevels->upper_input - videolevels->lower_input);

  for (i=0; i<loIn; i++)
    lut[i] = loOut;
  for (i=loIn; i<hiIn; i++)
    lut[i] = loOut + (guint8) ( (i-loIn) * slope);
  for (i=hiIn; i<256; i++)
    lut[i] = hiOut;
}

/**
 * gst_videolevels_do_levels
 * @videolevels: #GstVideoLevels
 * @indata: input data
 * @outdata: output data
 * @size: size of data
 *
 * Convert frame using previously calculated LUT
 *
 * Returns: TRUE on success
 */
gboolean
gst_videolevels_do_levels (GstVideoLevels * videolevels, gpointer indata,
    gpointer outdata)
{
  guint8 * dst = outdata;
  guint16 * src = indata;
  gint r, c;
  guint8 * lut = (guint8 *) videolevels->lookup_table;

  GST_DEBUG ("Converting frame using LUT");
  
  if (!videolevels->is_signed_in) {
    if (videolevels->endianness_in == G_BYTE_ORDER) {
      for (r = 0; r < videolevels->height; r++) {
        for (c = 0; c < videolevels->width; c++) {
          dst[c+r*videolevels->stride_out] =
              lut[src[c+r*videolevels->stride_in] >> 8];
        }
        GST_DEBUG ("Row %d", r);
      }
    }
    else {
      for (r = 0; r < videolevels->height; r++) {
        for (c = 0; c < videolevels->width; c++) {
          dst[c+r*videolevels->stride_out] =
            lut[GUINT16_FROM_BE(src[c+r*videolevels->stride_in]) >> 8];
        }
      }
    }
  }
  else {
    if (videolevels->endianness_in == G_BYTE_ORDER) {
      for (r = 0; r < videolevels->height; r++) {
        for (c = 0; c < videolevels->width; c++) {
          dst[c+r*videolevels->stride_out] =
            lut[(src[c+r*videolevels->stride_in]+32767) >> 8];
        }
        GST_DEBUG ("Row %d", r);
      }
    }
    else {
      for (r = 0; r < videolevels->height; r++) {
        for (c = 0; c < videolevels->width; c++) {
          dst[c+r*videolevels->stride_out] =
            lut[(GUINT16_FROM_BE(src[c+r*videolevels->stride_in])+32767) >> 8];
        }
      }
    }
  }

  GST_DEBUG ("DONE converting frame using LUT");

  return TRUE;

  //gdouble loIn = videolevels->lower_input;
  //gdouble hiIn = videolevels->upper_input;
  //gdouble loOut = videolevels->lower_output;
  //gdouble hiOut = videolevels->upper_output;
  //gdouble slope;
  //gdouble yintercept;
  //guint32 dst_max;
  //guint32 src_max;
  //
  ///* y=mx+b */
  ///* check for division by zero */
  //if (hiIn == loIn)
  //  slope=0;
  //else
  //  slope = (hiOut-loOut)/(hiIn-loIn);
  //yintercept = loOut - slope*loIn;

  //src_max = (1 << videolevels->bpp_in) - 1;
  //dst_max = (1 << videolevels->bpp_out) - 1;

  /* TODO doesn't handle byte ordering */
  //if (videolevels->depth_in == 32 && videolevels->depth_out == 32) {
  //  guint32 * src = indata;
  //  guint32 * dst = outdata;
  //  gint r;
  //  gint c;
  //  guint32 b = dst_max * yintercept;
  //  gdouble m = slope*dst_max/src_max;


  //  for (r = 0; r < videolevels->height; r++) {
  //    for (c = 0; c < videolevels->width; c++) {
  //      dst[c+r*videolevels->stride_out] = m*src[c+r*videolevels->stride_in] + b;
  //    }
  //  }
  //}
  //else if (videolevels->depth_in == 16 && videolevels->depth_out == 8) {
  //  guint16 * src = indata;
  //  guint8 * dst = outdata;
  //  gint r;
  //  gint c;
  //  guint8 b = dst_max * yintercept;
  //  gdouble m = slope*dst_max/src_max;


  //  for (r = 0; r < videolevels->height; r++) {
  //    for (c = 0; c < videolevels->width; c++) {
  //      dst[c+r*videolevels->stride_out] = m*src[c+r*videolevels->stride_in] + b;
  //    }
  //  }
  //}
  //else {
  //  return FALSE;
  //}
  return TRUE;
}
