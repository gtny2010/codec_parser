#include "video_parser.h"

//---------------------------------------------------------------------------------------
//            h263 parser end
//----------------------------------------------------------------------------------------
/* Most is hardcoded; should extend to handle all H.263 streams. */
int ff_h263_decode_data(ParseContext *pc, H263Packet *pkt, const uint8_t *buf,
                        int buf_size) {
  return ff_h263_packet_split_and_parse(pc, pkt, buf, buf_size);
}

//---------------------------------------------------------------------------------------
//            h264 parser end
//----------------------------------------------------------------------------------------
static int decode_extradata_ps(const uint8_t *data, int size, H264ParamSets *ps,
                               int is_avc, void *logctx) {
  H2645Packet pkt = {0};
  int i, ret = 0;

  ret = ff_h2645_packet_split(&pkt, data, size, logctx, is_avc, 2,
                              AV_CODEC_ID_H264, 1, 0);
  if (ret < 0) {
    ret = 0;
    goto fail;
  }

  for (i = 0; i < pkt.nb_nals; i++) {
    H2645NAL *nal = &pkt.nals[i];
    switch (nal->type) {
    case H264_NAL_SPS: {
      GetBitContext tmp_gb = nal->gb;
      ret = ff_h264_decode_seq_parameter_set(&tmp_gb, ps, 0);
      if (ret >= 0)
        break;
      printf("SPS decoding failure, trying again with the complete NAL\n");
      init_get_bits8(&tmp_gb, nal->raw_data + 1, nal->raw_size - 1);
      ret = ff_h264_decode_seq_parameter_set(&tmp_gb, ps, 0);
      if (ret >= 0)
        break;
      ret = ff_h264_decode_seq_parameter_set(&nal->gb, ps, 1);
      if (ret < 0)
        goto fail;
      break;
    }
    case H264_NAL_PPS:
      ret = ff_h264_decode_picture_parameter_set(&nal->gb, ps, nal->size_bits);
      if (ret < 0)
        goto fail;
      break;
    default:
      printf("Ignoring NAL type %d in extradata\n", nal->type);
      break;
    }
  }

fail:
  ff_h2645_packet_uninit(&pkt);
  return ret;
}

/* There are (invalid) samples in the wild with mp4-style extradata, where the
 * parameter sets are stored unescaped (i.e. as RBSP).
 * This function catches the parameter set decoding failure and tries again
 * after escaping it */
static int decode_extradata_ps_mp4(const uint8_t *buf, int buf_size,
                                   H264ParamSets *ps, int err_recognition,
                                   void *logctx) {
  int ret;

  ret = decode_extradata_ps(buf, buf_size, ps, 1, logctx);
  if (ret < 0 /*&& !(err_recognition & AV_EF_EXPLODE)*/) {
    GetByteContext gbc;
    PutByteContext pbc;
    uint8_t *escaped_buf;
    int escaped_buf_size;

    printf("SPS decoding failure, trying again after escaping the NAL\n");

    if (buf_size / 2 >= (INT16_MAX - AV_INPUT_BUFFER_PADDING_SIZE) / 3)
      return AVERROR(ERANGE);
    escaped_buf_size = buf_size * 3 / 2 + AV_INPUT_BUFFER_PADDING_SIZE;
    escaped_buf = av_mallocz(escaped_buf_size);
    if (!escaped_buf)
      return AVERROR(ENOMEM);

    bytestream2_init(&gbc, buf, buf_size);
    bytestream2_init_writer(&pbc, escaped_buf, escaped_buf_size);

    while (bytestream2_get_bytes_left(&gbc)) {
      if (bytestream2_get_bytes_left(&gbc) >= 3 &&
          bytestream2_peek_be24(&gbc) <= 3) {
        bytestream2_put_be24(&pbc, 3);
        bytestream2_skip(&gbc, 2);
      } else
        bytestream2_put_byte(&pbc, bytestream2_get_byte(&gbc));
    }

    escaped_buf_size = bytestream2_tell_p(&pbc);
    AV_WB16(escaped_buf, escaped_buf_size - 2);

    (void)decode_extradata_ps(escaped_buf, escaped_buf_size, ps, 1, logctx);
    // lorex.mp4 decodes ok even with extradata decoding failing
    av_freep(&escaped_buf);
  }

  return 0;
}

int ff_h264_decode_extradata(const uint8_t *data, int size, H264ParamSets *ps,
                             int *is_avc, int *nal_length_size,
                             int err_recognition, void *logctx) {
  int ret;

  if (!data || size <= 0)
    return -1;

  if (data[0] == 1) {
    int i, cnt, nalsize;
    const uint8_t *p = data;

    *is_avc = 1;

    if (size < 7) {
      printf("avcC %d too short\n", size);
      return AVERROR_INVALIDDATA;
    }

    // Decode sps from avcC
    cnt = *(p + 5) & 0x1f; // Number of sps
    p += 6;
    for (i = 0; i < cnt; i++) {
      nalsize = AV_RB16(p) + 2;
      if (nalsize > size - (p - data))
        return AVERROR_INVALIDDATA;
      ret = decode_extradata_ps_mp4(p, nalsize, ps, err_recognition, logctx);
      if (ret < 0) {
        printf("Decoding sps %d from avcC failed\n", i);
        return ret;
      }
      p += nalsize;
    }
    // Decode pps from avcC
    cnt = *(p++); // Number of pps
    for (i = 0; i < cnt; i++) {
      nalsize = AV_RB16(p) + 2;
      if (nalsize > size - (p - data))
        return AVERROR_INVALIDDATA;
      ret = decode_extradata_ps_mp4(p, nalsize, ps, err_recognition, logctx);
      if (ret < 0) {
        printf("Decoding pps %d from avcC failed\n", i);
        return ret;
      }
      p += nalsize;
    }
    // Store right nal length size that will be used to parse all other nals
    *nal_length_size = (data[4] & 0x03) + 1;
  } else {
    *is_avc = 0;
    ret = decode_extradata_ps(data, size, ps, 0, logctx);
    if (ret < 0)
      return ret;
  }
  return size;
}

//---------------------------------------------------------------------------------------
//            h265 parser end
//----------------------------------------------------------------------------------------

static int hevc_decode_nal_units(const uint8_t *buf, int buf_size,
                                 HEVCParamSets *ps, HEVCSEI *sei, int is_nalff,
                                 int nal_length_size, int err_recognition,
                                 int apply_defdispwin, void *logctx) {
  int i;
  int ret = 0;
  H2645Packet pkt = {0};

  ret = ff_h2645_packet_split(&pkt, buf, buf_size, logctx, is_nalff,
                              nal_length_size, AV_CODEC_ID_HEVC, 1, 0);
  if (ret < 0) {
    goto done;
  }

  for (i = 0; i < pkt.nb_nals; i++) {
    H2645NAL *nal = &pkt.nals[i];
    if (nal->nuh_layer_id > 0)
      continue;

    /* ignore everything except parameter sets and VCL NALUs */
    switch (nal->type) {
    case HEVC_NAL_VPS:
      ret = ff_hevc_decode_nal_vps(&nal->gb, ps);
      if (ret < 0)
        goto done;
      break;
    case HEVC_NAL_SPS:
      ret = ff_hevc_decode_nal_sps(&nal->gb, ps, apply_defdispwin);
      if (ret < 0)
        goto done;
      break;
    case HEVC_NAL_PPS:
      ret = ff_hevc_decode_nal_pps(&nal->gb, ps);
      if (ret < 0)
        goto done;
      break;
    case HEVC_NAL_SEI_PREFIX:
    case HEVC_NAL_SEI_SUFFIX:
      ret = ff_hevc_decode_nal_sei(&nal->gb, logctx, sei, ps, nal->type);
      if (ret < 0)
        goto done;
      break;
    default:
      printf("Ignoring NAL type %d in extradata\n", nal->type);
      break;
    }
  }

done:
  ff_h2645_packet_uninit(&pkt);
  // if (err_recognition & AV_EF_EXPLODE)
  //     return ret;

  return 0;
}

int ff_hevc_decode_extradata(const uint8_t *data, int size, HEVCParamSets *ps,
                             HEVCSEI *sei, int *is_nalff, int *nal_length_size,
                             int err_recognition, int apply_defdispwin,
                             void *logctx) {
  int ret = 0;
  GetByteContext gb;

  bytestream2_init(&gb, data, size);

  if (size > 3 && (data[0] || data[1] || data[2] > 1)) {
    /* It seems the extradata is encoded as hvcC format.
     * Temporarily, we support configurationVersion==0 until 14496-15 3rd
     * is finalized. When finalized, configurationVersion will be 1 and we
     * can recognize hvcC by checking if avctx->extradata[0]==1 or not. */
    int i, j, num_arrays, nal_len_size;

    *is_nalff = 1;

    bytestream2_skip(&gb, 21);
    nal_len_size = (bytestream2_get_byte(&gb) & 3) + 1;
    num_arrays = bytestream2_get_byte(&gb);

    /* nal units in the hvcC always have length coded with 2 bytes,
     * so put a fake nal_length_size = 2 while parsing them */
    *nal_length_size = 2;

    /* Decode nal units from hvcC. */
    for (i = 0; i < num_arrays; i++) {
      int type = bytestream2_get_byte(&gb) & 0x3f;
      int cnt = bytestream2_get_be16(&gb);

      for (j = 0; j < cnt; j++) {
        // +2 for the nal size field
        int nalsize = bytestream2_peek_be16(&gb) + 2;
        if (bytestream2_get_bytes_left(&gb) < nalsize) {
          printf("Invalid NAL unit size in extradata.\n");
          return AVERROR_INVALIDDATA;
        }

        ret = hevc_decode_nal_units(gb.buffer, nalsize, ps, sei, *is_nalff,
                                    *nal_length_size, err_recognition,
                                    apply_defdispwin, logctx);
        if (ret < 0) {
          printf("Decoding nal unit %d %d from hvcC failed\n", type, i);
          return ret;
        }
        bytestream2_skip(&gb, nalsize);
      }
    }

    /* Now store right nal length size, that will be used to parse
     * all other nals */
    *nal_length_size = nal_len_size;
  } else {
    *is_nalff = 0;
    ret =
        hevc_decode_nal_units(data, size, ps, sei, *is_nalff, *nal_length_size,
                              err_recognition, apply_defdispwin, logctx);
    if (ret < 0)
      return ret;
  }

  return ret;
}