/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystree00@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gl/gl.h>
#include "gstgldownloadelement.h"

GST_DEBUG_CATEGORY_STATIC (gst_gl_download_element_debug);
#define GST_CAT_DEFAULT gst_gl_download_element_debug

#define gst_gl_download_element_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLDownloadElement, gst_gl_download_element,
    GST_TYPE_GL_BASE_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_gl_download_element_debug, "gldownloadelement",
        0, "download element");
    );
static void gst_gl_download_element_finalize (GObject * object);

static gboolean gst_gl_download_element_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static gboolean gst_gl_download_element_query (GstBaseTransform * bt,
    GstPadDirection direction, GstQuery * query);
static GstCaps *gst_gl_download_element_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_gl_download_element_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps);
static GstFlowReturn
gst_gl_download_element_prepare_output_buffer (GstBaseTransform * bt,
    GstBuffer * buffer, GstBuffer ** outbuf);
static GstFlowReturn gst_gl_download_element_transform (GstBaseTransform * bt,
    GstBuffer * buffer, GstBuffer * outbuf);

static GstStaticPadTemplate gst_gl_download_element_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw; video/x-raw(memory:GLMemory)"));

static GstStaticPadTemplate gst_gl_download_element_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(memory:GLMemory)"));

static void
gst_gl_download_element_class_init (GstGLDownloadElementClass * klass)
{
  GstBaseTransformClass *bt_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  bt_class->query = gst_gl_download_element_query;
  bt_class->transform_caps = gst_gl_download_element_transform_caps;
  bt_class->set_caps = gst_gl_download_element_set_caps;
  bt_class->get_unit_size = gst_gl_download_element_get_unit_size;
  bt_class->prepare_output_buffer =
      gst_gl_download_element_prepare_output_buffer;
  bt_class->transform = gst_gl_download_element_transform;

  bt_class->passthrough_on_same_caps = TRUE;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_download_element_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_download_element_sink_pad_template));

  gst_element_class_set_metadata (element_class,
      "OpenGL uploader", "Filter/Video",
      "Uploads data into OpenGL", "Matthew Waters <matthew@centricular.com>");

  G_OBJECT_CLASS (klass)->finalize = gst_gl_download_element_finalize;
}

static void
gst_gl_download_element_init (GstGLDownloadElement * download)
{
  gst_base_transform_set_prefer_passthrough (GST_BASE_TRANSFORM (download),
      TRUE);
}

static void
gst_gl_download_element_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_gl_download_element_parent_class)->finalize (object);
}

static gboolean
gst_gl_download_element_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
    {
      if (direction == GST_PAD_SINK)
        return gst_pad_peer_query (GST_BASE_TRANSFORM_SRC_PAD (trans), query);
      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
      query);
}

static gboolean
gst_gl_download_element_set_caps (GstBaseTransform * bt, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstVideoInfo out_info;

  if (!gst_video_info_from_caps (&out_info, out_caps))
    return FALSE;

  return TRUE;
}

static GstCaps *
_set_caps_features (const GstCaps * caps, const gchar * feature_name)
{
  GstCaps *tmp = gst_caps_copy (caps);
  guint n = gst_caps_get_size (tmp);
  guint i = 0;

  for (i = 0; i < n; i++)
    gst_caps_set_features (tmp, i,
        gst_caps_features_from_string (feature_name));

  return tmp;
}

static GstCaps *
gst_gl_download_element_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *result, *tmp;

  if (direction == GST_PAD_SRC) {
    tmp = _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
    tmp = gst_caps_merge (gst_caps_ref (caps), tmp);
  } else {
    tmp = _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
    tmp = gst_caps_merge (gst_caps_ref (caps), tmp);
  }

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_ERROR_OBJECT (bt, "returning caps %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_gl_download_element_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  gboolean ret = FALSE;
  GstVideoInfo info;

  ret = gst_video_info_from_caps (&info, caps);
  if (ret)
    *size = GST_VIDEO_INFO_SIZE (&info);

  return TRUE;
}

static GstFlowReturn
gst_gl_download_element_prepare_output_buffer (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  *outbuf = gst_buffer_ref (inbuf);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_gl_download_element_transform (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  return GST_FLOW_OK;
}
