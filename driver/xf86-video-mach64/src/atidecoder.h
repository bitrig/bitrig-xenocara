/*
 * Copyright 2003 by Marc Aurele La France (TSI @ UQV), tsi@xfree86.org
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of Marc Aurele La France not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Marc Aurele La France makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as-is" without express or implied warranty.
 *
 * MARC AURELE LA FRANCE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.  IN NO
 * EVENT SHALL MARC AURELE LA FRANCE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef ___ATIDECODER_H___
#define ___ATIDECODER_H___ 1

/*
 * Video decoder definitions.
 */
typedef enum
{
    ATI_DECODER_NONE,
    ATI_DECODER_BT819,
    ATI_DECODER_BT829,
    ATI_DECODER_BT829A,
    ATI_DECODER_SA7111,
    ATI_DECODER_SA7112,
    ATI_DECODER_THEATER,
    ATI_DECODER_7,
    ATI_DECODER_8,
    ATI_DECODER_9,
    ATI_DECODER_10,
    ATI_DECODER_11,
    ATI_DECODER_12,
    ATI_DECODER_13,
    ATI_DECODER_14,
    ATI_DECODER_15
} ATIDecoderType;

extern const char *ATIDecoderNames[];

#endif /* ___ATIDECODER_H___ */
