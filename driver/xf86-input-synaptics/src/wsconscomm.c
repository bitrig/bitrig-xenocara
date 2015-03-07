/*
 * Copyright © 2011 Alexandr Shadchin
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xorg-server.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include "synproto.h"
#include "synapticsstr.h"
#include <xf86.h>

#include <fcntl.h>
#include <dev/wscons/wsconsio.h>

#ifdef X_PRIVSEP
extern int priv_open_device(const char *);
#endif

#define DEFAULT_WSMOUSE_DEV		"/dev/wsmouse0"

static Bool
WSConsIsTouchpad(InputInfoPtr pInfo, const char *device)
{
    int wsmouse_type, fd = -1;
    Bool rc = FALSE;

    if (device) {
#ifndef X_PRIVSEP
        fd = open(device, O_RDWR);
#else
        fd = priv_open_device(device);
#endif
    } else
        fd = pInfo->fd;

    if (fd < 0)
        return FALSE;

    if (ioctl(fd, WSMOUSEIO_GTYPE, &wsmouse_type) == -1) {
        xf86IDrvMsg(pInfo, X_ERROR, "cannot get mouse type\n");
        goto out;
    }

    if (wsmouse_type == WSMOUSE_TYPE_SYNAPTICS ||
        wsmouse_type == WSMOUSE_TYPE_SYNAP_SBTN ||
        wsmouse_type == WSMOUSE_TYPE_ALPS ||
        wsmouse_type == WSMOUSE_TYPE_ELANTECH)
        rc = TRUE;

out:
    if (device)
        close(fd);

    return rc;
}

static Bool
WSConsReadEvent(InputInfoPtr pInfo, struct wscons_event *event)
{
    Bool rc = TRUE;
    ssize_t len;

    len = read(pInfo->fd, event, sizeof(struct wscons_event));
    if (len <= 0) {
        if (errno != EAGAIN)
            xf86IDrvMsg(pInfo, X_ERROR, "read error %s\n", strerror(errno));
        rc = FALSE;
    } else if (len % sizeof(struct wscons_event)) {
        xf86IDrvMsg(pInfo, X_ERROR, "read error, invalid number of bytes\n");
        rc = FALSE;
    }

    return rc;
}

static Bool
WSConsDeviceOnHook(InputInfoPtr pInfo, SynapticsParameters *para)
{
    int wsmouse_mode = WSMOUSE_NATIVE;

    if (ioctl(pInfo->fd, WSMOUSEIO_SETMODE, &wsmouse_mode) == -1) {
        xf86IDrvMsg(pInfo, X_ERROR, "cannot set native mode\n");
        return FALSE;
    }

    return TRUE;
}

static Bool
WSConsDeviceOffHook(InputInfoPtr pInfo)
{
    int wsmouse_mode = WSMOUSE_COMPAT;

    if (ioctl(pInfo->fd, WSMOUSEIO_SETMODE, &wsmouse_mode) == -1) {
        xf86IDrvMsg(pInfo, X_ERROR, "cannot set compat mode\n");
        return FALSE;
    }

    return TRUE;
}

static Bool
WSConsQueryHardware(InputInfoPtr pInfo)
{
    return WSConsIsTouchpad(pInfo, NULL);
}

static Bool
WSConsReadHwState(InputInfoPtr pInfo,
    struct CommData *comm, struct SynapticsHwState *hwRet)
{
    SynapticsPrivate *priv = (SynapticsPrivate *)pInfo->private;
    struct SynapticsHwState *hw = comm->hwState;
    struct wscons_event event;
    Bool v;

    while (WSConsReadEvent(pInfo, &event)) {
        switch (event.type) {
        case WSCONS_EVENT_MOUSE_UP:
        case WSCONS_EVENT_MOUSE_DOWN:
            v = (event.type == WSCONS_EVENT_MOUSE_DOWN) ? TRUE : FALSE;
            switch (event.value) {
            case 0:
                hw->left = v;
                break;
            case 1:
                hw->middle = v;
                break;
            case 2:
                hw->right = v;
                break;
            case 3:
                hw->up = v;
                break;
            case 4:
                hw->down = v;
                break;
            case 5:
                hw->multi[0] = v;
                break;
            case 6:
                hw->multi[1] = v;
                break;
            case 7:
                hw->multi[2] = v;
                break;
            case 8:
                hw->multi[3] = v;
                break;
            case 9:
                hw->multi[4] = v;
                break;
            case 10:
                hw->multi[5] = v;
                break;
            case 11:
                hw->multi[6] = v;
                break;
            case 12:
                hw->multi[7] = v;
                break;
            }
            break;
        case WSCONS_EVENT_MOUSE_ABSOLUTE_X:
            hw->x = event.value;
            hw->cumulative_dx = hw->x;
            break;
        case WSCONS_EVENT_MOUSE_ABSOLUTE_Y:
            hw->y = priv->maxy - event.value + priv->miny;
            hw->cumulative_dy = hw->y;
            break;
        case WSCONS_EVENT_MOUSE_ABSOLUTE_Z:
            hw->z = event.value;
            break;
        case WSCONS_EVENT_MOUSE_ABSOLUTE_W:
            if (priv->model == MODEL_ELANTECH) {
                /* Elantech touchpads report number of fingers directly. */
                hw->fingerWidth = 5;
                hw->numFingers = event.value;
                break;
            }
            /* XXX magic number mapping which is mirrored in pms driver */
            switch (event.value) {
            case 0:
                hw->fingerWidth = 5;
                hw->numFingers = 2;
                break;
            case 1:
                hw->fingerWidth = 5;
                hw->numFingers = 3;
                break;
            case 4 ... 5:
                hw->fingerWidth = event.value;
                hw->numFingers = 1;
                break;
            }
            break;
        case WSCONS_EVENT_SYNC:
            if (hw->z == 0) {
                hw->fingerWidth = 0;
                hw->numFingers = 0;
            } else if (hw->numFingers == 0) {
                /*
                 * Because W may be 0 already, a two-finger touch on a
                 * Synaptics touchpad doesn't necessarily produce an update
                 * event for W.
                 */
                hw->fingerWidth = 5;
                hw->numFingers = 2;
            }
            hw->millis = 1000 * event.time.tv_sec + event.time.tv_nsec / 1000000;
            SynapticsCopyHwState(hwRet, hw);
            return TRUE;
        default:
            return FALSE;
        }
    }

    return FALSE;
}

static Bool
WSConsAutoDevProbe(InputInfoPtr pInfo, const char *device)
{
    int i;

    if (device && WSConsIsTouchpad(pInfo, device))
        return TRUE;

    if (WSConsIsTouchpad(pInfo, DEFAULT_WSMOUSE_DEV)) {
        xf86IDrvMsg(pInfo, X_PROBED, "auto-dev sets device to %s\n",
            DEFAULT_WSMOUSE_DEV);
        xf86ReplaceStrOption(pInfo->options, "Device", DEFAULT_WSMOUSE_DEV);
        return TRUE;
    }

    return FALSE;
}

static void
WSConsReadDevDimensions(InputInfoPtr pInfo)
{
    SynapticsPrivate *priv = (SynapticsPrivate *)pInfo->private;
    SynapticsParameters *para = &priv->synpara;
    struct wsmouse_calibcoords wsmc;
    int wsmouse_type;

    if (ioctl(pInfo->fd, WSMOUSEIO_GCALIBCOORDS, &wsmc) != 0) {
        xf86IDrvMsg(pInfo, X_ERROR, "failed to query axis range (%s)\n",
            strerror(errno));
        return;
    }

    priv->minx = wsmc.minx;
    priv->maxx = wsmc.maxx;
    priv->resx = wsmc.resx;
    xf86IDrvMsg(pInfo, X_PROBED, "x-axis range %d - %d resolution %d\n",
        priv->minx, priv->maxx, priv->resx);

    priv->miny = wsmc.miny;
    priv->maxy = wsmc.maxy;
    priv->resy = wsmc.resy;
    xf86IDrvMsg(pInfo, X_PROBED, "y-axis range %d - %d resolution %d\n",
        priv->miny, priv->maxy, priv->resy);

    priv->minp = 0;
    priv->maxp = 255;

    priv->minw = 0;
    priv->maxw = 15;

    priv->has_pressure = TRUE;
    priv->has_left = TRUE;
    priv->has_right = TRUE;
    priv->has_middle = TRUE;

    if (ioctl(pInfo->fd, WSMOUSEIO_GTYPE, &wsmouse_type) == -1)
        xf86IDrvMsg(pInfo, X_ERROR, "cannot get mouse type\n");

    switch (wsmouse_type) {
    default:
    case WSMOUSE_TYPE_SYNAP_SBTN:
        para->clickpad = TRUE;
        para->has_secondary_buttons = TRUE;
        /* FALLTHROUGH */
    case WSMOUSE_TYPE_SYNAPTICS:
        priv->model = MODEL_SYNAPTICS;
        priv->has_width = TRUE;
        priv->has_double = TRUE;
        priv->has_triple = TRUE;
        break;
    case WSMOUSE_TYPE_ALPS:
        priv->model = MODEL_ALPS;
        priv->has_width = FALSE;
        priv->has_double = FALSE;
        priv->has_triple = FALSE;
        break;
    case WSMOUSE_TYPE_ELANTECH:
        priv->model = MODEL_ELANTECH;
        priv->has_width = TRUE;
        priv->has_double = TRUE;
        priv->has_triple = TRUE;
        break;
    }
}

struct SynapticsProtocolOperations wscons_proto_operations = {
    WSConsDeviceOnHook,
    WSConsDeviceOffHook,
    WSConsQueryHardware,
    WSConsReadHwState,
    WSConsAutoDevProbe,
    WSConsReadDevDimensions
};
