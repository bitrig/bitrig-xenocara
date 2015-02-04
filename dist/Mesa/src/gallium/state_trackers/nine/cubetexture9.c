/*
 * Copyright 2011 Joakim Sindholt <opensource@zhasha.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include "device9.h"
#include "cubetexture9.h"
#include "nine_helpers.h"
#include "nine_pipe.h"

#define DBG_CHANNEL DBG_CUBETEXTURE


static HRESULT
NineCubeTexture9_ctor( struct NineCubeTexture9 *This,
                       struct NineUnknownParams *pParams,
                       UINT EdgeLength, UINT Levels,
                       DWORD Usage,
                       D3DFORMAT Format,
                       D3DPOOL Pool,
                       HANDLE *pSharedHandle )
{
    struct pipe_resource *info = &This->base.base.info;
    struct pipe_screen *screen = pParams->device->screen;
    enum pipe_format pf;
    unsigned i;
    D3DSURFACE_DESC sfdesc;
    HRESULT hr;

    DBG("This=%p pParams=%p EdgeLength=%u Levels=%u Usage=%d "
        "Format=%d Pool=%d pSharedHandle=%p\n",
        This, pParams, EdgeLength, Levels, Usage,
        Format, Pool, pSharedHandle);

    user_assert(!(Usage & D3DUSAGE_AUTOGENMIPMAP) ||
                (Pool != D3DPOOL_SYSTEMMEM && Levels <= 1), D3DERR_INVALIDCALL);

    user_assert(!pSharedHandle, D3DERR_INVALIDCALL); /* TODO */

    if (Usage & D3DUSAGE_AUTOGENMIPMAP)
        Levels = 0;

    pf = d3d9_to_pipe_format(Format);
    if (pf == PIPE_FORMAT_NONE ||
        !screen->is_format_supported(screen, pf, PIPE_TEXTURE_CUBE, 0, PIPE_BIND_SAMPLER_VIEW)) {
        return D3DERR_INVALIDCALL;
    }

    /* We support ATI1 and ATI2 hacks only for 2D textures */
    if (Format == D3DFMT_ATI1 || Format == D3DFMT_ATI2)
        return D3DERR_INVALIDCALL;

    info->screen = pParams->device->screen;
    info->target = PIPE_TEXTURE_CUBE;
    info->format = pf;
    info->width0 = EdgeLength;
    info->height0 = EdgeLength;
    info->depth0 = 1;
    if (Levels)
        info->last_level = Levels - 1;
    else
        info->last_level = util_logbase2(EdgeLength);
    info->array_size = 6;
    info->nr_samples = 0;
    info->bind = PIPE_BIND_SAMPLER_VIEW;
    info->usage = PIPE_USAGE_DEFAULT;
    info->flags = 0;

    if (Usage & D3DUSAGE_RENDERTARGET)
        info->bind |= PIPE_BIND_RENDER_TARGET;
    if (Usage & D3DUSAGE_DEPTHSTENCIL)
        info->bind |= PIPE_BIND_DEPTH_STENCIL;

    if (Usage & D3DUSAGE_DYNAMIC) {
        info->usage = PIPE_USAGE_DYNAMIC;
        info->bind |=
            PIPE_BIND_TRANSFER_READ |
            PIPE_BIND_TRANSFER_WRITE;
    }

    This->surfaces = CALLOC(6 * (info->last_level + 1), sizeof(*This->surfaces));
    if (!This->surfaces)
        return E_OUTOFMEMORY;

    hr = NineBaseTexture9_ctor(&This->base, pParams, NULL, D3DRTYPE_CUBETEXTURE,
                               Format, Pool, Usage);
    if (FAILED(hr))
        return hr;
    This->base.pstype = 2;

    /* Create all the surfaces right away.
     * They manage backing storage, and transfers (LockRect) are deferred
     * to them.
     */
    sfdesc.Format = Format;
    sfdesc.Type = D3DRTYPE_SURFACE;
    sfdesc.Usage = Usage;
    sfdesc.Pool = Pool;
    sfdesc.MultiSampleType = D3DMULTISAMPLE_NONE;
    sfdesc.MultiSampleQuality = 0;
    for (i = 0; i < (info->last_level + 1) * 6; ++i) {
        sfdesc.Width = sfdesc.Height = u_minify(EdgeLength, i / 6);

        hr = NineSurface9_new(This->base.base.base.device, NineUnknown(This),
                              This->base.base.resource, NULL, D3DRTYPE_CUBETEXTURE,
                              i / 6, i % 6,
                              &sfdesc, &This->surfaces[i]);
        if (FAILED(hr))
            return hr;
    }
    for (i = 0; i < 6; ++i) /* width = 0 means empty, depth stays 1 */
        This->dirty_rect[i].depth = 1;

    return D3D_OK;
}

static void
NineCubeTexture9_dtor( struct NineCubeTexture9 *This )
{
    unsigned i;

    DBG("This=%p\n", This);

    if (This->surfaces) {
        for (i = 0; i < This->base.base.info.last_level * 6; ++i)
            NineUnknown_Destroy(&This->surfaces[i]->base.base);
        FREE(This->surfaces);
    }

    NineBaseTexture9_dtor(&This->base);
}

HRESULT WINAPI
NineCubeTexture9_GetLevelDesc( struct NineCubeTexture9 *This,
                               UINT Level,
                               D3DSURFACE_DESC *pDesc )
{
    DBG("This=%p Level=%u pDesc=%p\n", This, Level, pDesc);

    user_assert(Level <= This->base.base.info.last_level, D3DERR_INVALIDCALL);
    user_assert(Level == 0 || !(This->base.base.usage & D3DUSAGE_AUTOGENMIPMAP),
                D3DERR_INVALIDCALL);

    *pDesc = This->surfaces[Level * 6]->desc;

    return D3D_OK;
}

HRESULT WINAPI
NineCubeTexture9_GetCubeMapSurface( struct NineCubeTexture9 *This,
                                    D3DCUBEMAP_FACES FaceType,
                                    UINT Level,
                                    IDirect3DSurface9 **ppCubeMapSurface )
{
    const unsigned s = Level * 6 + FaceType;

    DBG("This=%p FaceType=%d Level=%u ppCubeMapSurface=%p\n",
        This, FaceType, Level, ppCubeMapSurface);

    user_assert(Level <= This->base.base.info.last_level, D3DERR_INVALIDCALL);
    user_assert(Level == 0 || !(This->base.base.usage & D3DUSAGE_AUTOGENMIPMAP),
                D3DERR_INVALIDCALL);
    user_assert(FaceType < 6, D3DERR_INVALIDCALL);

    NineUnknown_AddRef(NineUnknown(This->surfaces[s]));
    *ppCubeMapSurface = (IDirect3DSurface9 *)This->surfaces[s];

    return D3D_OK;
}

HRESULT WINAPI
NineCubeTexture9_LockRect( struct NineCubeTexture9 *This,
                           D3DCUBEMAP_FACES FaceType,
                           UINT Level,
                           D3DLOCKED_RECT *pLockedRect,
                           const RECT *pRect,
                           DWORD Flags )
{
    const unsigned s = Level * 6 + FaceType;

    DBG("This=%p FaceType=%d Level=%u pLockedRect=%p pRect=%p Flags=%d\n",
        This, FaceType, Level, pLockedRect, pRect, Flags);

    user_assert(Level <= This->base.base.info.last_level, D3DERR_INVALIDCALL);
    user_assert(Level == 0 || !(This->base.base.usage & D3DUSAGE_AUTOGENMIPMAP),
                D3DERR_INVALIDCALL);
    user_assert(FaceType < 6, D3DERR_INVALIDCALL);

    return NineSurface9_LockRect(This->surfaces[s], pLockedRect, pRect, Flags);
}

HRESULT WINAPI
NineCubeTexture9_UnlockRect( struct NineCubeTexture9 *This,
                             D3DCUBEMAP_FACES FaceType,
                             UINT Level )
{
    const unsigned s = Level * 6 + FaceType;

    DBG("This=%p FaceType=%d Level=%u\n", This, FaceType, Level);

    user_assert(Level <= This->base.base.info.last_level, D3DERR_INVALIDCALL);
    user_assert(FaceType < 6, D3DERR_INVALIDCALL);

    return NineSurface9_UnlockRect(This->surfaces[s]);
}

HRESULT WINAPI
NineCubeTexture9_AddDirtyRect( struct NineCubeTexture9 *This,
                               D3DCUBEMAP_FACES FaceType,
                               const RECT *pDirtyRect )
{
    DBG("This=%p FaceType=%d pDirtyRect=%p\n", This, FaceType, pDirtyRect);

    user_assert(FaceType < 6, D3DERR_INVALIDCALL);

    if (This->base.base.pool != D3DPOOL_MANAGED) {
        if (This->base.base.usage & D3DUSAGE_AUTOGENMIPMAP)
            This->base.dirty_mip = TRUE;
        return D3D_OK;
    }
    This->base.dirty = TRUE;

    BASETEX_REGISTER_UPDATE(&This->base);

    if (!pDirtyRect) {
        u_box_origin_2d(This->base.base.info.width0,
                        This->base.base.info.height0,
                        &This->dirty_rect[FaceType]);
    } else {
        struct pipe_box box;
        rect_to_pipe_box_clamp(&box, pDirtyRect);
        u_box_union_2d(&This->dirty_rect[FaceType], &This->dirty_rect[FaceType],
                       &box);
    }
    return D3D_OK;
}

IDirect3DCubeTexture9Vtbl NineCubeTexture9_vtable = {
    (void *)NineUnknown_QueryInterface,
    (void *)NineUnknown_AddRef,
    (void *)NineUnknown_Release,
    (void *)NineUnknown_GetDevice, /* actually part of Resource9 iface */
    (void *)NineResource9_SetPrivateData,
    (void *)NineResource9_GetPrivateData,
    (void *)NineResource9_FreePrivateData,
    (void *)NineResource9_SetPriority,
    (void *)NineResource9_GetPriority,
    (void *)NineBaseTexture9_PreLoad,
    (void *)NineResource9_GetType,
    (void *)NineBaseTexture9_SetLOD,
    (void *)NineBaseTexture9_GetLOD,
    (void *)NineBaseTexture9_GetLevelCount,
    (void *)NineBaseTexture9_SetAutoGenFilterType,
    (void *)NineBaseTexture9_GetAutoGenFilterType,
    (void *)NineBaseTexture9_GenerateMipSubLevels,
    (void *)NineCubeTexture9_GetLevelDesc,
    (void *)NineCubeTexture9_GetCubeMapSurface,
    (void *)NineCubeTexture9_LockRect,
    (void *)NineCubeTexture9_UnlockRect,
    (void *)NineCubeTexture9_AddDirtyRect
};

static const GUID *NineCubeTexture9_IIDs[] = {
    &IID_IDirect3DCubeTexture9,
    &IID_IDirect3DBaseTexture9,
    &IID_IDirect3DResource9,
    &IID_IUnknown,
    NULL
};

HRESULT
NineCubeTexture9_new( struct NineDevice9 *pDevice,
                      UINT EdgeLength, UINT Levels,
                      DWORD Usage,
                      D3DFORMAT Format,
                      D3DPOOL Pool,
                      struct NineCubeTexture9 **ppOut,
                      HANDLE *pSharedHandle )
{
    NINE_DEVICE_CHILD_NEW(CubeTexture9, ppOut, pDevice,
                          EdgeLength, Levels,
                          Usage, Format, Pool, pSharedHandle);
}
