// uzlib's checksum helpers are only reached through uzlib_uncompress_chksum, which the
// Bible pack reader never calls (raw deflate). The device build links them from elsewhere.
#include <stdint.h>

uint32_t uzlib_adler32(const void *data, unsigned int length, uint32_t prev_sum) {
  (void)data;
  (void)length;
  return prev_sum;
}

uint32_t uzlib_crc32(const void *data, unsigned int length, uint32_t crc) {
  (void)data;
  (void)length;
  return crc;
}
