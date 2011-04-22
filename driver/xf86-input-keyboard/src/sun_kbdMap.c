/*
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 * Copyright 1993 by David Dawes <dawes@xfree86.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the names of Thomas Roell and David Dawes not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Thomas Roell and David Dawes make no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * THOMAS ROELL AND DAVID DAWES DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
 * IN NO EVENT SHALL THOMAS ROELL OR DAVID DAWES BE LIABLE FOR ANY SPECIAL,
 * INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xorg-server.h>
#include <X11/Xfuncproto.h>
#include <X11/Sunkeysym.h>
#include "atKeynames.h"
#include "xf86OSKbd.h"
#include "xf86Keymap.h"
#include "sun_kbd.h"

#include <sys/kbd.h>

/* Map the Solaris keycodes to the "XFree86" keycodes. */

/*
 * Additional Korean 106 Keyboard Keys not defined in atKeynames.h
 * These are exactly same USB usage id with Kana(0x90) and Eisu(0x91) keys
 * in Mac Japanese keyboard. From /usr/X11/lib/X11/xkb/keycodes/xfree86, these
 * are 209 and 210. So these should be 0xc9(209-8=201) and 0xca(210-8=202).
 *   <EISU> =   210;          // Alphanumeric mode on macintosh
 *   <KANA> =   209;          // Kana mode on macintosh
 */
#define KEY_Hangul            0xC9    /* Also Kana in Mac Japanaese kbd */
#define KEY_Hangul_Hanja      0xCA    /* Also Eisu in Mac Japanaese kbd */

/* Override atKeynames.h values with unique keycodes, so we can distinguish
   KEY_F15 from KEY_HKTG & KEY_KP_DEC from KEY_BSlash2 */
#undef KEY_HKTG
#define KEY_HKTG         /* Hirugana/Katakana tog 0xC8  */  200 /* was 112 */
#undef KEY_BSlash2
#define KEY_BSlash2      /* \           _         0xCB  */  203 /* was 115 */

static unsigned char sunmap[256] = {
#if defined(i386) || defined(__i386) || defined(__i386__) || defined(__x86)
	KEY_NOTUSED,		/*   0 */
	KEY_Tilde,		/*   1 */
	KEY_1,			/*   2 */
	KEY_2,			/*   3 */
	KEY_3,			/*   4 */
	KEY_4,			/*   5 */
	KEY_5,			/*   6 */
	KEY_6,			/*   7 */
	KEY_7,			/*   8 */
	KEY_8,			/*   9 */
	KEY_9,			/*  10 */
	KEY_0,			/*  11 */
	KEY_Minus,		/*  12 */
	KEY_Equal,		/*  13 */
	0x7D, /*KEY_P_YEN*/	/*  14 */
	KEY_BackSpace,		/*  15 */
	KEY_Tab,		/*  16 */
	KEY_Q,			/*  17 */
	KEY_W,			/*  18 */
	KEY_E,			/*  19 */
	KEY_R,			/*  20 */
	KEY_T,			/*  21 */
	KEY_Y,			/*  22 */
	KEY_U,			/*  23 */
	KEY_I,			/*  24 */
	KEY_O,			/*  25 */
	KEY_P,			/*  26 */
	KEY_LBrace,		/*  27 */
	KEY_RBrace,		/*  28 */
	KEY_BSlash,		/*  29 */
	KEY_CapsLock,		/*  30 */
	KEY_A,			/*  31 */
	KEY_S,			/*  32 */
	KEY_D,			/*  33 */
	KEY_F,			/*  34 */
	KEY_G,			/*  35 */
	KEY_H,			/*  36 */
	KEY_J,			/*  37 */
	KEY_K,			/*  38 */
	KEY_L,			/*  39 */
	KEY_SemiColon,		/*  40 */
	KEY_Quote,		/*  41 */
	KEY_UNKNOWN,		/*  42 */
	KEY_Enter,		/*  43 */
	KEY_ShiftL,		/*  44 */
	KEY_Less,		/*  45 */
	KEY_Z,			/*  46 */
	KEY_X,			/*  47 */
	KEY_C,			/*  48 */
	KEY_V,			/*  49 */
	KEY_B,			/*  50 */
	KEY_N,			/*  51 */
	KEY_M,			/*  52 */
	KEY_Comma,		/*  53 */
	KEY_Period,		/*  54 */
	KEY_Slash,		/*  55 */
	KEY_BSlash2,		/*  56 */
	KEY_ShiftR,		/*  57 */
	KEY_LCtrl,		/*  58 */
	KEY_LMeta,		/*  59 */
	KEY_Alt,		/*  60 */
	KEY_Space,		/*  61 */
	KEY_AltLang,		/*  62 */
	KEY_RMeta,		/*  63 */
	KEY_RCtrl,		/*  64 */
	KEY_Menu,		/*  65 */
	KEY_UNKNOWN,		/*  66 */
	KEY_UNKNOWN,		/*  67 */
	KEY_UNKNOWN,		/*  68 */
	KEY_UNKNOWN,		/*  69 */
	KEY_UNKNOWN,		/*  70 */
	KEY_UNKNOWN,		/*  71 */
	KEY_UNKNOWN,		/*  72 */
	KEY_UNKNOWN,		/*  73 */
	KEY_UNKNOWN,		/*  74 */
	KEY_Insert,		/*  75 */
	KEY_Delete,		/*  76 */
	KEY_UNKNOWN,		/*  77 */
	KEY_UNKNOWN,		/*  78 */
	KEY_Left,		/*  79 */
	KEY_Home,		/*  80 */
	KEY_End,		/*  81 */
	KEY_UNKNOWN,		/*  82 */
	KEY_Up,			/*  83 */
	KEY_Down,		/*  84 */
	KEY_PgUp,		/*  85 */
	KEY_PgDown,		/*  86 */
	KEY_UNKNOWN,		/*  87 */
	KEY_UNKNOWN,		/*  88 */
	KEY_Right,		/*  89 */
	KEY_NumLock,		/*  90 */
	KEY_KP_7,		/*  91 */
	KEY_KP_4,		/*  92 */
	KEY_KP_1,		/*  93 */
	KEY_UNKNOWN,		/*  94 */
	KEY_KP_Divide,		/*  95 */
	KEY_KP_8,		/*  96 */
	KEY_KP_5,		/*  97 */
	KEY_KP_2,		/*  98 */
	KEY_KP_0,		/*  99 */
	KEY_KP_Multiply,	/* 100 */
	KEY_KP_9,		/* 101 */
	KEY_KP_6,		/* 102 */
	KEY_KP_3,		/* 103 */
	KEY_KP_Decimal,		/* 104 */
	KEY_KP_Minus,		/* 105 */
	KEY_KP_Plus,		/* 106 */
	KEY_UNKNOWN,		/* 107 */
	KEY_KP_Enter,		/* 108 */
	KEY_UNKNOWN,		/* 109 */
	KEY_Escape,		/* 110 */
	KEY_UNKNOWN,		/* 111 */
	KEY_F1,			/* 112 */
	KEY_F2,			/* 113 */
	KEY_F3,			/* 114 */
	KEY_F4,			/* 115 */
	KEY_F5,			/* 116 */
	KEY_F6,			/* 117 */
	KEY_F7,			/* 118 */
	KEY_F8,			/* 119 */
	KEY_F9,			/* 120 */
	KEY_F10,		/* 121 */
	KEY_F11,		/* 122 */
	KEY_F12,		/* 123 */
	KEY_Print,		/* 124 */
	KEY_ScrollLock,		/* 125 */
	KEY_Pause,		/* 126 */
	KEY_UNKNOWN,		/* 127 */
	KEY_UNKNOWN,		/* 128 */
	KEY_UNKNOWN,		/* 129 */
	KEY_UNKNOWN,		/* 130 */
	KEY_NFER,		/* 131 */
	KEY_XFER,		/* 132 */
	KEY_HKTG,		/* 133 */
	KEY_UNKNOWN,		/* 134 */
#elif defined(sparc) || defined(__sparc__)
	KEY_UNKNOWN,		/* 0x00 */
	KEY_UNKNOWN,		/* 0x01 */
	KEY_UNKNOWN,		/* 0x02 */
	KEY_UNKNOWN,		/* 0x03 */
	KEY_UNKNOWN,		/* 0x04 */
	KEY_F1,			/* 0x05 */
	KEY_F2,			/* 0x06 */
	KEY_F10,		/* 0x07 */
	KEY_F3,			/* 0x08 */
	KEY_F11,		/* 0x09 */
	KEY_F4,			/* 0x0A */
	KEY_F12,		/* 0x0B */
	KEY_F5,			/* 0x0C */
	KEY_UNKNOWN,		/* 0x0D */
	KEY_F6,			/* 0x0E */
	KEY_UNKNOWN,		/* 0x0F */
	KEY_F7,			/* 0x10 */
	KEY_F8,			/* 0x11 */
	KEY_F9,			/* 0x12 */
	KEY_Alt,		/* 0x13 */
	KEY_Up,			/* 0x14 */
	KEY_Pause,		/* 0x15 */
	KEY_SysReqest,		/* 0x16 */
	KEY_ScrollLock,		/* 0x17 */
	KEY_Left,		/* 0x18 */
	KEY_UNKNOWN,		/* 0x19 */
	KEY_UNKNOWN,		/* 0x1A */
	KEY_Down,		/* 0x1B */
	KEY_Right,		/* 0x1C */
	KEY_Escape,		/* 0x1D */
	KEY_1,			/* 0x1E */
	KEY_2,			/* 0x1F */
	KEY_3,			/* 0x20 */
	KEY_4,			/* 0x21 */
	KEY_5,			/* 0x22 */
	KEY_6,			/* 0x23 */
	KEY_7,			/* 0x24 */
	KEY_8,			/* 0x25 */
	KEY_9,			/* 0x26 */
	KEY_0,			/* 0x27 */
	KEY_Minus,		/* 0x28 */
	KEY_Equal,		/* 0x29 */
	KEY_Tilde,		/* 0x2A */
	KEY_BackSpace,		/* 0x2B */
	KEY_Insert,		/* 0x2C */
	KEY_UNKNOWN,		/* 0x2D */
	KEY_KP_Divide,		/* 0x2E */
	KEY_KP_Multiply,	/* 0x2F */
	KEY_UNKNOWN,		/* 0x30 */
	KEY_UNKNOWN,		/* 0x31 */
	KEY_KP_Decimal,		/* 0x32 */
	KEY_UNKNOWN,		/* 0x33 */
	KEY_Home,		/* 0x34 */
	KEY_Tab,		/* 0x35 */
	KEY_Q,			/* 0x36 */
	KEY_W,			/* 0x37 */
	KEY_E,			/* 0x38 */
	KEY_R,			/* 0x39 */
	KEY_T,			/* 0x3A */
	KEY_Y,			/* 0x3B */
	KEY_U,			/* 0x3C */
	KEY_I,			/* 0x3D */
	KEY_O,			/* 0x3E */
	KEY_P,			/* 0x3F */
	KEY_LBrace,		/* 0x40 */
	KEY_RBrace,		/* 0x41 */
	KEY_Delete,		/* 0x42 */
	KEY_UNKNOWN,		/* 0x43 */
	KEY_KP_7,		/* 0x44 */
	KEY_KP_8,		/* 0x45 */
	KEY_KP_9,		/* 0x46 */
	KEY_KP_Minus,		/* 0x47 */
	KEY_UNKNOWN,		/* 0x48 */
	KEY_UNKNOWN,		/* 0x49 */
	KEY_End,		/* 0x4A */
	KEY_UNKNOWN,		/* 0x4B */
	KEY_LCtrl,		/* 0x4C */
	KEY_A,			/* 0x4D */
	KEY_S,			/* 0x4E */
	KEY_D,			/* 0x4F */
	KEY_F,			/* 0x50 */
	KEY_G,			/* 0x51 */
	KEY_H,			/* 0x52 */
	KEY_J,			/* 0x53 */
	KEY_K,			/* 0x54 */
	KEY_L,			/* 0x55 */
	KEY_SemiColon,		/* 0x56 */
	KEY_Quote,		/* 0x57 */
	KEY_BSlash,		/* 0x58 */
	KEY_Enter,		/* 0x59 */
	KEY_KP_Enter,		/* 0x5A */
	KEY_KP_4,		/* 0x5B */
	KEY_KP_5,		/* 0x5C */
	KEY_KP_6,		/* 0x5D */
	KEY_KP_0,		/* 0x5E */
	KEY_UNKNOWN,		/* 0x5F */
	KEY_PgUp,		/* 0x60 */
	KEY_UNKNOWN,		/* 0x61 */
	KEY_NumLock,		/* 0x62 */
	KEY_ShiftL,		/* 0x63 */
	KEY_Z,			/* 0x64 */
	KEY_X,			/* 0x65 */
	KEY_C,			/* 0x66 */
	KEY_V,			/* 0x67 */
	KEY_B,			/* 0x68 */
	KEY_N,			/* 0x69 */
	KEY_M,			/* 0x6A */
	KEY_Comma,		/* 0x6B */
	KEY_Period,		/* 0x6C */
	KEY_Slash,		/* 0x6D */
	KEY_ShiftR,		/* 0x6E */
	KEY_UNKNOWN,		/* 0x6F */
	KEY_KP_1,		/* 0x70 */
	KEY_KP_2,		/* 0x71 */
	KEY_KP_3,		/* 0x72 */
	KEY_UNKNOWN,		/* 0x73 */
	KEY_UNKNOWN,		/* 0x74 */
	KEY_UNKNOWN,		/* 0x75 */
	KEY_UNKNOWN,		/* 0x76 */
	KEY_CapsLock,		/* 0x77 */
	KEY_LMeta,		/* 0x78 */
	KEY_Space,		/* 0x79 */
	KEY_RMeta,		/* 0x7A */
	KEY_PgDown,		/* 0x7B */
	KEY_UNKNOWN,		/* 0x7C */
	KEY_KP_Plus,		/* 0x7D */
	KEY_UNKNOWN,		/* 0x7E */
	KEY_UNKNOWN,		/* 0x7F */
#endif
	/* The rest default to KEY_UNKNOWN */
};

static
TransMapRec sunTransMap = {
    0,
    (sizeof(sunmap)/sizeof(unsigned char)),
    sunmap
};

#if defined(KB_USB)
static unsigned char usbmap[256] = {
/*
 * partially taken from ../bsd/bsd_KbdMap.c
 *
 * added keycodes for Sun special keys (left function keys, audio control)
 */
	/* 0 */ KEY_NOTUSED,
	/* 1 */ KEY_NOTUSED,
	/* 2 */ KEY_NOTUSED,
	/* 3 */ KEY_NOTUSED,
	/* 4 */ KEY_A,		
	/* 5 */ KEY_B,
	/* 6 */ KEY_C,
	/* 7 */ KEY_D,
	/* 8 */ KEY_E,
	/* 9 */ KEY_F,
	/* 10 */ KEY_G,
	/* 11 */ KEY_H,
	/* 12 */ KEY_I,
	/* 13 */ KEY_J,
	/* 14 */ KEY_K,
	/* 15 */ KEY_L,
	/* 16 */ KEY_M,
	/* 17 */ KEY_N,
	/* 18 */ KEY_O,
	/* 19 */ KEY_P,
	/* 20 */ KEY_Q,
	/* 21 */ KEY_R,
	/* 22 */ KEY_S,
	/* 23 */ KEY_T,
	/* 24 */ KEY_U,
	/* 25 */ KEY_V,
	/* 26 */ KEY_W,
	/* 27 */ KEY_X,
	/* 28 */ KEY_Y,
	/* 29 */ KEY_Z,
	/* 30 */ KEY_1,		/* 1 !*/
	/* 31 */ KEY_2,		/* 2 @ */
	/* 32 */ KEY_3,		/* 3 # */
	/* 33 */ KEY_4,		/* 4 $ */
	/* 34 */ KEY_5,		/* 5 % */
	/* 35 */ KEY_6,		/* 6 ^ */
	/* 36 */ KEY_7,		/* 7 & */
	/* 37 */ KEY_8,		/* 8 * */
	/* 38 */ KEY_9,		/* 9 ( */
	/* 39 */ KEY_0,		/* 0 ) */
	/* 40 */ KEY_Enter,	/* Return  */
	/* 41 */ KEY_Escape,	/* Escape */
	/* 42 */ KEY_BackSpace,	/* Backspace Delete */
	/* 43 */ KEY_Tab,	/* Tab */
	/* 44 */ KEY_Space,	/* Space */
	/* 45 */ KEY_Minus,	/* - _ */
	/* 46 */ KEY_Equal,	/* = + */
	/* 47 */ KEY_LBrace,	/* [ { */
	/* 48 */ KEY_RBrace,	/* ] } */
	/* 49 */ KEY_BSlash,	/* \ | */
	/* 50 */ KEY_BSlash,	/* \ _ # ~ on some keyboards */
	/* 51 */ KEY_SemiColon,	/* ; : */
	/* 52 */ KEY_Quote,	/* ' " */
	/* 53 */ KEY_Tilde,	/* ` ~ */
	/* 54 */ KEY_Comma,	/* , <  */
	/* 55 */ KEY_Period,	/* . > */
	/* 56 */ KEY_Slash,	/* / ? */
	/* 57 */ KEY_CapsLock,	/* Caps Lock */
	/* 58 */ KEY_F1,		/* F1 */
	/* 59 */ KEY_F2,		/* F2 */
	/* 60 */ KEY_F3,		/* F3 */
	/* 61 */ KEY_F4,		/* F4 */
	/* 62 */ KEY_F5,		/* F5 */
	/* 63 */ KEY_F6,		/* F6 */
	/* 64 */ KEY_F7,		/* F7 */
	/* 65 */ KEY_F8,		/* F8 */
	/* 66 */ KEY_F9,		/* F9 */
	/* 67 */ KEY_F10,	/* F10 */
	/* 68 */ KEY_F11,	/* F11 */
	/* 69 */ KEY_F12,	/* F12 */
	/* 70 */ KEY_Print,	/* PrintScrn SysReq */
	/* 71 */ KEY_ScrollLock,	/* Scroll Lock */
	/* 72 */ KEY_Pause,	/* Pause Break */
	/* 73 */ KEY_Insert,	/* Insert XXX  Help on some Mac Keyboards */
	/* 74 */ KEY_Home,	/* Home */
	/* 75 */ KEY_PgUp,	/* Page Up */
	/* 76 */ KEY_Delete,	/* Delete */
	/* 77 */ KEY_End,	/* End */
	/* 78 */ KEY_PgDown,	/* Page Down */
	/* 79 */ KEY_Right,	/* Right Arrow */
	/* 80 */ KEY_Left,	/* Left Arrow */
	/* 81 */ KEY_Down,	/* Down Arrow */
	/* 82 */ KEY_Up,		/* Up Arrow */
	/* 83 */ KEY_NumLock,	/* Num Lock */
	/* 84 */ KEY_KP_Divide,	/* Keypad / */
	/* 85 */ KEY_KP_Multiply, /* Keypad * */
	/* 86 */ KEY_KP_Minus,	/* Keypad - */
	/* 87 */ KEY_KP_Plus,	/* Keypad + */
	/* 88 */ KEY_KP_Enter,	/* Keypad Enter */
	/* 89 */ KEY_KP_1,	/* Keypad 1 End */
	/* 90 */ KEY_KP_2,	/* Keypad 2 Down */
	/* 91 */ KEY_KP_3,	/* Keypad 3 Pg Down */
	/* 92 */ KEY_KP_4,	/* Keypad 4 Left  */
	/* 93 */ KEY_KP_5,	/* Keypad 5 */
	/* 94 */ KEY_KP_6,	/* Keypad 6 */
	/* 95 */ KEY_KP_7,	/* Keypad 7 Home */
	/* 96 */ KEY_KP_8,	/* Keypad 8 Up */
	/* 97 */ KEY_KP_9,	/* KEypad 9 Pg Up */
	/* 98 */ KEY_KP_0,	/* Keypad 0 Ins */
	/* 99 */ KEY_KP_Decimal,	/* Keypad . Del */
	/* 100 */ KEY_Less,	/* < > on some keyboards */
	/* 101 */ KEY_Menu,	/* Menu */
	/* 102 */ KEY_Power,	/* Sun: Power */
	/* 103 */ KEY_KP_Equal, /* Keypad = on Mac keyboards */
	/* 104 */ KEY_NOTUSED,
	/* 105 */ KEY_NOTUSED,
	/* 106 */ KEY_NOTUSED,
	/* 107 */ KEY_NOTUSED,
	/* 108 */ KEY_NOTUSED,
	/* 109 */ KEY_NOTUSED,
	/* 110 */ KEY_NOTUSED,
	/* 111 */ KEY_NOTUSED,
	/* 112 */ KEY_NOTUSED,
	/* 113 */ KEY_NOTUSED,
	/* 114 */ KEY_NOTUSED,
	/* 115 */ KEY_NOTUSED,
	/* 116 */ KEY_L7,	/* Sun: Open */
	/* 117 */ KEY_Help,	/* Sun: Help */
	/* 118 */ KEY_L3,	/* Sun: Props */
	/* 119 */ KEY_L5,	/* Sun: Front */
	/* 120 */ KEY_L1,	/* Sun: Stop */
	/* 121 */ KEY_L2,	/* Sun: Again */
	/* 122 */ KEY_L4,	/* Sun: Undo */
	/* 123 */ KEY_L10,	/* Sun: Cut */
	/* 124 */ KEY_L6,	/* Sun: Copy */
	/* 125 */ KEY_L8,	/* Sun: Paste */
	/* 126 */ KEY_L9,	/* Sun: Find */
	/* 127 */ KEY_Mute,	/* Sun: AudioMute */
	/* 128 */ KEY_AudioRaise,	/* Sun: AudioRaise */
	/* 129 */ KEY_AudioLower,	/* Sun: AudioLower */
	/* 130 */ KEY_NOTUSED,
	/* 131 */ KEY_NOTUSED,
	/* 132 */ KEY_NOTUSED,
	/* 133 */ KEY_NOTUSED,
	/* 134 */ KEY_NOTUSED,
	/* 135 */ KEY_BSlash2,	/* Sun Japanese Kbd: Backslash / Underscore */
	/* 136 */ KEY_HKTG,	/* Sun Japanese type7 Kbd: Hirugana/Katakana */
	/* 137 */ KEY_Yen,	/* Sun Japanese Kbd: Yen / Brokenbar */
	/* 138 */ KEY_XFER,	/* Sun Japanese Kbd: Kanji Transfer */
	/* 139 */ KEY_NFER,	/* Sun Japanese Kbd: No Kanji Transfer */
	/* 140 */ KEY_NOTUSED,
	/* 141 */ KEY_NOTUSED,
	/* 142 */ KEY_NOTUSED,
	/* 143 */ KEY_NOTUSED,
	/* 144 */ KEY_Hangul,		/* Korean 106 Kbd: Hangul */
	/* 145 */ KEY_Hangul_Hanja,	/* Korean 106 Kbd: Hanja */
	/* 146 */ KEY_NOTUSED,
	/* 147 */ KEY_NOTUSED,
	/* 148 */ KEY_NOTUSED,
	/* 149 */ KEY_NOTUSED,
	/* 150 */ KEY_NOTUSED,
	/* 151 */ KEY_NOTUSED,
	/* 152 */ KEY_NOTUSED,
	/* 153 */ KEY_NOTUSED,
	/* 154 */ KEY_NOTUSED,
	/* 155 */ KEY_NOTUSED,
	/* 156 */ KEY_NOTUSED,
	/* 157 */ KEY_NOTUSED,
	/* 158 */ KEY_NOTUSED,
	/* 159 */ KEY_NOTUSED,
	/* 160 */ KEY_NOTUSED,
	/* 161 */ KEY_NOTUSED,
	/* 162 */ KEY_NOTUSED,
	/* 163 */ KEY_NOTUSED,
	/* 164 */ KEY_NOTUSED,
	/* 165 */ KEY_NOTUSED,
	/* 166 */ KEY_NOTUSED,
	/* 167 */ KEY_NOTUSED,
	/* 168 */ KEY_NOTUSED,
	/* 169 */ KEY_NOTUSED,
	/* 170 */ KEY_NOTUSED,
	/* 171 */ KEY_NOTUSED,
	/* 172 */ KEY_NOTUSED,
	/* 173 */ KEY_NOTUSED,
	/* 174 */ KEY_NOTUSED,
	/* 175 */ KEY_NOTUSED,
	/* 176 */ KEY_NOTUSED,
	/* 177 */ KEY_NOTUSED,
	/* 178 */ KEY_NOTUSED,
	/* 179 */ KEY_NOTUSED,
	/* 180 */ KEY_NOTUSED,
	/* 181 */ KEY_NOTUSED,
	/* 182 */ KEY_NOTUSED,
	/* 183 */ KEY_NOTUSED,
	/* 184 */ KEY_NOTUSED,
	/* 185 */ KEY_NOTUSED,
	/* 186 */ KEY_NOTUSED,
	/* 187 */ KEY_NOTUSED,
	/* 188 */ KEY_NOTUSED,
	/* 189 */ KEY_NOTUSED,
	/* 190 */ KEY_NOTUSED,
	/* 191 */ KEY_NOTUSED,
	/* 192 */ KEY_NOTUSED,
	/* 193 */ KEY_NOTUSED,
	/* 194 */ KEY_NOTUSED,
	/* 195 */ KEY_NOTUSED,
	/* 196 */ KEY_NOTUSED,
	/* 197 */ KEY_NOTUSED,
	/* 198 */ KEY_NOTUSED,
	/* 199 */ KEY_NOTUSED,
	/* 200 */ KEY_NOTUSED,
	/* 201 */ KEY_NOTUSED,
	/* 202 */ KEY_NOTUSED,
	/* 203 */ KEY_NOTUSED,
	/* 204 */ KEY_NOTUSED,
	/* 205 */ KEY_NOTUSED,
	/* 206 */ KEY_NOTUSED,
	/* 207 */ KEY_NOTUSED,
	/* 208 */ KEY_NOTUSED,
	/* 209 */ KEY_NOTUSED,
	/* 210 */ KEY_NOTUSED,
	/* 211 */ KEY_NOTUSED,
	/* 212 */ KEY_NOTUSED,
	/* 213 */ KEY_NOTUSED,
	/* 214 */ KEY_NOTUSED,
	/* 215 */ KEY_NOTUSED,
	/* 216 */ KEY_NOTUSED,
	/* 217 */ KEY_NOTUSED,
	/* 218 */ KEY_NOTUSED,
	/* 219 */ KEY_NOTUSED,
	/* 220 */ KEY_NOTUSED,
	/* 221 */ KEY_NOTUSED,
	/* 222 */ KEY_NOTUSED,
	/* 223 */ KEY_NOTUSED,
	/* 224 */ KEY_LCtrl,	/* Left Control */
	/* 225 */ KEY_ShiftL,	/* Left Shift */
	/* 226 */ KEY_Alt,	/* Left Alt */
	/* 227 */ KEY_LMeta,	/* Left Meta */
	/* 228 */ KEY_RCtrl,	/* Right Control */
	/* 229 */ KEY_ShiftR,	/* Right Shift */
	/* 230 */ KEY_AltLang,	/* Right Alt, AKA AltGr */
	/* 231 */ KEY_RMeta,	/* Right Meta */
};

static
TransMapRec usbTransMap = {
    0,
    (sizeof(usbmap)/sizeof(unsigned char)),
    usbmap
};
#endif /* KB_USB */

_X_HIDDEN void
KbdGetMapping (InputInfoPtr pInfo, KeySymsPtr pKeySyms, CARD8 *pModMap)
{
    KbdDevPtr pKbd = (KbdDevPtr) pInfo->private;
    sunKbdPrivPtr priv = (sunKbdPrivPtr) pKbd->private;
    int i;
    KeySym        *k;
    
#if defined(KB_USB)
    if (priv->ktype == KB_USB)
	pKbd->scancodeMap = &usbTransMap;
    else
#endif
        pKbd->scancodeMap = &sunTransMap;

    /*
     * Add Sun keyboard keysyms to default map
     */
#define map_for_key(k,c) 	map[(k * GLYPHS_PER_KEY) + c]   
    map_for_key(KEY_Power,	0) = SunXK_PowerSwitch;
    map_for_key(KEY_Power,	1) = SunXK_PowerSwitchShift;
    map_for_key(KEY_Mute,	0) = SunXK_AudioMute;
    map_for_key(KEY_Mute,	1) = SunXK_VideoDegauss;
    map_for_key(KEY_AudioLower,	0) = SunXK_AudioLowerVolume;
    map_for_key(KEY_AudioLower,	1) = SunXK_VideoLowerBrightness;
    map_for_key(KEY_AudioRaise,	0) = SunXK_AudioRaiseVolume;
    map_for_key(KEY_AudioRaise,	1) = SunXK_VideoRaiseBrightness;
    map_for_key(KEY_Help,	0) = XK_Help;
    map_for_key(KEY_L1,		0) = XK_L1;
    map_for_key(KEY_L2,		0) = XK_L2;
    map_for_key(KEY_L3,		0) = XK_L3;
    map_for_key(KEY_L4,		0) = XK_L4;
    map_for_key(KEY_L5,		0) = XK_L5;
    map_for_key(KEY_L6,		0) = XK_L6;
    map_for_key(KEY_L7,		0) = XK_L7;
    map_for_key(KEY_L8,		0) = XK_L8;
    map_for_key(KEY_L9,		0) = XK_L9;
    map_for_key(KEY_L10,	0) = XK_L10;
    map_for_key(KEY_F11,	0) = SunXK_F36;
    map_for_key(KEY_F12,	0) = SunXK_F37;
    map_for_key(KEY_Menu,	0) = XK_Multi_key;
    
    /*
     * compute the modifier map
     */
    for (i = 0; i < MAP_LENGTH; i++)
	pModMap[i] = NoSymbol;  /* make sure it is restored */
  
    for (k = map, i = MIN_KEYCODE;
	 i < (NUM_KEYCODES + MIN_KEYCODE);
	 i++, k += 4)
    {
	switch(*k) {
      
	case XK_Shift_L:
	case XK_Shift_R:
	    pModMap[i] = ShiftMask;
	    break;
      
	case XK_Control_L:
	case XK_Control_R:
	    pModMap[i] = ControlMask;
	    break;
      
	case XK_Caps_Lock:
	    pModMap[i] = LockMask;
	    break;
      
	case XK_Alt_L:
	case XK_Alt_R:
	    pModMap[i] = AltMask;
	    break;
      
	case XK_Num_Lock:
	    pModMap[i] = NumLockMask;
	    break;

	case XK_Scroll_Lock:
	    pModMap[i] = ScrollLockMask;
	    break;

	    /* kana support */
	case XK_Kana_Lock:
	case XK_Kana_Shift:
	    pModMap[i] = KanaMask;
	    break;

	    /* alternate toggle for multinational support */
	case XK_Mode_switch:
	    pModMap[i] = AltLangMask;
	    break;

	}
    }
	
    pKeySyms->map        = map;
    pKeySyms->mapWidth   = GLYPHS_PER_KEY;
    pKeySyms->minKeyCode = MIN_KEYCODE;
    pKeySyms->maxKeyCode = MAX_KEYCODE;
}
