/* -LICENSE-START-
** Copyright (c) 2009 Blackmagic Design
**
** Permission is hereby granted, free of charge, to any person or organization
** obtaining a copy of the software and accompanying documentation covered by
** this license (the "Software") to use, reproduce, display, distribute,
** execute, and transmit the Software, and to prepare derivative works of the
** Software, and to permit third-parties to whom the Software is furnished to
** do so, all subject to the following:
** 
** The copyright notices in the Software and this entire statement, including
** the above license grant, this restriction and the following disclaimer,
** must be included in all copies of the Software, in whole or in part, and
** all derivative works of the Software, unless such copies or derivative
** works are solely in the form of machine-executable object code generated by
** a source language processor.
** 
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
** -LICENSE-END-
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

#include "gstdecklinksrc.h"

#include "DeckLinkAPI.h"
#include "capture.h"


int videoOutputFile = -1;
int audioOutputFile = -1;

IDeckLink *deckLink;
IDeckLinkInput *deckLinkInput;
IDeckLinkDisplayModeIterator *displayModeIterator;

static BMDTimecodeFormat g_timecodeFormat = 0;

DeckLinkCaptureDelegate::DeckLinkCaptureDelegate ():m_refCount (0)
{
  pthread_mutex_init (&m_mutex, NULL);
}

DeckLinkCaptureDelegate::~DeckLinkCaptureDelegate ()
{
  pthread_mutex_destroy (&m_mutex);
}

ULONG DeckLinkCaptureDelegate::AddRef (void)
{
  pthread_mutex_lock (&m_mutex);
  m_refCount++;
  pthread_mutex_unlock (&m_mutex);

  return (ULONG) m_refCount;
}

ULONG DeckLinkCaptureDelegate::Release (void)
{
  pthread_mutex_lock (&m_mutex);
  m_refCount--;
  pthread_mutex_unlock (&m_mutex);

  if (m_refCount == 0) {
    delete
        this;
    return 0;
  }

  return (ULONG) m_refCount;
}

HRESULT
    DeckLinkCaptureDelegate::VideoInputFrameArrived (IDeckLinkVideoInputFrame *
    videoFrame, IDeckLinkAudioInputPacket * audioFrame)
{
  GstDecklinkSrc *decklinksrc;

  g_return_val_if_fail (priv != NULL, S_OK);
  g_return_val_if_fail (GST_IS_DECKLINK_SRC (priv), S_OK);

  decklinksrc = GST_DECKLINK_SRC (priv);

  // Handle Video Frame
  if (videoFrame) {
    if (videoFrame->GetFlags () & bmdFrameHasNoInputSource) {
      GST_DEBUG ("Frame received - No input signal detected");
    } else {
      const char *timecodeString = NULL;
      if (g_timecodeFormat != 0) {
        IDeckLinkTimecode *timecode;
        if (videoFrame->GetTimecode (g_timecodeFormat, &timecode) == S_OK) {
          timecode->GetString (&timecodeString);
        }
      }

      GST_DEBUG ("Frame received [%s] - %s - Size: %li bytes",
          timecodeString != NULL ? timecodeString : "No timecode",
          "Valid Frame", videoFrame->GetRowBytes () * videoFrame->GetHeight ());

      if (timecodeString)
        free ((void *) timecodeString);

      g_mutex_lock (decklinksrc->mutex);
      if (decklinksrc->video_frame != NULL) {
        decklinksrc->dropped_frames++;
      } else {
        videoFrame->AddRef ();
        decklinksrc->video_frame = videoFrame;
        if (audioFrame) {
          audioFrame->AddRef ();
          decklinksrc->audio_frame = audioFrame;
        }
      }
      g_cond_signal (decklinksrc->cond);
      g_mutex_unlock (decklinksrc->mutex);
    }
  }
  return S_OK;
}

HRESULT
    DeckLinkCaptureDelegate::VideoInputFormatChanged
    (BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode * mode,
    BMDDetectedVideoInputFormatFlags) {
  GST_ERROR ("moo");
  return S_OK;
}
