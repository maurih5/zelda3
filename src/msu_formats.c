#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "msu_formats.h"

#if defined(_MSC_VER)
#pragma warning(push, 1)
#pragma warning(disable: 4244 4245 4456 4457 4701 4703 4146)
#endif
#define STB_VORBIS_NO_PUSHDATA_API
#include "third_party/stb/stb_vorbis.c"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#include "third_party/dr_libs/dr_flac.h"
#define DR_MP3_IMPLEMENTATION
#include "third_party/dr_libs/dr_mp3.h"
#define DR_WAV_IMPLEMENTATION
#include "third_party/dr_libs/dr_wav.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

const char *const kMsuDecoderExts[] = { "ogg", "flac", "mp3", "wav", NULL };

enum {
  kKind_Vorbis = 1,
  kKind_Flac,
  kKind_Mp3,
  kKind_Wav,
};

enum { kChunk = 1024, kMaxChannels = 8 };

struct MsuDecoder {
  int kind;
  stb_vorbis *vorbis;
  drflac *flac;
  drmp3 mp3;
  drwav wav;
  uint32 rate, channels;
  uint64 loop_start, loop_end;  // in source frames; loop_end 0 = end of file
  uint64 pos;                   // next source frame to read
  uint64 file_size;
  bool just_looped;
  // Source frames read but not yet used by the resampler.
  int16 src[kChunk * 2];
  uint32 src_n, src_pos;
  int16 tmp[kChunk * kMaxChannels];
  // Linear resampler
  bool primed;
  int16 cur[2], nxt[2];
  uint32 frac, out_rate;  // frac / out_rate = position between cur and nxt
};

static int StrCaseCmp(const char *a, const char *b) {
  for (;; a++, b++) {
    int x = (unsigned char)*a, y = (unsigned char)*b;
    if (x >= 'A' && x <= 'Z') x += 32;
    if (y >= 'A' && y <= 'Z') y += 32;
    if (x != y || !x)
      return x - y;
  }
}

static uint64 ParseU64(const char *s) {
  uint64 v = 0;
  while (*s == ' ')
    s++;
  while (*s >= '0' && *s <= '9')
    v = v * 10 + (*s++ - '0');
  return v;
}

// "LOOPSTART=1234" style tags (as used by many game music rips and MSU packs).
static void ParseLoopTag(MsuDecoder *d, const char *s, size_t len, uint64 *length) {
  char buf[64];
  if (len >= sizeof(buf))
    return;
  memcpy(buf, s, len);
  buf[len] = 0;
  char *eq = strchr(buf, '=');
  if (!eq)
    return;
  *eq = 0;
  if (StrCaseCmp(buf, "LOOPSTART") == 0 || StrCaseCmp(buf, "LOOP_START") == 0)
    d->loop_start = ParseU64(eq + 1);
  else if (StrCaseCmp(buf, "LOOPEND") == 0 || StrCaseCmp(buf, "LOOP_END") == 0)
    d->loop_end = ParseU64(eq + 1);
  else if (StrCaseCmp(buf, "LOOPLENGTH") == 0 || StrCaseCmp(buf, "LOOP_LENGTH") == 0)
    *length = ParseU64(eq + 1);
}

static void FlacOnMeta(void *user, drflac_metadata *m) {
  void **args = (void **)user;
  MsuDecoder *d = (MsuDecoder *)args[0];
  if (m->type != DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT)
    return;
  drflac_vorbis_comment_iterator it;
  drflac_init_vorbis_comment_iterator(&it, m->data.vorbis_comment.commentCount, m->data.vorbis_comment.pComments);
  const char *s;
  drflac_uint32 len;
  while ((s = drflac_next_vorbis_comment(&it, &len)) != NULL)
    ParseLoopTag(d, s, len, (uint64 *)args[1]);
}

static bool HasExt(const char *path, const char *ext) {
  const char *dot = strrchr(path, '.');
  return dot && StrCaseCmp(dot + 1, ext) == 0;
}

MsuDecoder *MsuDecoder_Open(const char *path, int out_rate) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  uint64 file_size = (uint64)ftell(f);
  fclose(f);

  MsuDecoder *d = (MsuDecoder *)calloc(1, sizeof(MsuDecoder));
  if (!d)
    return NULL;
  d->file_size = file_size;
  uint64 length = 0;
  if (HasExt(path, "ogg")) {
    int err;
    d->vorbis = stb_vorbis_open_filename(path, &err, NULL);
    if (!d->vorbis)
      goto FAIL;
    d->kind = kKind_Vorbis;
    stb_vorbis_info info = stb_vorbis_get_info(d->vorbis);
    d->rate = info.sample_rate;
    d->channels = info.channels;
    stb_vorbis_comment c = stb_vorbis_get_comment(d->vorbis);
    for (int i = 0; i < c.comment_list_length; i++)
      ParseLoopTag(d, c.comment_list[i], strlen(c.comment_list[i]), &length);
  } else if (HasExt(path, "flac")) {
    void *args[2] = { d, &length };
    d->flac = drflac_open_file_with_metadata(path, FlacOnMeta, args, NULL);
    if (!d->flac)
      goto FAIL;
    d->kind = kKind_Flac;
    d->rate = d->flac->sampleRate;
    d->channels = d->flac->channels;
  } else if (HasExt(path, "mp3")) {
    if (!drmp3_init_file(&d->mp3, path, NULL))
      goto FAIL;
    d->kind = kKind_Mp3;
    d->rate = d->mp3.sampleRate;
    d->channels = d->mp3.channels;
  } else if (HasExt(path, "wav")) {
    if (!drwav_init_file_with_metadata(&d->wav, path, 0, NULL))
      goto FAIL;
    d->kind = kKind_Wav;
    d->rate = d->wav.sampleRate;
    d->channels = d->wav.channels;
    for (uint32 i = 0; i < d->wav.metadataCount; i++) {
      drwav_metadata *m = &d->wav.pMetadata[i];
      if (m->type == drwav_metadata_type_smpl && m->data.smpl.sampleLoopCount) {
        d->loop_start = m->data.smpl.pLoops[0].firstSampleOffset;
        d->loop_end = (uint64)m->data.smpl.pLoops[0].lastSampleOffset + 1;  // inclusive in the file
        break;
      }
    }
  } else {
    goto FAIL;
  }
  if (d->channels < 1 || d->channels > kMaxChannels || d->rate < 8000 || d->rate > 192000) {
    fprintf(stderr, "MSU: unsupported audio format in %s (%d channels, %d Hz)\n", path, d->channels, d->rate);
    MsuDecoder_Close(d);
    return NULL;
  }
  if (length)
    d->loop_end = d->loop_start + length;
  if (d->loop_end && d->loop_end <= d->loop_start)
    d->loop_end = 0;
  d->out_rate = out_rate;
  return d;
FAIL:
  fprintf(stderr, "MSU: unable to decode %s\n", path);
  free(d);
  return NULL;
}

void MsuDecoder_Close(MsuDecoder *d) {
  if (!d)
    return;
  switch (d->kind) {
  case kKind_Vorbis: stb_vorbis_close(d->vorbis); break;
  case kKind_Flac: drflac_close(d->flac); break;
  case kKind_Mp3: drmp3_uninit(&d->mp3); break;
  case kKind_Wav: drwav_uninit(&d->wav); break;
  }
  free(d);
}

static bool SeekRaw(MsuDecoder *d, uint64 frame) {
  bool ok = false;
  switch (d->kind) {
  case kKind_Vorbis: ok = stb_vorbis_seek(d->vorbis, (unsigned)frame) != 0; break;
  case kKind_Flac: ok = drflac_seek_to_pcm_frame(d->flac, frame) != 0; break;
  case kKind_Mp3: ok = drmp3_seek_to_pcm_frame(&d->mp3, frame) != 0; break;
  case kKind_Wav: ok = drwav_seek_to_pcm_frame(&d->wav, frame) != 0; break;
  }
  if (ok)
    d->pos = frame;
  return ok;
}

// Reads up to |n| (<= kChunk) source frames as stereo.
static uint32 ReadRaw(MsuDecoder *d, int16 *out, uint32 n) {
  uint32 r = 0, ch = d->channels;
  if (d->kind == kKind_Vorbis)
    return stb_vorbis_get_samples_short_interleaved(d->vorbis, 2, out, n * 2);
  switch (d->kind) {
  case kKind_Flac: r = (uint32)drflac_read_pcm_frames_s16(d->flac, n, d->tmp); break;
  case kKind_Mp3: r = (uint32)drmp3_read_pcm_frames_s16(&d->mp3, n, d->tmp); break;
  case kKind_Wav: r = (uint32)drwav_read_pcm_frames_s16(&d->wav, n, d->tmp); break;
  }
  for (uint32 i = 0; i < r; i++) {
    out[i * 2 + 0] = d->tmp[i * ch];
    out[i * 2 + 1] = d->tmp[i * ch + (ch > 1)];
  }
  return r;
}

// Fills the source buffer, following the loop. Returns false at the end.
static bool RefillSource(MsuDecoder *d, bool loop) {
  for (int tries = 0; tries < 4; tries++) {
    uint64 end = d->loop_end ? d->loop_end : ~(uint64)0;
    uint32 r = 0;
    if (d->pos < end) {
      uint64 want = end - d->pos;
      r = ReadRaw(d, d->src, want < kChunk ? (uint32)want : kChunk);
    }
    if (r) {
      d->pos += r;
      d->src_n = r;
      d->src_pos = 0;
      d->just_looped = false;
      return true;
    }
    // End of the file or of the loop.
    if (!loop || d->just_looped || !SeekRaw(d, d->loop_start))
      return false;
    d->just_looped = true;
  }
  return false;
}

static bool NextSourceFrame(MsuDecoder *d, int16 *lr, bool loop) {
  if (d->src_pos == d->src_n && !RefillSource(d, loop))
    return false;
  lr[0] = d->src[d->src_pos * 2 + 0];
  lr[1] = d->src[d->src_pos * 2 + 1];
  d->src_pos++;
  return true;
}

uint32 MsuDecoder_Read(MsuDecoder *d, int16 *out, uint32 frames, bool loop) {
  if (!d->primed) {
    if (!NextSourceFrame(d, d->cur, loop))
      return 0;
    if (!NextSourceFrame(d, d->nxt, loop))
      memcpy(d->nxt, d->cur, sizeof(d->cur));
    d->frac = 0;
    d->primed = true;
  }
  uint32 i;
  for (i = 0; i < frames; i++) {
    while (d->frac >= d->out_rate) {
      memcpy(d->cur, d->nxt, sizeof(d->cur));
      if (!NextSourceFrame(d, d->nxt, loop))
        return i;
      d->frac -= d->out_rate;
    }
    int f = (int)(((uint64)d->frac << 16) / d->out_rate);
    out[i * 2 + 0] = (int16)(d->cur[0] + ((d->nxt[0] - d->cur[0]) * f >> 16));
    out[i * 2 + 1] = (int16)(d->cur[1] + ((d->nxt[1] - d->cur[1]) * f >> 16));
    d->frac += d->rate;
  }
  return i;
}

uint64 MsuDecoder_Tell(MsuDecoder *d) {
  // The frame in |cur| is the one being played.
  uint64 buffered = d->src_n - d->src_pos + (d->primed ? 2 : 0);
  return d->pos >= buffered ? d->pos - buffered : 0;
}

bool MsuDecoder_Seek(MsuDecoder *d, uint64 frame) {
  d->src_n = d->src_pos = 0;
  d->primed = false;
  d->just_looped = false;
  return SeekRaw(d, frame);
}

uint32 MsuDecoder_Tag(MsuDecoder *d) {
  static const char kTags[][4] = { "OGGV", "FLAC", "MP3 ", "WAVE" };
  const char *t = kTags[d->kind - 1];
  return t[0] | t[1] << 8 | t[2] << 16 | (uint32)t[3] << 24;
}

uint64 MsuDecoder_Fingerprint(MsuDecoder *d) {
  return d->file_size ^ ((uint64)d->rate << 40) ^ ((uint64)d->channels << 60);
}
