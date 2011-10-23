
#include "target-helpers/inline_debug_helper.h"
#include "state_tracker/drm_driver.h"
#include "radeon/drm/radeon_drm_public.h"
#include "r300/r300_public.h"

static struct pipe_screen *
create_screen(int fd)
{
   struct r300_winsys_screen *sws;
   struct pipe_screen *screen;

   sws = r300_drm_winsys_screen_create(fd);
   if (!sws)
      return NULL;

   screen = r300_screen_create(sws);
   if (!screen)
      return NULL;

   screen = debug_screen_wrap(screen);

   return screen;
}

DRM_DRIVER_DESCRIPTOR("radeon", "radeon", create_screen)
