#ifndef R600_FORMATS_H
#define R600_FORMATS_H

/* list of formats from R700 ISA document - apply across GPUs in different registers */
#define     FMT_INVALID                     0x00000000
#define     FMT_8                           0x00000001
#define     FMT_4_4                         0x00000002
#define     FMT_3_3_2                       0x00000003
#define     FMT_16                          0x00000005
#define     FMT_16_FLOAT                    0x00000006
#define     FMT_8_8                         0x00000007
#define     FMT_5_6_5                       0x00000008
#define     FMT_6_5_5                       0x00000009
#define     FMT_1_5_5_5                     0x0000000A
#define     FMT_4_4_4_4                     0x0000000B
#define     FMT_5_5_5_1                     0x0000000C
#define     FMT_32                          0x0000000D
#define     FMT_32_FLOAT                    0x0000000E
#define     FMT_16_16                       0x0000000F
#define     FMT_16_16_FLOAT                 0x00000010
#define     FMT_8_24                        0x00000011
#define     FMT_8_24_FLOAT                  0x00000012
#define     FMT_24_8                        0x00000013
#define     FMT_24_8_FLOAT                  0x00000014
#define     FMT_10_11_11                    0x00000015
#define     FMT_10_11_11_FLOAT              0x00000016
#define     FMT_11_11_10                    0x00000017
#define     FMT_11_11_10_FLOAT              0x00000018
#define     FMT_2_10_10_10                  0x00000019
#define     FMT_8_8_8_8                     0x0000001A
#define     FMT_10_10_10_2                  0x0000001B
#define     FMT_X24_8_32_FLOAT              0x0000001C
#define     FMT_32_32                       0x0000001D
#define     FMT_32_32_FLOAT                 0x0000001E
#define     FMT_16_16_16_16                 0x0000001F
#define     FMT_16_16_16_16_FLOAT           0x00000020
#define     FMT_32_32_32_32                 0x00000022
#define     FMT_32_32_32_32_FLOAT           0x00000023
#define     FMT_1                           0x00000025
#define     FMT_GB_GR                       0x00000027
#define     FMT_BG_RG                       0x00000028
#define     FMT_32_AS_8                     0x00000029
#define     FMT_32_AS_8_8                   0x0000002a
#define     FMT_5_9_9_9_SHAREDEXP           0x0000002b
#define     FMT_8_8_8                       0x0000002c
#define     FMT_16_16_16                    0x0000002d
#define     FMT_16_16_16_FLOAT              0x0000002e
#define     FMT_32_32_32                    0x0000002f
#define     FMT_32_32_32_FLOAT              0x00000030
#define     FMT_BC1                         0x00000031
#define     FMT_BC2                         0x00000032
#define     FMT_BC3                         0x00000033
#define     FMT_BC4                         0x00000034
#define     FMT_BC5                         0x00000035

#endif
