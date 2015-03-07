/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton interface for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     ATTRIBUTE = 258,
     CONST_TOK = 259,
     BOOL_TOK = 260,
     FLOAT_TOK = 261,
     INT_TOK = 262,
     UINT_TOK = 263,
     BREAK = 264,
     CONTINUE = 265,
     DO = 266,
     ELSE = 267,
     FOR = 268,
     IF = 269,
     DISCARD = 270,
     RETURN = 271,
     SWITCH = 272,
     CASE = 273,
     DEFAULT = 274,
     BVEC2 = 275,
     BVEC3 = 276,
     BVEC4 = 277,
     IVEC2 = 278,
     IVEC3 = 279,
     IVEC4 = 280,
     UVEC2 = 281,
     UVEC3 = 282,
     UVEC4 = 283,
     VEC2 = 284,
     VEC3 = 285,
     VEC4 = 286,
     CENTROID = 287,
     IN_TOK = 288,
     OUT_TOK = 289,
     INOUT_TOK = 290,
     UNIFORM = 291,
     VARYING = 292,
     NOPERSPECTIVE = 293,
     FLAT = 294,
     SMOOTH = 295,
     MAT2X2 = 296,
     MAT2X3 = 297,
     MAT2X4 = 298,
     MAT3X2 = 299,
     MAT3X3 = 300,
     MAT3X4 = 301,
     MAT4X2 = 302,
     MAT4X3 = 303,
     MAT4X4 = 304,
     SAMPLER1D = 305,
     SAMPLER2D = 306,
     SAMPLER3D = 307,
     SAMPLERCUBE = 308,
     SAMPLER1DSHADOW = 309,
     SAMPLER2DSHADOW = 310,
     SAMPLERCUBESHADOW = 311,
     SAMPLER1DARRAY = 312,
     SAMPLER2DARRAY = 313,
     SAMPLER1DARRAYSHADOW = 314,
     SAMPLER2DARRAYSHADOW = 315,
     SAMPLERCUBEARRAY = 316,
     SAMPLERCUBEARRAYSHADOW = 317,
     ISAMPLER1D = 318,
     ISAMPLER2D = 319,
     ISAMPLER3D = 320,
     ISAMPLERCUBE = 321,
     ISAMPLER1DARRAY = 322,
     ISAMPLER2DARRAY = 323,
     ISAMPLERCUBEARRAY = 324,
     USAMPLER1D = 325,
     USAMPLER2D = 326,
     USAMPLER3D = 327,
     USAMPLERCUBE = 328,
     USAMPLER1DARRAY = 329,
     USAMPLER2DARRAY = 330,
     USAMPLERCUBEARRAY = 331,
     SAMPLER2DRECT = 332,
     ISAMPLER2DRECT = 333,
     USAMPLER2DRECT = 334,
     SAMPLER2DRECTSHADOW = 335,
     SAMPLERBUFFER = 336,
     ISAMPLERBUFFER = 337,
     USAMPLERBUFFER = 338,
     SAMPLER2DMS = 339,
     ISAMPLER2DMS = 340,
     USAMPLER2DMS = 341,
     SAMPLER2DMSARRAY = 342,
     ISAMPLER2DMSARRAY = 343,
     USAMPLER2DMSARRAY = 344,
     SAMPLEREXTERNALOES = 345,
     IMAGE1D = 346,
     IMAGE2D = 347,
     IMAGE3D = 348,
     IMAGE2DRECT = 349,
     IMAGECUBE = 350,
     IMAGEBUFFER = 351,
     IMAGE1DARRAY = 352,
     IMAGE2DARRAY = 353,
     IMAGECUBEARRAY = 354,
     IMAGE2DMS = 355,
     IMAGE2DMSARRAY = 356,
     IIMAGE1D = 357,
     IIMAGE2D = 358,
     IIMAGE3D = 359,
     IIMAGE2DRECT = 360,
     IIMAGECUBE = 361,
     IIMAGEBUFFER = 362,
     IIMAGE1DARRAY = 363,
     IIMAGE2DARRAY = 364,
     IIMAGECUBEARRAY = 365,
     IIMAGE2DMS = 366,
     IIMAGE2DMSARRAY = 367,
     UIMAGE1D = 368,
     UIMAGE2D = 369,
     UIMAGE3D = 370,
     UIMAGE2DRECT = 371,
     UIMAGECUBE = 372,
     UIMAGEBUFFER = 373,
     UIMAGE1DARRAY = 374,
     UIMAGE2DARRAY = 375,
     UIMAGECUBEARRAY = 376,
     UIMAGE2DMS = 377,
     UIMAGE2DMSARRAY = 378,
     IMAGE1DSHADOW = 379,
     IMAGE2DSHADOW = 380,
     IMAGE1DARRAYSHADOW = 381,
     IMAGE2DARRAYSHADOW = 382,
     COHERENT = 383,
     VOLATILE = 384,
     RESTRICT = 385,
     READONLY = 386,
     WRITEONLY = 387,
     ATOMIC_UINT = 388,
     STRUCT = 389,
     VOID_TOK = 390,
     WHILE = 391,
     IDENTIFIER = 392,
     TYPE_IDENTIFIER = 393,
     NEW_IDENTIFIER = 394,
     FLOATCONSTANT = 395,
     INTCONSTANT = 396,
     UINTCONSTANT = 397,
     BOOLCONSTANT = 398,
     FIELD_SELECTION = 399,
     LEFT_OP = 400,
     RIGHT_OP = 401,
     INC_OP = 402,
     DEC_OP = 403,
     LE_OP = 404,
     GE_OP = 405,
     EQ_OP = 406,
     NE_OP = 407,
     AND_OP = 408,
     OR_OP = 409,
     XOR_OP = 410,
     MUL_ASSIGN = 411,
     DIV_ASSIGN = 412,
     ADD_ASSIGN = 413,
     MOD_ASSIGN = 414,
     LEFT_ASSIGN = 415,
     RIGHT_ASSIGN = 416,
     AND_ASSIGN = 417,
     XOR_ASSIGN = 418,
     OR_ASSIGN = 419,
     SUB_ASSIGN = 420,
     INVARIANT = 421,
     LOWP = 422,
     MEDIUMP = 423,
     HIGHP = 424,
     SUPERP = 425,
     PRECISION = 426,
     VERSION_TOK = 427,
     EXTENSION = 428,
     LINE = 429,
     COLON = 430,
     EOL = 431,
     INTERFACE = 432,
     OUTPUT = 433,
     PRAGMA_DEBUG_ON = 434,
     PRAGMA_DEBUG_OFF = 435,
     PRAGMA_OPTIMIZE_ON = 436,
     PRAGMA_OPTIMIZE_OFF = 437,
     PRAGMA_INVARIANT_ALL = 438,
     LAYOUT_TOK = 439,
     ASM = 440,
     CLASS = 441,
     UNION = 442,
     ENUM = 443,
     TYPEDEF = 444,
     TEMPLATE = 445,
     THIS = 446,
     PACKED_TOK = 447,
     GOTO = 448,
     INLINE_TOK = 449,
     NOINLINE = 450,
     PUBLIC_TOK = 451,
     STATIC = 452,
     EXTERN = 453,
     EXTERNAL = 454,
     LONG_TOK = 455,
     SHORT_TOK = 456,
     DOUBLE_TOK = 457,
     HALF = 458,
     FIXED_TOK = 459,
     UNSIGNED = 460,
     INPUT_TOK = 461,
     OUPTUT = 462,
     HVEC2 = 463,
     HVEC3 = 464,
     HVEC4 = 465,
     DVEC2 = 466,
     DVEC3 = 467,
     DVEC4 = 468,
     FVEC2 = 469,
     FVEC3 = 470,
     FVEC4 = 471,
     SAMPLER3DRECT = 472,
     SIZEOF = 473,
     CAST = 474,
     NAMESPACE = 475,
     USING = 476,
     RESOURCE = 477,
     PATCH = 478,
     SAMPLE = 479,
     SUBROUTINE = 480,
     ERROR_TOK = 481,
     COMMON = 482,
     PARTITION = 483,
     ACTIVE = 484,
     FILTER = 485,
     ROW_MAJOR = 486,
     THEN = 487
   };
#endif
/* Tokens.  */
#define ATTRIBUTE 258
#define CONST_TOK 259
#define BOOL_TOK 260
#define FLOAT_TOK 261
#define INT_TOK 262
#define UINT_TOK 263
#define BREAK 264
#define CONTINUE 265
#define DO 266
#define ELSE 267
#define FOR 268
#define IF 269
#define DISCARD 270
#define RETURN 271
#define SWITCH 272
#define CASE 273
#define DEFAULT 274
#define BVEC2 275
#define BVEC3 276
#define BVEC4 277
#define IVEC2 278
#define IVEC3 279
#define IVEC4 280
#define UVEC2 281
#define UVEC3 282
#define UVEC4 283
#define VEC2 284
#define VEC3 285
#define VEC4 286
#define CENTROID 287
#define IN_TOK 288
#define OUT_TOK 289
#define INOUT_TOK 290
#define UNIFORM 291
#define VARYING 292
#define NOPERSPECTIVE 293
#define FLAT 294
#define SMOOTH 295
#define MAT2X2 296
#define MAT2X3 297
#define MAT2X4 298
#define MAT3X2 299
#define MAT3X3 300
#define MAT3X4 301
#define MAT4X2 302
#define MAT4X3 303
#define MAT4X4 304
#define SAMPLER1D 305
#define SAMPLER2D 306
#define SAMPLER3D 307
#define SAMPLERCUBE 308
#define SAMPLER1DSHADOW 309
#define SAMPLER2DSHADOW 310
#define SAMPLERCUBESHADOW 311
#define SAMPLER1DARRAY 312
#define SAMPLER2DARRAY 313
#define SAMPLER1DARRAYSHADOW 314
#define SAMPLER2DARRAYSHADOW 315
#define SAMPLERCUBEARRAY 316
#define SAMPLERCUBEARRAYSHADOW 317
#define ISAMPLER1D 318
#define ISAMPLER2D 319
#define ISAMPLER3D 320
#define ISAMPLERCUBE 321
#define ISAMPLER1DARRAY 322
#define ISAMPLER2DARRAY 323
#define ISAMPLERCUBEARRAY 324
#define USAMPLER1D 325
#define USAMPLER2D 326
#define USAMPLER3D 327
#define USAMPLERCUBE 328
#define USAMPLER1DARRAY 329
#define USAMPLER2DARRAY 330
#define USAMPLERCUBEARRAY 331
#define SAMPLER2DRECT 332
#define ISAMPLER2DRECT 333
#define USAMPLER2DRECT 334
#define SAMPLER2DRECTSHADOW 335
#define SAMPLERBUFFER 336
#define ISAMPLERBUFFER 337
#define USAMPLERBUFFER 338
#define SAMPLER2DMS 339
#define ISAMPLER2DMS 340
#define USAMPLER2DMS 341
#define SAMPLER2DMSARRAY 342
#define ISAMPLER2DMSARRAY 343
#define USAMPLER2DMSARRAY 344
#define SAMPLEREXTERNALOES 345
#define IMAGE1D 346
#define IMAGE2D 347
#define IMAGE3D 348
#define IMAGE2DRECT 349
#define IMAGECUBE 350
#define IMAGEBUFFER 351
#define IMAGE1DARRAY 352
#define IMAGE2DARRAY 353
#define IMAGECUBEARRAY 354
#define IMAGE2DMS 355
#define IMAGE2DMSARRAY 356
#define IIMAGE1D 357
#define IIMAGE2D 358
#define IIMAGE3D 359
#define IIMAGE2DRECT 360
#define IIMAGECUBE 361
#define IIMAGEBUFFER 362
#define IIMAGE1DARRAY 363
#define IIMAGE2DARRAY 364
#define IIMAGECUBEARRAY 365
#define IIMAGE2DMS 366
#define IIMAGE2DMSARRAY 367
#define UIMAGE1D 368
#define UIMAGE2D 369
#define UIMAGE3D 370
#define UIMAGE2DRECT 371
#define UIMAGECUBE 372
#define UIMAGEBUFFER 373
#define UIMAGE1DARRAY 374
#define UIMAGE2DARRAY 375
#define UIMAGECUBEARRAY 376
#define UIMAGE2DMS 377
#define UIMAGE2DMSARRAY 378
#define IMAGE1DSHADOW 379
#define IMAGE2DSHADOW 380
#define IMAGE1DARRAYSHADOW 381
#define IMAGE2DARRAYSHADOW 382
#define COHERENT 383
#define VOLATILE 384
#define RESTRICT 385
#define READONLY 386
#define WRITEONLY 387
#define ATOMIC_UINT 388
#define STRUCT 389
#define VOID_TOK 390
#define WHILE 391
#define IDENTIFIER 392
#define TYPE_IDENTIFIER 393
#define NEW_IDENTIFIER 394
#define FLOATCONSTANT 395
#define INTCONSTANT 396
#define UINTCONSTANT 397
#define BOOLCONSTANT 398
#define FIELD_SELECTION 399
#define LEFT_OP 400
#define RIGHT_OP 401
#define INC_OP 402
#define DEC_OP 403
#define LE_OP 404
#define GE_OP 405
#define EQ_OP 406
#define NE_OP 407
#define AND_OP 408
#define OR_OP 409
#define XOR_OP 410
#define MUL_ASSIGN 411
#define DIV_ASSIGN 412
#define ADD_ASSIGN 413
#define MOD_ASSIGN 414
#define LEFT_ASSIGN 415
#define RIGHT_ASSIGN 416
#define AND_ASSIGN 417
#define XOR_ASSIGN 418
#define OR_ASSIGN 419
#define SUB_ASSIGN 420
#define INVARIANT 421
#define LOWP 422
#define MEDIUMP 423
#define HIGHP 424
#define SUPERP 425
#define PRECISION 426
#define VERSION_TOK 427
#define EXTENSION 428
#define LINE 429
#define COLON 430
#define EOL 431
#define INTERFACE 432
#define OUTPUT 433
#define PRAGMA_DEBUG_ON 434
#define PRAGMA_DEBUG_OFF 435
#define PRAGMA_OPTIMIZE_ON 436
#define PRAGMA_OPTIMIZE_OFF 437
#define PRAGMA_INVARIANT_ALL 438
#define LAYOUT_TOK 439
#define ASM 440
#define CLASS 441
#define UNION 442
#define ENUM 443
#define TYPEDEF 444
#define TEMPLATE 445
#define THIS 446
#define PACKED_TOK 447
#define GOTO 448
#define INLINE_TOK 449
#define NOINLINE 450
#define PUBLIC_TOK 451
#define STATIC 452
#define EXTERN 453
#define EXTERNAL 454
#define LONG_TOK 455
#define SHORT_TOK 456
#define DOUBLE_TOK 457
#define HALF 458
#define FIXED_TOK 459
#define UNSIGNED 460
#define INPUT_TOK 461
#define OUPTUT 462
#define HVEC2 463
#define HVEC3 464
#define HVEC4 465
#define DVEC2 466
#define DVEC3 467
#define DVEC4 468
#define FVEC2 469
#define FVEC3 470
#define FVEC4 471
#define SAMPLER3DRECT 472
#define SIZEOF 473
#define CAST 474
#define NAMESPACE 475
#define USING 476
#define RESOURCE 477
#define PATCH 478
#define SAMPLE 479
#define SUBROUTINE 480
#define ERROR_TOK 481
#define COMMON 482
#define PARTITION 483
#define ACTIVE 484
#define FILTER 485
#define ROW_MAJOR 486
#define THEN 487




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 91 "/usr/xenocara/lib/libGL/generated/../../../dist/Mesa/src/glsl/glsl_parser.yy"
{
   int n;
   float real;
   const char *identifier;

   struct ast_type_qualifier type_qualifier;

   ast_node *node;
   ast_type_specifier *type_specifier;
   ast_array_specifier *array_specifier;
   ast_fully_specified_type *fully_specified_type;
   ast_function *function;
   ast_parameter_declarator *parameter_declarator;
   ast_function_definition *function_definition;
   ast_compound_statement *compound_statement;
   ast_expression *expression;
   ast_declarator_list *declarator_list;
   ast_struct_specifier *struct_specifier;
   ast_declaration *declaration;
   ast_switch_body *switch_body;
   ast_case_label *case_label;
   ast_case_label_list *case_label_list;
   ast_case_statement *case_statement;
   ast_case_statement_list *case_statement_list;
   ast_interface_block *interface_block;

   struct {
      ast_node *cond;
      ast_expression *rest;
   } for_rest_statement;

   struct {
      ast_node *then_statement;
      ast_node *else_statement;
   } selection_rest_statement;
}
/* Line 1529 of yacc.c.  */
#line 550 "glsl/glsl_parser.h"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
} YYLTYPE;
# define yyltype YYLTYPE /* obsolescent; will be withdrawn */
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif


