#ifndef _PORTSPIKE_THEORADEC_H
#define _PORTSPIKE_THEORADEC_H
// Engine true-port feasibility spike (Task #194/#198).
//
// RetroEngine.hpp:564 includes <theora/theoradec.h> UNCONDITIONALLY (before the
// engine header block), so every core TU pulls it in transitively. Video.hpp:7-23
// then declares struct VideoManager with STATIC members of concrete libogg /
// libtheora types (ogg_sync_state oy; ogg_page og; ... th_dec_ctx *td; ...).
//
// On Saturn, Theora decode is INFEASIBLE on the 26.8 MHz SH-2 -- the same CPU-
// budget wall that ruled out RSDK's software rasterizer (Probe 3: 11 instr/opaque
// px, 788,480 cyc floor vs 446,667 cyc budget). FMV on Saturn is Cinepak via SBL
// + SCSP, so libtheora is DROPPED. Video.cpp (the only TU that DEREFERENCES these
// members) is therefore NOT in the core compile set; the core only needs the
// TYPE-SURFACE so the VideoManager static-member DECLARATIONS typecheck. This is
// the FMV analogue of replacing Drawing.cpp's rasterizer with the VDP1/VDP2
// device backend.
//
// Types mirror the real libogg <ogg/ogg.h> and libtheora <theora/codec.h>:
// the by-value structs are concrete (libogg's are public value types); the two
// decoder contexts are opaque (libtheora forward-declares th_dec_ctx /
// th_setup_info and only ever hands out pointers); th_pixel_fmt is the real
// enum; ogg_int64_t is libogg's 64-bit integer (os_types.h -> long long on a
// 32-bit ILP32 target like SH-2).

typedef long long          ogg_int64_t;
typedef unsigned long long ogg_uint64_t;
typedef int                ogg_int32_t;
typedef unsigned int       ogg_uint32_t;
typedef short              ogg_int16_t;
typedef unsigned short     ogg_uint16_t;

// --- libogg public value types (<ogg/ogg.h>) -------------------------------
typedef struct {
    long  endbyte;
    int   endbit;
    unsigned char *buffer;
    unsigned char *ptr;
    long  storage;
} oggpack_buffer;

typedef struct {
    unsigned char *header;
    long           header_len;
    unsigned char *body;
    long           body_len;
} ogg_page;

typedef struct {
    unsigned char *body_data;
    long           body_storage;
    long           body_fill;
    long           body_returned;
    int           *lacing_vals;
    ogg_int64_t   *granule_vals;
    long           lacing_storage;
    long           lacing_fill;
    long           lacing_packet;
    long           lacing_returned;
    unsigned char  header[282];
    int            header_fill;
    int            e_o_s;
    int            b_o_s;
    long           serialno;
    long           pageno;
    ogg_int64_t    packetno;
    ogg_int64_t    granulepos;
} ogg_stream_state;

typedef struct {
    unsigned char *packet;
    long           bytes;
    long           b_o_s;
    long           e_o_s;
    ogg_int64_t    granulepos;
    ogg_int64_t    packetno;
} ogg_packet;

typedef struct {
    unsigned char *data;
    int            storage;
    int            fill;
    int            returned;
    int            unsynced;
    int            headerbytes;
    int            bodybytes;
} ogg_sync_state;

// --- libtheora types (<theora/codec.h>) ------------------------------------
typedef enum {
    TH_PF_420,
    TH_PF_RSVD,
    TH_PF_422,
    TH_PF_444,
    TH_PF_NFORMATS
} th_pixel_fmt;

typedef struct {
    unsigned char  version_major;
    unsigned char  version_minor;
    unsigned char  version_subminor;
    int            frame_width;
    int            frame_height;
    int            pic_width;
    int            pic_height;
    int            pic_x;
    int            pic_y;
    ogg_uint32_t   fps_numerator;
    ogg_uint32_t   fps_denominator;
    ogg_uint32_t   aspect_numerator;
    ogg_uint32_t   aspect_denominator;
    int            colorspace;
    th_pixel_fmt   pixel_fmt;
    int            target_bitrate;
    int            quality;
    int            keyframe_granule_shift;
} th_info;

typedef struct {
    char         **user_comments;
    int           *comment_lengths;
    int            comments;
    char          *vendor;
} th_comment;

// Opaque decoder contexts -- libtheora only ever exposes pointers.
typedef struct th_dec_ctx    th_dec_ctx;
typedef struct th_setup_info th_setup_info;

#endif // _PORTSPIKE_THEORADEC_H
