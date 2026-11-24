#ifndef ZELDA3_MSU_FORMATS_H_
#define ZELDA3_MSU_FORMATS_H_

#include "types.h"

// Decoders for MSU tracks stored as regular audio files (OGG Vorbis, FLAC,
// MP3, WAV), resampled to the output rate. The loop point is read from the
// LOOPSTART (and LOOPEND / LOOPLENGTH) tags of OGG and FLAC files, or from
// the "smpl" chunk of WAV files; otherwise looping tracks start over.

typedef struct MsuDecoder MsuDecoder;

// Extensions tried, in order, after the native .pcm / .opuz files.
extern const char *const kMsuDecoderExts[];

// Returns NULL if the file doesn't exist or can't be decoded.
MsuDecoder *MsuDecoder_Open(const char *path, int out_rate);
void MsuDecoder_Close(MsuDecoder *d);
// Writes up to |frames| stereo frames at the output rate. When the end of the
// track (or of the loop) is reached it continues from the loop point if
// |loop| is set, otherwise it returns fewer frames (0 = finished).
uint32 MsuDecoder_Read(MsuDecoder *d, int16 *out, uint32 frames, bool loop);
// Position in source frames (for resuming), and seeking to one.
uint64 MsuDecoder_Tell(MsuDecoder *d);
bool MsuDecoder_Seek(MsuDecoder *d, uint64 frame);
// Identifies the file, to check that a resume position belongs to it.
uint32 MsuDecoder_Tag(MsuDecoder *d);
uint64 MsuDecoder_Fingerprint(MsuDecoder *d);

#endif  // ZELDA3_MSU_FORMATS_H_
