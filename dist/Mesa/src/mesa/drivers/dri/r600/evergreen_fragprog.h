/*
 * Copyright (C) 2008-2009  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Authors:
 *   Richard Li <RichardZ.Li@amd.com>, <richardradeon@gmail.com>
 */

#ifndef _EVERGREEN_FRAGPROG_H_
#define _EVERGREEN_FRAGPROG_H_

#include "r600_context.h"
#include "r700_assembler.h"

struct evergreen_fragment_program
{
	struct gl_fragment_program mesa_program;

    r700_AssemblerBase r700AsmCode;
	R700_Shader        r700Shader;

	GLboolean translated;
    GLboolean loaded;
	GLboolean error;

    void * shaderbo;

	GLuint k0used;
    void * constbo0;

	GLboolean WritesDepth;
	GLuint optimization;
};

/* Internal */
void evergreen_insert_wpos_code(struct gl_context *ctx, struct gl_fragment_program *fprog);

void evergreen_Map_Fragment_Program(r700_AssemblerBase         *pAsm,
			  struct gl_fragment_program *mesa_fp,
                          struct gl_context *ctx); 
GLboolean evergreen_Find_Instruction_Dependencies_fp(struct evergreen_fragment_program *fp,
					   struct gl_fragment_program   *mesa_fp);

GLboolean evergreenTranslateFragmentShader(struct evergreen_fragment_program *fp,
				      struct gl_fragment_program   *mesa_vp,
                                      struct gl_context *ctx); 

/* Interface */
extern void evergreenSelectFragmentShader(struct gl_context *ctx);

extern GLboolean evergreenSetupFragmentProgram(struct gl_context * ctx);

extern GLboolean evergreenSetupFPconstants(struct gl_context * ctx);

extern void *    evergreenGetActiveFpShaderBo(struct gl_context * ctx);

extern void *    evergreenGetActiveFpShaderConstBo(struct gl_context * ctx);

#endif /*_EVERGREEN_FRAGPROG_H_*/
