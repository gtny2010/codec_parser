#include "video_parser.h"

// /**
//  * Used to parse AVC variant of H.264
//  */
// int is_avc;           ///< this flag is != 0 if codec is avc1
// int nal_length_size;  ///< Number of bytes used for nal length (1, 2 or 4)

// int bit_depth_luma;         ///< luma bit depth from sps to detect changes
// int chroma_format_idc;      ///< chroma format from sps to detect changes

/*****************************************************************************
 * Main functions
 *****************************************************************************/

static void help(const char *exe) { printf("Usage: %s <INPUT> \n", exe); }

int main(int argc, char *argv[]) {
  if (argc <= 1) {
    help(argv[0]);
    return 1;
  }

  // 打开文件
  FILE *fp = fopen(argv[1], "rb");
  if (fp == NULL) {
    perror("Failed to open file");
    exit(EXIT_FAILURE);
  }

  // 获取文件大小
  fseek(fp, 0, SEEK_END);
  int file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  // 读取文件内容到缓冲区
  char *fbuf = (char *)malloc(file_size);
  if (fbuf == NULL) {
    perror("Failed to allocate memory");
    exit(EXIT_FAILURE);
  }
  if (fread(fbuf, file_size, 1, fp) != 1) {
    perror("Failed to read file");
    exit(EXIT_FAILURE);
  }
  int is_avc = 0;
  int nal_length_size = 0;

  // H264ParamSets param;
  // memset(&param, 0, sizeof(param));
  // ff_h264_decode_extradata((const uint8_t *)buf, file_size, &param, &is_avc,
  //                          &nal_length_size, 0, NULL);

  // HEVCParamSets hevcparam;
  // HEVCSEI hevcsei;
  // ff_hevc_decode_extradata((const uint8_t *)buf, file_size, &hevcparam,
  //                          &hevcsei, &is_avc, &nal_length_size, 0, 0, NULL);

  ParseContext pc;
  H263Packet pkt;
  memset(&pc, 0, sizeof(pc));
  memset(&pkt, 0, sizeof(pkt));

  int left = file_size;
  uint8_t *buf = fbuf;
  while (left >= 0) {
    memset(&pkt, 0, sizeof(pkt));
    int ret = ff_h263_decode_data(&pc, &pkt, buf, left);
    if (ret > 0) {
      buf += ret;
      left -= ret;
      if (pkt.got_pic)
        printf("pkt size:%d x %d\n", pkt.picture.width, pkt.picture.height);
    } else if (ret == 0) { // EOS
      if (pkt.got_pic)
        printf("pkt size:%d x %d\n", pkt.picture.width, pkt.picture.height);

      printf("---stream eos---\n");
      break;
    }
    printf("ret=%d\n", ret);
  }

  // 关闭文件
  fclose(fp);

  // 处理文件内容
  // ...

  // 释放缓冲区
  free(fbuf);

  return 0;
}