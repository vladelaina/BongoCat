/* stb_vorbis must be declared before miniaudio to enable its Ogg backend. */
#ifndef BONGO_CAT_VORBIS_HEADER
#define BONGO_CAT_VORBIS_HEADER <stb_vorbis.c>
#endif
#define STB_VORBIS_NO_INTEGER_CONVERSION
#define STB_VORBIS_HEADER_ONLY
#include BONGO_CAT_VORBIS_HEADER
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#undef STB_VORBIS_HEADER_ONLY
#include BONGO_CAT_VORBIS_HEADER
