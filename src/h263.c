/*
 * H.263/MPEG-4 backend for encoder and decoder
 * Copyright (c) 2000,2001 Fabrice Bellard
 * H.263+ support.
 * Copyright (c) 2001 Juan J. Sierralta P
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * H.263/MPEG-4 codec.
 */

#include "h263.h"
#include "bytestream.h"
#include "h263data.h"
#include "thread.h"

#define END_NOT_FOUND (-100)

int ff_h263_find_frame_end(ParseContext *pc, const uint8_t *buf, int buf_size) {
  int vop_found, i;
  uint32_t state;

  vop_found = pc->frame_start_found;
  state = pc->state;

  i = 0;
  if (!vop_found) {
    for (i = 0; i < buf_size; i++) {
      state = (state << 8) | buf[i];
      if (state >> (32 - 22) == 0x20) {
        i++;
        vop_found = 1;
        break;
      }
    }
  }

  if (vop_found) {
    for (; i < buf_size; i++) {
      state = (state << 8) | buf[i];
      if (state >> (32 - 22) == 0x20) {
        pc->frame_start_found = 0;
        pc->state = -1;
        return i - 3;
      }
    }
  }
  pc->frame_start_found = vop_found;
  pc->state = state;

  return END_NOT_FOUND;
}

int ff_combine_frame(ParseContext *pc, int next, const uint8_t *buf,
                     int buf_size) {
  if (pc->overread) {
    printf("overread %d, state:%" PRIX32 " next:%d index:%d o_index:%d\n",
           pc->overread, pc->state, next, pc->index, pc->overread_index);
    printf("%X %X %X %X\n", (buf)[0], (buf)[1], (buf)[2], (buf)[3]);
  }

  /* Copy overread bytes from last frame into buffer. */
  for (; pc->overread > 0; pc->overread--)
    pc->buffer[pc->index++] = pc->buffer[pc->overread_index++];

  if (next > buf_size)
    return AVERROR(EINVAL);

  /* flush remaining if EOF */
  if (!buf_size && next == END_NOT_FOUND)
    next = 0;

  pc->last_index = pc->index;

  /* copy into buffer end return */
  if (next == END_NOT_FOUND) {
    void *new_buffer = NULL;
    if (pc->buffer_size < buf_size + pc->index + AV_INPUT_BUFFER_PADDING_SIZE) {
      pc->buffer_size = buf_size + pc->index + AV_INPUT_BUFFER_PADDING_SIZE;
      new_buffer = realloc(pc->buffer, pc->buffer_size);
    } else {
      new_buffer = pc->buffer;
    }

    if (!new_buffer) {
      printf("Failed to reallocate parser buffer to %d\n",
             buf_size + pc->index + AV_INPUT_BUFFER_PADDING_SIZE);
      pc->index = 0;
      return AVERROR(ENOMEM);
    }
    pc->buffer = new_buffer;
    memcpy(pc->buffer + pc->index, buf, buf_size);
    pc->index += buf_size;
    return -1;
  }

  av_assert0(next >= 0 || pc->buffer);

  pc->overread_index = pc->index + next;

  /* append to buffer */
  if (pc->index >= 0) {
    void *new_buffer = NULL;
    if (pc->buffer_size < next + pc->index + AV_INPUT_BUFFER_PADDING_SIZE) {
      pc->buffer_size = next + pc->index + AV_INPUT_BUFFER_PADDING_SIZE;
      new_buffer = realloc(pc->buffer, pc->buffer_size);
    } else {
      new_buffer = pc->buffer;
    }

    if (!new_buffer) {
      printf("Failed to reallocate parser buffer to %d\n",
             next + pc->index + AV_INPUT_BUFFER_PADDING_SIZE);
      pc->overread_index = pc->index = 0;
      return AVERROR(ENOMEM);
    }
    pc->buffer = new_buffer;

    if (next > 0)
      memcpy(pc->buffer + pc->index, buf, next);

    /*reset*/
    pc->index = 0;
    // *buf = pc->buffer;
  }

  /*Tell me, why?*/
  // if (next < -8) {
  //   pc->overread += -8 - next;
  //   next = -8;
  // }

  /* store overread bytes */
  // for (; next < 0; next++) {
  //   pc->state = pc->state << 8 | pc->buffer[pc->last_index + next];
  //   pc->state64 = pc->state64 << 8 | pc->buffer[pc->last_index + next];
  //   pc->overread++;
  // }

  // if (pc->overread) {
  //   printf("overread %d, state:%" PRIX32 " next:%d index:%d o_index:%d\n",
  //          pc->overread, pc->state, next, pc->index, pc->overread_index);
  //   printf("%X %X %X %X\n", (buf)[0], (buf)[1], (buf)[2], (buf)[3]);
  // }

  return 0;
}

static char av_get_picture_type_char(enum AVPictureType pict_type) {
  switch (pict_type) {
  case AV_PICTURE_TYPE_I:
    return 'I';
  case AV_PICTURE_TYPE_P:
    return 'P';
  case AV_PICTURE_TYPE_B:
    return 'B';
  case AV_PICTURE_TYPE_S:
    return 'S';
  case AV_PICTURE_TYPE_SI:
    return 'i';
  case AV_PICTURE_TYPE_SP:
    return 'p';
  case AV_PICTURE_TYPE_BI:
    return 'b';
  default:
    return '?';
  }
}

static void ff_h263_show_pict_info(H263Packet *pic) {
  int debug = 1;
  H263Pic *s = &pic->picture;
  if (debug) {
    printf("qp:%d %c size:%d rnd:%d%s%s%s%s%s%s%s%s%s %d/%d\n", s->qscale,
           av_get_picture_type_char(s->pict_type), s->gb.size_in_bits,
           1 - s->no_rounding, s->obmc ? " AP" : "", s->umvplus ? " UMV" : "",
           s->h263_long_vectors ? " LONG" : "", s->h263_plus ? " +" : "",
           s->h263_aic ? " AIC" : "", s->alt_inter_vlc ? " AIV" : "",
           s->modified_quant ? " MQ" : "", s->loop_filter ? " LOOP" : "",
           s->h263_slice_structured ? " SS" : "", s->framerate.num,
           s->framerate.den);
  }
}

static int ff_h263_decode_mba(H263Pic *s) {
  int i, mb_pos;

  for (i = 0; i < 6; i++)
    if (s->mb_num - 1 <= ff_mba_max[i])
      break;
  mb_pos = get_bits(&s->gb, ff_mba_length[i]);
  s->mb_x = mb_pos % s->mb_width;
  s->mb_y = mb_pos / s->mb_width;

  return mb_pos;
}

/* Most is hardcoded; should extend to handle all H.263 streams. */
int ff_h263_decode_picture_header(H263Packet *s) {
  int format, width, height, i, ret;
  uint32_t startcode;

  GetBitContext *gb;
  H263Pic *pic;
  gb = &s->picture.gb;
  pic = &s->picture;

  align_get_bits(gb);

  if (show_bits(gb, 2) == 2) {
    printf("Header looks like RTP instead of H.263\n");
  }

  startcode = get_bits(gb, 22 - 8);

  for (i = get_bits_left(gb); i > 24; i -= 8) {
    startcode = ((startcode << 8) | get_bits(gb, 8)) & 0x003FFFFF;

    if (startcode == 0x20)
      break;
  }

  if (startcode != 0x20) {
    printf("Bad picture start code\n");
    return -1;
  }

  /* temporal reference */
  i = get_bits(gb, 8); /* picture timestamp */

  // i -= (i - (s->picture_number & 0xFF) + 128) & ~0xFF;

  // s->picture_number= (s->picture_number&~0xFF) + i;

  /* PTYPE starts here */
  if (check_marker(NULL, gb, "in PTYPE") != 1) {
    return -1;
  }
  if (get_bits1(gb) != 0) {
    printf("Bad H.263 id\n");
    return -1; /* H.263 id */
  }
  skip_bits1(gb); /* split screen off */
  skip_bits1(gb); /* camera  off */
  skip_bits1(gb); /* freeze picture release off */

  format = get_bits(gb, 3);
  /*
      0    forbidden
      1    sub-QCIF
      10   QCIF
      7       extended PTYPE (PLUSPTYPE)
  */

  if (format != 7 && format != 6) {
    /* H.263v1 */
    width = ff_h263_format[format][0];
    height = ff_h263_format[format][1];
    if (!width)
      return -1;

    pic->pict_type = AV_PICTURE_TYPE_I + get_bits1(gb);

    pic->h263_long_vectors = get_bits1(gb);

    if (get_bits1(gb) != 0) {
      printf("H.263 SAC not supported\n");
      return -1; /* SAC: off */
    }
    pic->obmc = get_bits1(gb); /* Advanced prediction mode */
    pic->unrestricted_mv = pic->h263_long_vectors || pic->obmc;

    pic->pb_frame = get_bits1(gb);
    pic->chroma_qscale = pic->qscale = get_bits(gb, 5);
    skip_bits1(gb); /* Continuous Presence Multipoint mode: off */

    pic->width = width;
    pic->height = height;
    pic->framerate = (AVRational){30000, 1001};
  } else {
    int ufep;

    /* H.263v2 */
    pic->h263_plus = 1;
    ufep = get_bits(gb, 3); /* Update Full Extended PTYPE */

    /* ufep other than 0 and 1 are reserved */
    if (ufep == 1) {
      /* OPPTYPE */
      format = get_bits(gb, 3);
      printf("ufep=1, format: %d\n", format);
      pic->custom_pcf = get_bits1(gb);
      pic->umvplus = get_bits1(gb); /* Unrestricted Motion Vector */
      if (get_bits1(gb) != 0) {
        printf("Syntax-based Arithmetic Coding (SAC) not supported\n");
      }
      pic->obmc = get_bits1(gb);     /* Advanced prediction mode */
      pic->h263_aic = get_bits1(gb); /* Advanced Intra Coding (AIC) */
      pic->loop_filter = get_bits1(gb);
      pic->unrestricted_mv = pic->umvplus || pic->obmc || pic->loop_filter;

      pic->h263_slice_structured = get_bits1(gb);
      if (get_bits1(gb) != 0) {
        printf("Reference Picture Selection not supported\n");
      }
      if (get_bits1(gb) != 0) {
        printf("Independent Segment Decoding not supported\n");
      }
      pic->alt_inter_vlc = get_bits1(gb);
      pic->modified_quant = get_bits1(gb);
      if (pic->modified_quant)
        pic->chroma_qscale_table = ff_h263_chroma_qscale_table;

      skip_bits(gb, 1); /* Prevent start code emulation */

      skip_bits(gb, 3); /* Reserved */
    } else if (ufep != 0) {
      printf("Bad UFEP type (%d)\n", ufep);
      return -1;
    }

    /* MPPTYPE */
    pic->pict_type = get_bits(gb, 3);
    switch (pic->pict_type) {
    case 0:
      pic->pict_type = AV_PICTURE_TYPE_I;
      break;
    case 1:
      pic->pict_type = AV_PICTURE_TYPE_P;
      break;
    case 2:
      pic->pict_type = AV_PICTURE_TYPE_P;
      pic->pb_frame = 3;
      break;
    case 3:
      pic->pict_type = AV_PICTURE_TYPE_B;
      break;
    case 7:
      pic->pict_type = AV_PICTURE_TYPE_I;
      break; // ZYGO
    default:
      return -1;
    }
    skip_bits(gb, 2);
    pic->no_rounding = get_bits1(gb);
    skip_bits(gb, 4);

    /* Get the picture dimensions */
    if (ufep) {
      if (format == 6) {
        /* Custom Picture Format (CPFMT) */
        pic->aspect_ratio_info = get_bits(gb, 4);
        printf("aspect: %d\n", pic->aspect_ratio_info);
        /* aspect ratios:
        0 - forbidden
        1 - 1:1
        2 - 12:11 (CIF 4:3)
        3 - 10:11 (525-type 4:3)
        4 - 16:11 (CIF 16:9)
        5 - 40:33 (525-type 16:9)
        6-14 - reserved
        */
        width = (get_bits(gb, 9) + 1) * 4;
        check_marker(NULL, gb, "in dimensions");
        height = get_bits(gb, 9) * 4;
        printf("\nH.263+ Custom picture: %dx%d\n", width, height);
        if (pic->aspect_ratio_info == FF_ASPECT_EXTENDED) {
          /* expected dimensions */
          pic->sample_aspect_ratio.num = get_bits(gb, 8);
          pic->sample_aspect_ratio.den = get_bits(gb, 8);
        } else {
          pic->sample_aspect_ratio =
              ff_h263_pixel_aspect[pic->aspect_ratio_info];
        }
      } else {
        width = ff_h263_format[format][0];
        height = ff_h263_format[format][1];
        pic->sample_aspect_ratio = (AVRational){12, 11};
      }
      pic->sample_aspect_ratio.den <<= pic->ehc_mode;
      if ((width == 0) || (height == 0))
        return -1;
      pic->width = width;
      pic->height = height;

      if (pic->custom_pcf) {
        int gcd;
        pic->framerate.num = 1800000;
        pic->framerate.den = 1000 + get_bits1(gb);
        pic->framerate.den *= get_bits(gb, 7);
        if (pic->framerate.den == 0) {
          printf("zero framerate\n");
          return -1;
        }
        // gcd = av_gcd(pic->framerate.den, pic->framerate.num);
        pic->framerate.den /= gcd;
        pic->framerate.num /= gcd;
      } else {
        pic->framerate = (AVRational){30000, 1001};
      }
    }

    if (pic->custom_pcf) {
      skip_bits(gb, 2); // extended Temporal reference
    }

    if (ufep) {
      if (pic->umvplus) {
        if (get_bits1(gb) ==
            0) /* Unlimited Unrestricted Motion Vectors Indicator (UUI) */
          skip_bits1(gb);
      }
      if (pic->h263_slice_structured) {
        if (get_bits1(gb) != 0) {
          printf("rectangular slices not supported\n");
        }
        if (get_bits1(gb) != 0) {
          printf("unordered slices not supported\n");
        }
      }
      if (pic->pict_type == AV_PICTURE_TYPE_B) {
        skip_bits(gb, 4); // ELNUM
        if (ufep == 1) {
          skip_bits(gb, 4); // RLNUM
        }
      }
    }

    pic->qscale = get_bits(gb, 5);
  } // if

  // if ((ret = av_image_check_size(pic->width, pic->height, 0, s)) < 0)`
  //   return ret;

  int AV_CODEC_FLAG2_CHUNKS = 0;
  if (!(AV_CODEC_FLAG2_CHUNKS)) {
    if ((pic->width * pic->height / 256 / 8) > get_bits_left(gb))
      return AVERROR_INVALIDDATA;
  }

  pic->mb_width = (pic->width + 15) / 16;
  pic->mb_height = (pic->height + 15) / 16;
  pic->mb_num = pic->mb_width * pic->mb_height;

  if (pic->pb_frame) {
    skip_bits(gb, 3); /* Temporal reference for B-pictures */
    if (pic->custom_pcf)
      skip_bits(gb, 2); // extended Temporal reference
    skip_bits(gb, 2);   /* Quantization information for B-pictures */
  }

  if (pic->pict_type != AV_PICTURE_TYPE_B) {
    pic->time = pic->picture_number;
    pic->pp_time = pic->time - pic->last_non_b_time;
    pic->last_non_b_time = pic->time;
  } else {
    pic->time = pic->picture_number;
    pic->pb_time = pic->pp_time - (pic->last_non_b_time - pic->time);
    if (pic->pp_time <= pic->pb_time ||
        pic->pp_time <= pic->pp_time - pic->pb_time || pic->pp_time <= 0) {
      pic->pp_time = 2;
      pic->pb_time = 1;
    }
    // ff_mpeg4_init_direct_mv(s);
  }

  /* PEI */
  if (skip_1stop_8data_bits(gb) < 0)
    return AVERROR_INVALIDDATA;

  if (pic->h263_slice_structured) {
    if (check_marker(NULL, gb, "SEPB1") != 1) {
      return -1;
    }

    ff_h263_decode_mba(&s->picture);

    if (check_marker(NULL, gb, "SEPB2") != 1) {
      return -1;
    }
  }
  // pic->f_code = 1;

  if (pic->pict_type == AV_PICTURE_TYPE_B)
    pic->low_delay = 0;

  if (pic->h263_aic) {
    pic->y_dc_scale_table = pic->c_dc_scale_table = ff_aic_dc_scale_table;
  } else {
    // pic->y_dc_scale_table = pic->c_dc_scale_table = ff_mpeg1_dc_scale_table;
  }

  ff_h263_show_pict_info(s);
  if (pic->pict_type == AV_PICTURE_TYPE_I &&
      pic->codec_tag == AV_RL32("ZYGO") &&
      get_bits_left(gb) >= 85 + 13 * 3 * 16 + 50) {
    int i, j;
    for (i = 0; i < 85; i++)
      printf("%d", get_bits1(gb));
    printf("\n");
    for (i = 0; i < 13; i++) {
      for (j = 0; j < 3; j++) {
        int v = get_bits(gb, 8);
        v |= get_sbits(gb, 8) * (1 << 8);
        printf(" %5d", v);
      }
      printf("\n");
    }
    for (i = 0; i < 50; i++)
      printf("%d", get_bits1(gb));
  }

  return 0;
}

int ff_h263_packet_split_and_parse(ParseContext *pc, H263Packet *pkt,
                                   const uint8_t *buf, int buf_size) {
  GetByteContext bc;
  int consumed, ret = 0;
  int next;
  av_assert0(pc);
  av_assert0(pkt);
  av_assert0(buf);
  av_assert0(buf_size >= 0);

  if (buf_size == 0) {
    next = 0;
    printf("---flush stream---\n");
    goto end;
  }

  next = ff_h263_find_frame_end(pc, buf, buf_size);
  if (ff_combine_frame(pc, next, buf, buf_size) < 0) {
    pkt->picture.data = NULL;
    pkt->picture.size = 0;
    pkt->got_pic = 0;
    return buf_size;
  }

end:
  pkt->picture.data = pc->buffer;
  pkt->picture.size = pc->buffer_size;
  pkt->got_pic = 1;
  init_get_bits(&pkt->picture.gb, pkt->picture.data, pkt->picture.size);
  ff_h263_decode_picture_header(pkt);
  return next;
}