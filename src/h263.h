/*
 * H.263 internal header
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#ifndef AVCODEC_H263_H
#define AVCODEC_H263_H

#include "codec_id.h"
#include "get_bits.h"
#include "h263data.h"
#include "rational.h"
#include <stdint.h>

#define FF_ASPECT_EXTENDED 15
#define INT_BIT (CHAR_BIT * sizeof(int))
#define H263_STREAM_EOS (-1)

typedef struct ParseContext {
  uint8_t *buffer;
  int index;
  int last_index;
  unsigned int buffer_size;
  uint32_t state; ///< contains the last few bytes in MSB order
  int frame_start_found;
  int overread; ///< the number of bytes which where irreversibly read from the
                ///< next frame
  int overread_index; ///< the index into ParseContext.buffer of the overread
                      ///< bytes
  uint64_t state64;   ///< contains the last 8 bytes in MSB order
} ParseContext;

typedef struct H263Pic {
  const uint8_t *data;
  int size;

  GetBitContext gb;

  int y_dc_scale, c_dc_scale;
  int ac_pred;
  int block_last_index[12]; ///< last non zero coefficient in block
  int h263_aic;             ///< Advanced INTRA Coding (AIC)

  const uint8_t *y_dc_scale_table;    ///< qscale -> y_dc_scale table
  const uint8_t *c_dc_scale_table;    ///< qscale -> c_dc_scale table
  const uint8_t *chroma_qscale_table; ///< qscale -> chroma_qscale (H.263)
  uint8_t *coded_block_base;
  uint8_t *coded_block; ///< used for coded block pattern prediction (msmpeg4v3,
                        ///< wmv1)

  /* the following parameters must be initialized before encoding */
  int width, height; ///< picture size. must be a multiple of 16
  int gop_size;
  int intra_only;   ///< if true, only intra pictures are generated
  int64_t bit_rate; ///< wanted bit rate
  int h263_pred;    ///< use MPEG-4/H.263 ac/dc predictions
  int pb_frame;     ///< PB-frame mode (0 = none, 1 = base, 2 = improved)

  int qscale;           ///< QP
  int chroma_qscale;    ///< chroma QP
  unsigned int lambda;  ///< Lagrange multiplier used in rate distortion
  unsigned int lambda2; ///< (lambda*lambda) >> FF_LAMBDA_SHIFT
  int *lambda_table;
  int adaptive_quant; ///< use adaptive quantization
  int dquant;         ///< qscale difference to prev qscale
  int pict_type; ///< AV_PICTURE_TYPE_I, AV_PICTURE_TYPE_P, AV_PICTURE_TYPE_B,
                 ///< ...
  int vbv_delay;
  int last_pict_type;       // FIXME removes
  int last_non_b_pict_type; ///< used for MPEG-4 gmc B-frames & ratecontrol
  int droppable;
  int frame_rate_index;
  AVRational framerate;
  int last_lambda_for[5]; ///< last lambda for a specific pict type
  int skipdct;            ///< skip dct and code zero residual

  /* motion compensation */
  int unrestricted_mv;   ///< mv can point outside of the coded picture
  int h263_long_vectors; ///< use horrible H.263v1 long vector mode

  /* H.263 specific */
  int gob_index;
  int obmc;    ///< overlapped block motion compensation
  int mb_info; ///< interval for outputting info about mb offsets as side data
  int prev_mb_info, last_mb_info;
  uint8_t *mb_info_ptr;
  int mb_info_size;
  int ehc_mode;

  /* H.263+ specific */
  int h263_plus;
  int umvplus;      ///< == H.263+ && unrestricted_mv
  int h263_aic_dir; ///< AIC direction: 0 = left, 1 = top
  int h263_slice_structured;
  int alt_inter_vlc; ///< alternative inter vlc
  int modified_quant;
  int loop_filter;
  int custom_pcf;

  int no_rounding; /**< apply no rounding to motion compensation (MPEG-4,
                      msmpeg4, ...) for B-frames rounding mode is always 0 */

  /* macroblock layer */
  int mb_x, mb_y;
  int mb_skip_run;
  int mb_intra;
  uint16_t *mb_type; ///< Table for candidate MB types for encoding (defines in
                     ///< mpegutils.h)

  int block_index[6]; ///< index to current MB in block based arrays with edges
  int block_wrap[6];
  uint8_t *dest[3];

  int *mb_index2xy; ///< mb_index -> mb_x + mb_y*mb_stride

  /** matrix transmitted in the bitstream */
  uint16_t intra_matrix[64];
  uint16_t chroma_intra_matrix[64];
  uint16_t inter_matrix[64];
  uint16_t chroma_inter_matrix[64];
  int force_duplicated_matrix; ///< Force duplication of mjpeg matrices, useful
                               ///< for rtp streaming

  int intra_quant_bias; ///< bias for the quantizer
  int inter_quant_bias; ///< bias for the quantizer
  int min_qcoeff;       ///< minimum encodable coefficient
  int max_qcoeff;       ///< maximum encodable coefficient
  int ac_esc_length;    ///< num of bits needed to encode the longest esc
  uint8_t *intra_ac_vlc_length;
  uint8_t *intra_ac_vlc_last_length;
  uint8_t *intra_chroma_ac_vlc_length;
  uint8_t *intra_chroma_ac_vlc_last_length;
  uint8_t *inter_ac_vlc_length;
  uint8_t *inter_ac_vlc_last_length;
  uint8_t *luma_dc_vlc_length;
#define UNI_AC_ENC_INDEX(run, level) ((run)*128 + (level))

  int coded_score[12];

  /** precomputed matrix (combine qscale and DCT renorm) */
  int (*q_intra_matrix)[64];
  int (*q_chroma_intra_matrix)[64];
  int (*q_inter_matrix)[64];
  /** identical to the above but for MMX & these are not permutated, second 64
   * entries are bias*/
  uint16_t (*q_intra_matrix16)[2][64];
  uint16_t (*q_chroma_intra_matrix16)[2][64];
  uint16_t (*q_inter_matrix16)[2][64];

  /* noise reduction */
  int (*dct_error_sum)[64];
  int dct_count[2];
  uint16_t (*dct_offset)[64];

  /* bit rate control */
  int64_t total_bits;
  int frame_bits;    ///< bits used for the current frame
  int stuffing_bits; ///< bits used for stuffing
  int next_lambda;   ///< next lambda used for retrying to encode a frame

  /* MPEG-4 specific */
  int studio_profile;
  int dct_precision;
  ///< number of bits to represent the fractional part of time (encoder only)
  int time_increment_bits;
  int last_time_base;
  int time_base; ///< time in seconds of last I,P,S Frame
  int64_t time;  ///< time of current frame
  int64_t last_non_b_time;
  uint16_t pp_time; ///< time distance between the last 2 p,s,i frames
  uint16_t pb_time; ///< time distance between the last b and p,s,i frame
  uint16_t pp_field_time;
  uint16_t pb_field_time; ///< like above, just for interlaced
  int real_sprite_warping_points;
  int sprite_offset[2][2]; ///< sprite offset[isChroma][isMVY]
  int sprite_delta[2][2];  ///< sprite_delta [isY][isMVY]
  int mcsel;
  int quant_precision;
  int quarter_sample;    ///< 1->qpel, 0->half pel ME/MC
  int aspect_ratio_info; // FIXME remove
  int sprite_warping_accuracy;
  int data_partitioning; ///< data partitioning flag from header
  int partitioned_frame; ///< is current frame partitioned
  int low_delay;         ///< no reordering needed / has no B-frames
  int vo_type;

  int mpeg_quant;
  int padding_bug_score; ///< used to detect the VERY common padding bug in
                         ///< MPEG-4

  AVRational sample_aspect_ratio;

  /* sequence parameters */
  int context_initialized;
  int input_picture_number; ///< used to set pic->display_picture_number, should
                            ///< not be used for/by anything else
  int coded_picture_number; ///< used to set pic->coded_picture_number, should
                            ///< not be used for/by anything else
  int picture_number;       // FIXME remove, unclear definition
  int picture_in_gop_number; ///< 0-> first pic in gop, ...
  int mb_width, mb_height;   ///< number of MBs horizontally & vertically
  int mb_stride; ///< mb_width+1 used for some arrays to allow simple addressing
                 ///< of left & top MBs without sig11
  int b8_stride; ///< 2*mb_width+1 used for some 8x8 block arrays to allow
                 ///< simple addressing
  int h_edge_pos, v_edge_pos; ///< horizontal / vertical position of the
                              ///< right/bottom edge (pixel replication)
  int mb_num;                 ///< number of MBs of a picture

  // AVCodecID codec_id; /* see AV_CODEC_ID_xxx */
  int fixed_qscale; ///< fixed qscale if non zero
  int encoding;     ///< true if we are encoding (vs decoding)
  int max_b_frames; ///< max number of B-frames for encoding
  int luma_elim_threshold;
  int chroma_elim_threshold;
  int strict_std_compliance; ///< strictly follow the std (MPEG-4, ...)
  int workaround_bugs; ///< workaround bugs in encoders which cannot be detected
                       ///< automatically
  int codec_tag;       ///< internal codec_tag upper case converted from avctx
                       ///< codec_tag

  /*pkt params*/
  int skipped_bytes;
  int skipped_bytes_pos_size;
  int *skipped_bytes_pos;
} H263Pic;

/* an input packet split into unescaped NAL units */
typedef struct H263Packet {
  H263Pic picture;

  int got_pic;
  unsigned reserved;
} H263Packet;

int ff_h263_packet_split_and_parse(ParseContext *pc, H263Packet *pkt,
                                   const uint8_t *buf, int buf_size);

#endif /* AVCODEC_H263_H */
