/* FisixDreamcast media — Dreamcast ONLY, NOT C64/GBA.
   JPEG viewer: decode JPEG to PowerVR texture, display
   scaled to fit 640x480, from disc / VMU / SD.
   Library needed in KOS build: kos-ports libjpeg (-ljpeg)
   or stb_image (-lstb_image), see KOS parallax/png examples.
   Video: generic/simple only, practical limit MPEG-1 /
   MJPEG style, low resolution, NOT 4K H.264 — SH-4
   200 MHz cannot decode that smoothly. Be honest.
   Host build: header parsing / scaling math only, no
   actual PowerVR display, no claim of KOS display. */
#ifndef FISIXDC_MEDIA_H
#define FISIXDC_MEDIA_H
int jpeg_fit_to_screen(int img_w, int img_h, int *out_w, int *out_h);
int video_is_supported(const char *ext);
const char *video_limit_text(void);
#endif
