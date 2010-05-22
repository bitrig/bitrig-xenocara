#include "eglcommon.h"

#include <VG/openvg.h>

#include <stdio.h>
#include <string.h>

static const VGfloat white_color[4] = {1.0, 1.0, 1.0, 1.0};

static VGPath path;
static VGPaint fill;

VGColorRampSpreadMode spread = VG_COLOR_RAMP_SPREAD_PAD;

static void
init(void)
{
   static const VGubyte sqrCmds[5] = {VG_MOVE_TO_ABS, VG_HLINE_TO_ABS, VG_VLINE_TO_ABS, VG_HLINE_TO_ABS, VG_CLOSE_PATH};
   static const VGfloat sqrCoords[5]   = {0.0f, 0.0f, 400.0f, 400.0f, 0.0f};

   VGfloat rampStop[] = {0.00f, 1.0f, 1.0f, 1.0f, 1.0f,
                         0.33f, 1.0f, 0.0f, 0.0f, 1.0f,
                         0.66f, 0.0f, 1.0f, 0.0f, 1.0f,
                         1.00f, 0.0f, 0.0f,  1.0f, 1.0f};
   VGfloat linearGradient[4] = {100.0f, 100.0f, 300.0f, 300.0f};

   path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F, 1, 0, 0, 0,
                       VG_PATH_CAPABILITY_APPEND_TO);
   vgAppendPathData(path, 5, sqrCmds, sqrCoords);

   fill = vgCreatePaint();
   vgSetPaint(fill, VG_FILL_PATH);

   vgSetParameteri(fill, VG_PAINT_TYPE, VG_PAINT_TYPE_LINEAR_GRADIENT);
   vgSetParameteri(fill, VG_PAINT_COLOR_RAMP_SPREAD_MODE, spread);
   vgSetParameterfv(fill, VG_PAINT_LINEAR_GRADIENT, 4, linearGradient);
   vgSetParameterfv(fill, VG_PAINT_COLOR_RAMP_STOPS, 20, rampStop);

   vgSetfv(VG_CLEAR_COLOR, 4, white_color);
}

/* new window size or exposure */
static void
reshape(int w, int h)
{
   vgLoadIdentity();
}

static void
draw(void)
{
   vgClear(0, 0, window_width(), window_height());

   vgDrawPath(path, VG_FILL_PATH);

   vgFlush();
}


int main(int argc, char **argv)
{
   if (argc > 1) {
      const char *arg = argv[1];
      if (!strcmp("-pad", arg))
         spread = VG_COLOR_RAMP_SPREAD_PAD;
      else if (!strcmp("-repeat", arg))
         spread = VG_COLOR_RAMP_SPREAD_REPEAT;
      else if (!strcmp("-reflect", arg))
         spread = VG_COLOR_RAMP_SPREAD_REFLECT;
   }

   switch(spread) {
   case VG_COLOR_RAMP_SPREAD_PAD:
      printf("Using spread mode: pad\n");
      break;
   case VG_COLOR_RAMP_SPREAD_REPEAT:
      printf("Using spread mode: repeat\n");
      break;
   case VG_COLOR_RAMP_SPREAD_REFLECT:
      printf("Using spread mode: reflect\n");
   }

   set_window_size(400, 400);

   return run(argc, argv, init, reshape,
              draw, 0);
}
