/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/ati/atichip.c,v 1.38tsi Exp $ */
/*
 * Copyright 1997 through 2004 by Marc Aurele La France (TSI @ UQV), tsi@xfree86.org
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ati.h"
#include "atibus.h"
#include "atichip.h"
#include "atimach64io.h"
#include "ativersion.h"

/*
 * Chip-related definitions.
 */
const char *ATIChipNames[] =
{
    "Unknown",

#ifndef AVOID_CPIO

    "IBM VGA or compatible",
    "ATI 18800",
    "ATI 18800-1",
    "ATI 28800-2",
    "ATI 28800-4",
    "ATI 28800-5",
    "ATI 28800-6",
    "IBM 8514/A",
    "Chips & Technologies 82C480",
    "ATI 38800-1",
    "ATI 68800",
    "ATI 68800-3",
    "ATI 68800-6",
    "ATI 68800LX",
    "ATI 68800AX",

#endif /* AVOID_CPIO */

    "ATI 88800GX-C",
    "ATI 88800GX-D",
    "ATI 88800GX-E",
    "ATI 88800GX-F",
    "ATI 88800GX",
    "ATI 88800CX",
    "ATI 264CT",
    "ATI 264ET",
    "ATI 264VT",
    "ATI 3D Rage",
    "ATI 264VT-B",
    "ATI 3D Rage II",
    "ATI 264VT3",
    "ATI 3D Rage II+DVD",
    "ATI 3D Rage LT",
    "ATI 264VT4",
    "ATI 3D Rage IIc",
    "ATI 3D Rage Pro",
    "ATI 3D Rage LT Pro",
    "ATI 3D Rage XL or XC",
    "ATI 3D Rage Mobility",
    "ATI unknown Mach64",
    "ATI Rage 128 GL",
    "ATI Rage 128 VR",
    "ATI Rage 128 Pro GL",
    "ATI Rage 128 Pro VR",
    "ATI Rage 128 Pro ULTRA",
    "ATI Rage 128 Mobility M3",
    "ATI Rage 128 Mobility M4",
    "ATI unknown Rage 128"
    "ATI Radeon 7200",
    "ATI Radeon 7000 (VE)",
    "ATI Radeon Mobility M6",
    "ATI Radeon IGP320",
    "ATI Radeon IGP330/340/350",
    "ATI Radeon 7000 IGP",
    "ATI Radeon 7500",
    "ATI Radeon Mobility M7",
    "ATI Radeon 8500/9100",
    "ATI Radeon 9000",
    "ATI Radeon Mobility M9",
    "ATI Radeon 9100 IGP",
    "ATI Radeon 9200 IGP",
    "ATI Radeon 9200",
    "ATI Radeon Mobility M9+",
    "ATI Radeon 9700/9500",
    "ATI Radeon 9600/9550",
    "ATI Radeon 9800",
    "ATI Radeon 9800XT",
    "ATI Radeon X300/X550/M22",
    "ATI Radeon X600/X550/M24",
    "ATI Radeon X800/M18 AGP",
    "ATI Radeon X800/M28 PCIE",
    "ATI Radeon X800XL PCIE",
    "ATI Radeon X850 PCIE",
    "ATI Radeon X850 AGP",
    "ATI Radeon X700",
    "ATI Xpress 200"
    "ATI unknown Radeon",
    "ATI Rage HDTV"
};

const char *ATIFoundryNames[] =
    { "SGS", "NEC", "KCS", "UMC", "TSMC", "5", "6", "UMC" };

#ifndef AVOID_CPIO

/*
 * ATIMach32ChipID --
 *
 * Set variables whose value is dependent upon an 68800's CHIP_ID register.
 */
void
ATIMach32ChipID
(
    ATIPtr pATI
)
{
    CARD16 IOValue     = inw(CHIP_ID);
    pATI->ChipType     = GetBits(IOValue, CHIP_CODE_0 | CHIP_CODE_1);
    pATI->ChipClass    = GetBits(IOValue, CHIP_CLASS);
    pATI->ChipRevision = GetBits(IOValue, CHIP_REV);
    pATI->ChipRev      = pATI->ChipRevision;
    if (IOValue == 0xFFFFU)
        IOValue = 0;
    switch (GetBits(IOValue, CHIP_CODE_0 | CHIP_CODE_1))
    {
        case OldChipID('A', 'A'):
            pATI->Chip = ATI_CHIP_68800_3;
            break;

        case OldChipID('X', 'X'):
            pATI->Chip = ATI_CHIP_68800_6;
            break;

        case OldChipID('L', 'X'):
            pATI->Chip = ATI_CHIP_68800LX;
            break;

        case OldChipID('A', 'X'):
            pATI->Chip = ATI_CHIP_68800AX;
            break;

        default:
            pATI->Chip = ATI_CHIP_68800;
            break;
    }
}

#endif /* AVOID_CPIO */

/*
 * ATIMach64ChipID --
 *
 * Set variables whose value is dependent upon a Mach64's CONFIG_CHIP_ID
 * register.
 */
void
ATIMach64ChipID
(
    ATIPtr       pATI,
    const CARD16 ExpectedChipType
)
{
    pATI->config_chip_id = inr(CONFIG_CHIP_ID);
    pATI->ChipType       = GetBits(pATI->config_chip_id, 0xFFFFU);
    pATI->ChipClass      = GetBits(pATI->config_chip_id, CFG_CHIP_CLASS);
    pATI->ChipRevision   = GetBits(pATI->config_chip_id, CFG_CHIP_REV);
    pATI->ChipVersion    = GetBits(pATI->config_chip_id, CFG_CHIP_VERSION);
    pATI->ChipFoundry    = GetBits(pATI->config_chip_id, CFG_CHIP_FOUNDRY);
    pATI->ChipRev        = pATI->ChipRevision;
    switch (pATI->ChipType)
    {
        case OldChipID('G', 'X'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('G', 'X'):
            switch (pATI->ChipRevision)
            {
                case 0x00U:
                    pATI->Chip = ATI_CHIP_88800GXC;
                    break;

                case 0x01U:
                    pATI->Chip = ATI_CHIP_88800GXD;
                    break;

                case 0x02U:
                    pATI->Chip = ATI_CHIP_88800GXE;
                    break;

                case 0x03U:
                    pATI->Chip = ATI_CHIP_88800GXF;
                    break;

                default:
                    pATI->Chip = ATI_CHIP_88800GX;
                    break;
            }
            break;

        case OldChipID('C', 'X'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('C', 'X'):
            pATI->Chip = ATI_CHIP_88800CX;
            break;

        case OldChipID('C', 'T'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('C', 'T'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264CT;
            pATI->BusType = ATI_BUS_PCI;
            break;

        case OldChipID('E', 'T'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('E', 'T'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264ET;
            pATI->BusType = ATI_BUS_PCI;
            break;

        case OldChipID('V', 'T'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('V', 'T'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264VT;
            pATI->BusType = ATI_BUS_PCI;
            /* Some early GT's are detected as VT's */
            if (ExpectedChipType && (pATI->ChipType != ExpectedChipType))
            {
                if (ExpectedChipType == NewChipID('G', 'T'))
                    pATI->Chip = ATI_CHIP_264GT;
                else
                    xf86Msg(X_WARNING,
                            ATI_NAME ":  Mach64 chip type probe discrepancy"
                            " detected:  PCI=0x%04X;  CHIP_ID=0x%04X.\n",
                            ExpectedChipType, pATI->ChipType);
            }
            else if (pATI->ChipVersion)
                pATI->Chip = ATI_CHIP_264VTB;
            break;

        case OldChipID('G', 'T'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('G', 'T'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->BusType = ATI_BUS_PCI;
            if (!pATI->ChipVersion)
                pATI->Chip = ATI_CHIP_264GT;
            else
                pATI->Chip = ATI_CHIP_264GTB;
            break;

        case OldChipID('V', 'U'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('V', 'U'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264VT3;
            pATI->BusType = ATI_BUS_PCI;
            break;

        case OldChipID('G', 'U'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('G', 'U'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264GTDVD;
            pATI->BusType = ATI_BUS_PCI;
            break;

        case OldChipID('L', 'G'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('L', 'G'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264LT;
            pATI->BusType = ATI_BUS_PCI;
            break;

        case OldChipID('V', 'V'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('V', 'V'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264VT4;
            pATI->BusType = ATI_BUS_PCI;
            break;

        case OldChipID('G', 'V'):
        case OldChipID('G', 'Y'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('G', 'V'):
        case NewChipID('G', 'Y'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264GT2C;
            pATI->BusType = ATI_BUS_PCI;
            break;

        case OldChipID('G', 'W'):
        case OldChipID('G', 'Z'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('G', 'W'):
        case NewChipID('G', 'Z'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264GT2C;
            pATI->BusType = ATI_BUS_AGP;
            break;

        case OldChipID('G', 'I'):
        case OldChipID('G', 'P'):
        case OldChipID('G', 'Q'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('G', 'I'):
        case NewChipID('G', 'P'):
        case NewChipID('G', 'Q'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264GTPRO;
            pATI->BusType = ATI_BUS_PCI;
            break;

        case OldChipID('G', 'B'):
        case OldChipID('G', 'D'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('G', 'B'):
        case NewChipID('G', 'D'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264GTPRO;
            pATI->BusType = ATI_BUS_AGP;
            break;

        case OldChipID('L', 'I'):
        case OldChipID('L', 'P'):
        case OldChipID('L', 'Q'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('L', 'I'):
        case NewChipID('L', 'P'):
        case NewChipID('L', 'Q'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264LTPRO;
            pATI->BusType = ATI_BUS_PCI;
            pATI->LCDVBlendFIFOSize = 800;
            break;

        case OldChipID('L', 'B'):
        case OldChipID('L', 'D'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('L', 'B'):
        case NewChipID('L', 'D'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264LTPRO;
            pATI->BusType = ATI_BUS_AGP;
            pATI->LCDVBlendFIFOSize = 800;
            break;

        case OldChipID('G', 'L'):
        case OldChipID('G', 'O'):
        case OldChipID('G', 'R'):
        case OldChipID('G', 'S'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('G', 'L'):
        case NewChipID('G', 'O'):
        case NewChipID('G', 'R'):
        case NewChipID('G', 'S'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264XL;
            pATI->BusType = ATI_BUS_PCI;
            pATI->LCDVBlendFIFOSize = 1024;
            break;

        case OldChipID('G', 'M'):
        case OldChipID('G', 'N'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('G', 'M'):
        case NewChipID('G', 'N'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_264XL;
            pATI->BusType = ATI_BUS_AGP;
            pATI->LCDVBlendFIFOSize = 1024;
            break;

        case OldChipID('L', 'R'):
        case OldChipID('L', 'S'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('L', 'R'):
        case NewChipID('L', 'S'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_MOBILITY;
            pATI->BusType = ATI_BUS_PCI;
            pATI->LCDVBlendFIFOSize = 1024;
            break;

        case OldChipID('L', 'M'):
        case OldChipID('L', 'N'):
            pATI->ChipType = OldToNewChipID(pATI->ChipType);
        case NewChipID('L', 'M'):
        case NewChipID('L', 'N'):
            pATI->ChipRevision =
                GetBits(pATI->config_chip_id, CFG_CHIP_REVISION);
            pATI->Chip = ATI_CHIP_MOBILITY;
            pATI->BusType = ATI_BUS_AGP;
            pATI->LCDVBlendFIFOSize = 1024;
            break;

        default:
            pATI->Chip = ATI_CHIP_Mach64;
            break;
    }
}

/*
 * ATIChipID --
 *
 * This returns the ATI_CHIP_* value (generally) associated with a particular
 * ChipID/ChipRev combination.
 */
ATIChipType
ATIChipID
(
    const CARD16 ChipID,
    const CARD8  ChipRev
)
{
    switch (ChipID)
    {

#ifndef AVOID_CPIO

        case OldChipID('A', 'A'):  case NewChipID('A', 'A'):
            return ATI_CHIP_68800_3;

        case OldChipID('X', 'X'):  case NewChipID('X', 'X'):
            return ATI_CHIP_68800_6;

        case OldChipID('L', 'X'):
            return ATI_CHIP_68800LX;

        case OldChipID('A', 'X'):  case NewChipID('A', 'X'):
            return ATI_CHIP_68800AX;

#endif /* AVOID_CPIO */

        case OldChipID('G', 'X'):  case NewChipID('G', 'X'):
            switch (ChipRev)
            {
                case 0x00U:
                    return ATI_CHIP_88800GXC;

                case 0x01U:
                    return ATI_CHIP_88800GXD;

                case 0x02U:
                    return ATI_CHIP_88800GXE;

                case 0x03U:
                    return ATI_CHIP_88800GXF;

                default:
                    return ATI_CHIP_88800GX;
            }

        case OldChipID('C', 'X'):  case NewChipID('C', 'X'):
            return ATI_CHIP_88800CX;

        case OldChipID('C', 'T'):  case NewChipID('C', 'T'):
            return ATI_CHIP_264CT;

        case OldChipID('E', 'T'):  case NewChipID('E', 'T'):
            return ATI_CHIP_264ET;

        case OldChipID('V', 'T'):  case NewChipID('V', 'T'):
            /* For simplicity, ignore ChipID discrepancy that can occur here */
            if (!(ChipRev & GetBits(CFG_CHIP_VERSION, CFG_CHIP_REV)))
                return ATI_CHIP_264VT;
            return ATI_CHIP_264VTB;

        case OldChipID('G', 'T'):  case NewChipID('G', 'T'):
            if (!(ChipRev & GetBits(CFG_CHIP_VERSION, CFG_CHIP_REV)))
                return ATI_CHIP_264GT;
            return ATI_CHIP_264GTB;

        case OldChipID('V', 'U'):  case NewChipID('V', 'U'):
            return ATI_CHIP_264VT3;

        case OldChipID('G', 'U'):  case NewChipID('G', 'U'):
            return ATI_CHIP_264GTDVD;

        case OldChipID('L', 'G'):  case NewChipID('L', 'G'):
            return ATI_CHIP_264LT;

        case OldChipID('V', 'V'):  case NewChipID('V', 'V'):
            return ATI_CHIP_264VT4;

        case OldChipID('G', 'V'):  case NewChipID('G', 'V'):
        case OldChipID('G', 'W'):  case NewChipID('G', 'W'):
        case OldChipID('G', 'Y'):  case NewChipID('G', 'Y'):
        case OldChipID('G', 'Z'):  case NewChipID('G', 'Z'):
            return ATI_CHIP_264GT2C;

        case OldChipID('G', 'B'):  case NewChipID('G', 'B'):
        case OldChipID('G', 'D'):  case NewChipID('G', 'D'):
        case OldChipID('G', 'I'):  case NewChipID('G', 'I'):
        case OldChipID('G', 'P'):  case NewChipID('G', 'P'):
        case OldChipID('G', 'Q'):  case NewChipID('G', 'Q'):
            return ATI_CHIP_264GTPRO;

        case OldChipID('L', 'B'):  case NewChipID('L', 'B'):
        case OldChipID('L', 'D'):  case NewChipID('L', 'D'):
        case OldChipID('L', 'I'):  case NewChipID('L', 'I'):
        case OldChipID('L', 'P'):  case NewChipID('L', 'P'):
        case OldChipID('L', 'Q'):  case NewChipID('L', 'Q'):
            return ATI_CHIP_264LTPRO;

        case OldChipID('G', 'L'):  case NewChipID('G', 'L'):
        case OldChipID('G', 'M'):  case NewChipID('G', 'M'):
        case OldChipID('G', 'N'):  case NewChipID('G', 'N'):
        case OldChipID('G', 'O'):  case NewChipID('G', 'O'):
        case OldChipID('G', 'R'):  case NewChipID('G', 'R'):
        case OldChipID('G', 'S'):  case NewChipID('G', 'S'):
            return ATI_CHIP_264XL;

        case OldChipID('L', 'M'):  case NewChipID('L', 'M'):
        case OldChipID('L', 'N'):  case NewChipID('L', 'N'):
        case OldChipID('L', 'R'):  case NewChipID('L', 'R'):
        case OldChipID('L', 'S'):  case NewChipID('L', 'S'):
            return ATI_CHIP_MOBILITY;

        case NewChipID('R', 'E'):
        case NewChipID('R', 'F'):
        case NewChipID('R', 'G'):
        case NewChipID('S', 'K'):
        case NewChipID('S', 'L'):
        case NewChipID('S', 'M'):
        /* "SN" is listed as ATI_CHIP_RAGE128_4X in ATI docs */
        case NewChipID('S', 'N'):
            return ATI_CHIP_RAGE128GL;

        case NewChipID('R', 'K'):
        case NewChipID('R', 'L'):
        /*
         * ATI documentation lists SE/SF/SG under both ATI_CHIP_RAGE128VR
         * and ATI_CHIP_RAGE128_4X, and lists SH/SK/SL under Rage 128 4X only.
         * I'm stuffing them here for now until this can be clarified as ATI
         * documentation doesn't mention their details. <mharris@redhat.com>
         */
        case NewChipID('S', 'E'):
        case NewChipID('S', 'F'):
        case NewChipID('S', 'G'):
        case NewChipID('S', 'H'):
            return ATI_CHIP_RAGE128VR;

     /* case NewChipID('S', 'H'): */
     /* case NewChipID('S', 'K'): */
     /* case NewChipID('S', 'L'): */
     /* case NewChipID('S', 'N'): */
     /*     return ATI_CHIP_RAGE128_4X; */

        case NewChipID('P', 'A'):
        case NewChipID('P', 'B'):
        case NewChipID('P', 'C'):
        case NewChipID('P', 'D'):
        case NewChipID('P', 'E'):
        case NewChipID('P', 'F'):
            return ATI_CHIP_RAGE128PROGL;

        case NewChipID('P', 'G'):
        case NewChipID('P', 'H'):
        case NewChipID('P', 'I'):
        case NewChipID('P', 'J'):
        case NewChipID('P', 'K'):
        case NewChipID('P', 'L'):
        case NewChipID('P', 'M'):
        case NewChipID('P', 'N'):
        case NewChipID('P', 'O'):
        case NewChipID('P', 'P'):
        case NewChipID('P', 'Q'):
        case NewChipID('P', 'R'):
        case NewChipID('P', 'S'):
        case NewChipID('P', 'T'):
        case NewChipID('P', 'U'):
        case NewChipID('P', 'V'):
        case NewChipID('P', 'W'):
        case NewChipID('P', 'X'):
            return ATI_CHIP_RAGE128PROVR;

        case NewChipID('T', 'F'):
        case NewChipID('T', 'L'):
        case NewChipID('T', 'R'):
        case NewChipID('T', 'S'):
        case NewChipID('T', 'T'):
        case NewChipID('T', 'U'):
            return ATI_CHIP_RAGE128PROULTRA;

        case NewChipID('L', 'E'):
        case NewChipID('L', 'F'):
        /*
         * "LK" and "LL" are not in any ATI documentation I can find
         * - mharris
         */
        case NewChipID('L', 'K'):
        case NewChipID('L', 'L'):
            return ATI_CHIP_RAGE128MOBILITY3;

        case NewChipID('M', 'F'):
        case NewChipID('M', 'L'):
            return ATI_CHIP_RAGE128MOBILITY4;

        case NewChipID('Q', 'D'):
        case NewChipID('Q', 'E'):
        case NewChipID('Q', 'F'):
        case NewChipID('Q', 'G'):
            return ATI_CHIP_RADEON;

        case NewChipID('Q', 'Y'):
        case NewChipID('Q', 'Z'):
        case NewChipID('Q', '^'):
            return ATI_CHIP_RADEONVE;

        case NewChipID('L', 'Y'):
        case NewChipID('L', 'Z'):
            return ATI_CHIP_RADEONMOBILITY6;

        case NewChipID('A', '6'):
        case NewChipID('C', '6'):
             return ATI_CHIP_RS100;

        case NewChipID('A', '7'):
        case NewChipID('C', '7'):
             return ATI_CHIP_RS200;

        case NewChipID('D', '7'):
        case NewChipID('B', '7'):
             return ATI_CHIP_RS250;

        case NewChipID('L', 'W'):
        case NewChipID('L', 'X'):
            return ATI_CHIP_RADEONMOBILITY7;

        case NewChipID('Q', 'H'):
        case NewChipID('Q', 'I'):
        case NewChipID('Q', 'J'):
        case NewChipID('Q', 'K'):
        case NewChipID('Q', 'L'):
        case NewChipID('Q', 'M'):
        case NewChipID('Q', 'N'):
        case NewChipID('Q', 'O'):
        case NewChipID('Q', 'h'):
        case NewChipID('Q', 'i'):
        case NewChipID('Q', 'j'):
        case NewChipID('Q', 'k'):
        case NewChipID('Q', 'l'):
        case NewChipID('B', 'B'):
            return ATI_CHIP_R200;

        case NewChipID('Q', 'W'):
        case NewChipID('Q', 'X'):
            return ATI_CHIP_RV200;

        case NewChipID('I', 'f'):
        case NewChipID('I', 'g'):
            return ATI_CHIP_RV250;

        case NewChipID('L', 'd'):
        case NewChipID('L', 'f'):
        case NewChipID('L', 'g'):
            return ATI_CHIP_RADEONMOBILITY9;

        case NewChipID('X', '4'):
        case NewChipID('X', '5'):
             return ATI_CHIP_RS300;

        case NewChipID('x', '4'):
        case NewChipID('x', '5'):
             return ATI_CHIP_RS350;

        case NewChipID('Y', '\''):
        case NewChipID('Y', 'a'):
        case NewChipID('Y', 'b'):
        case NewChipID('Y', 'd'):
        case NewChipID('Y', 'e'):
            return ATI_CHIP_RV280;

        case NewChipID('\\', 'a'):
        case NewChipID('\\', 'c'):
            return ATI_CHIP_RADEONMOBILITY9PLUS;

        case NewChipID('A', 'D'):
        case NewChipID('A', 'E'):
        case NewChipID('A', 'F'):
        case NewChipID('A', 'G'):
        case NewChipID('N', 'D'):
        case NewChipID('N', 'E'):
        case NewChipID('N', 'F'):
        case NewChipID('N', 'G'):
            return ATI_CHIP_R300;

        case NewChipID('A', 'H'):
        case NewChipID('A', 'I'):
        case NewChipID('A', 'J'):
        case NewChipID('A', 'K'):
        case NewChipID('N', 'H'):
        case NewChipID('N', 'I'):
        case NewChipID('N', 'K'):
            return ATI_CHIP_R350;

        case NewChipID('A', 'P'):
        case NewChipID('A', 'Q'):
        case NewChipID('A', 'R'):
        case NewChipID('A', 'S'):
        case NewChipID('A', 'T'):
        case NewChipID('A', 'U'):
        case NewChipID('A', 'V'):
        case NewChipID('N', 'P'):
        case NewChipID('N', 'Q'):
        case NewChipID('N', 'R'):
        case NewChipID('N', 'S'):
        case NewChipID('N', 'T'):
        case NewChipID('N', 'V'):
            return ATI_CHIP_RV350;

        case NewChipID('N', 'J'):
            return ATI_CHIP_R360;

        case NewChipID('[', '\''):
        case NewChipID('[', 'b'):
        case NewChipID('[', 'c'):
        case NewChipID('[', 'd'):
        case NewChipID('[', 'e'):
        case NewChipID('T', '\''):
        case NewChipID('T', 'b'):
        case NewChipID('T', 'd'):
	    return ATI_CHIP_RV370;

        case NewChipID('>', 'P'):
        case NewChipID('>', 'T'):
        case NewChipID('1', 'P'):
        case NewChipID('1', 'R'):
        case NewChipID('1', 'T'):
	    return ATI_CHIP_RV380;

        case NewChipID('J', 'H'):
        case NewChipID('J', 'I'):
        case NewChipID('J', 'J'):
        case NewChipID('J', 'K'):
        case NewChipID('J', 'L'):
        case NewChipID('J', 'M'):
        case NewChipID('J', 'N'):
        case NewChipID('J', 'O'):
        case NewChipID('J', 'P'):
        case NewChipID('J', 'T'):
	    return ATI_CHIP_R420;

        case NewChipID('U', 'H'):
        case NewChipID('U', 'I'):
        case NewChipID('U', 'J'):
        case NewChipID('U', 'K'):
        case NewChipID('U', 'P'):
        case NewChipID('U', 'Q'):
        case NewChipID('U', 'R'):
        case NewChipID('U', 'T'):
        case NewChipID(']', 'W'):
        /* those are m28, not 100% certain they are r423 could
	   be r480 but not r430 as their pci id names indicate... */
        case NewChipID(']', 'H'):
        case NewChipID(']', 'I'):
        case NewChipID(']', 'J'):
	    return ATI_CHIP_R423;

        case NewChipID('U', 'L'):
        case NewChipID('U', 'M'):
        case NewChipID('U', 'N'):
        case NewChipID('U', 'O'):
	    return ATI_CHIP_R430;

        case NewChipID(']', 'L'):
        case NewChipID(']', 'M'):
        case NewChipID(']', 'N'):
        case NewChipID(']', 'O'):
        case NewChipID(']', 'P'):
        case NewChipID(']', 'R'):
	    return ATI_CHIP_R480;

        case NewChipID('K', 'I'):
        case NewChipID('K', 'J'):
        case NewChipID('K', 'K'):
        case NewChipID('K', 'L'):
	    return ATI_CHIP_R481;

        case NewChipID('^', 'H'):
        case NewChipID('^', 'J'):
        case NewChipID('^', 'K'):
        case NewChipID('^', 'L'):
        case NewChipID('^', 'M'):
        case NewChipID('^', 'O'):
        case NewChipID('V', 'J'):
        case NewChipID('V', 'K'):
        case NewChipID('V', 'O'):
        case NewChipID('V', 'R'):
        case NewChipID('V', 'S'):
	    return ATI_CHIP_RV410;

        case NewChipID('Z', 'A'):
        case NewChipID('Z', 'B'):
        case NewChipID('Z', 'a'):
        case NewChipID('Z', 'b'):
        case NewChipID('Y', 'T'):
        case NewChipID('Y', 'U'):
        case NewChipID('Y', 't'):
        case NewChipID('Y', 'u'):
	    return ATI_CHIP_RS400;

        case NewChipID('H', 'D'):
            return ATI_CHIP_HDTV;

        default:
            /*
             * Treat anything else as an unknown Radeon.  Please keep the above
             * up-to-date however, as it serves as a central chip list.
             */
            return ATI_CHIP_Radeon;
    }
}
