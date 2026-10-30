#include <string.h>
#include "media.h"
int jpeg_fit_to_screen(int img_w, int img_h, int *out_w, int *out_h) {
  if (img_w <= 0 || img_h <= 0) return -1;
  /* Fit into 640x480, preserve aspect, integer math. */
  if (img_w * 480 > img_h * 640) { *out_w = 640; *out_h = img_h * 640 / img_w; }
  else { *out_h = 480; *out_w = img_w * 480 / img_h; }
  return 0;
}
int video_is_supported(const char *ext) {
  if (!ext) return 0;
  /* MPEG-1 / MJPEG style only. NOT H.264, NOT 4K. */
  return strcmp(ext, "mpg") == 0 || strcmp(ext, "mpeg") == 0 || strcmp(ext, "mjpeg") == 0 || strcmp(ext, "avi") == 0;
}
const char *video_limit_text(void) {
  return "VIDEO LIMIT: MPEG-1 / MJPEG, LOW RES ONLY. NOT 4K H.264 ON SH-4 200 MHZ.";
}
