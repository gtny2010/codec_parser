#ifndef AVUTIL_THREAD_H
#define AVUTIL_THREAD_H

#include "bytestream.h"
#include "get_bits.h"
#include "h263.h"
#include "h2645_parse.h"
#include "h264_ps.h"
#include "hevc_ps.h"
#include "hevc_sei.h"
#include "mem.h"

int ff_h263_decode_data(ParseContext *pc, H263Packet *pkt, const uint8_t *buf,
                        int buf_size);

int ff_h264_decode_extradata(const uint8_t *data, int size, H264ParamSets *ps,
                             int *is_avc, int *nal_length_size,
                             int err_recognition, void *logctx);

int ff_hevc_decode_extradata(const uint8_t *data, int size, HEVCParamSets *ps,
                             HEVCSEI *sei, int *is_nalff, int *nal_length_size,
                             int err_recognition, int apply_defdispwin,
                             void *logctx);
#endif