/**************************************************************************

Copyright (C) 2005 Aapo Tahkola.

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
on the rights to use, copy, modify, merge, publish, distribute, sub
license, and/or sell copies of the Software, and to permit persons to whom
the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

/*
 * Authors:
 *   Aapo Tahkola <aet@rasterburn.org>
 */
#include "glheader.h"
#include "macros.h"
#include "enums.h"
#include "program.h"
#include "nvvertexec.h"

#include "r300_context.h"
#include "r300_program.h"
#include "program_instruction.h"

#if SWIZZLE_X != VSF_IN_COMPONENT_X || \
    SWIZZLE_Y != VSF_IN_COMPONENT_Y || \
    SWIZZLE_Z != VSF_IN_COMPONENT_Z || \
    SWIZZLE_W != VSF_IN_COMPONENT_W || \
    SWIZZLE_ZERO != VSF_IN_COMPONENT_ZERO || \
    SWIZZLE_ONE != VSF_IN_COMPONENT_ONE || \
    WRITEMASK_X != VSF_FLAG_X || \
    WRITEMASK_Y != VSF_FLAG_Y || \
    WRITEMASK_Z != VSF_FLAG_Z || \
    WRITEMASK_W != VSF_FLAG_W
#error Cannot change these!
#endif
    
#define SCALAR_FLAG (1<<31)
#define FLAG_MASK (1<<31)
#define OP_MASK	(0xf)  /* we are unlikely to have more than 15 */
#define OPN(operator, ip) {#operator, OPCODE_##operator, ip}

static struct{
	char *name;
	int opcode;
	unsigned long ip; /* number of input operands and flags */
}op_names[]={
	OPN(ABS, 1),
	OPN(ADD, 2),
	OPN(ARL, 1|SCALAR_FLAG),
	OPN(DP3, 2),
	OPN(DP4, 2),
	OPN(DPH, 2),
	OPN(DST, 2),
	OPN(EX2, 1|SCALAR_FLAG),
	OPN(EXP, 1|SCALAR_FLAG),
	OPN(FLR, 1),
	OPN(FRC, 1),
	OPN(LG2, 1|SCALAR_FLAG),
	OPN(LIT, 1),
	OPN(LOG, 1|SCALAR_FLAG),
	OPN(MAD, 3),
	OPN(MAX, 2),
	OPN(MIN, 2),
	OPN(MOV, 1),
	OPN(MUL, 2),
	OPN(POW, 2|SCALAR_FLAG),
	OPN(RCP, 1|SCALAR_FLAG),
	OPN(RSQ, 1|SCALAR_FLAG),
	OPN(SGE, 2),
	OPN(SLT, 2),
	OPN(SUB, 2),
	OPN(SWZ, 1),
	OPN(XPD, 2),
	OPN(RCC, 0), //extra
	OPN(PRINT, 0),
	OPN(END, 0),
};
#undef OPN
	
int r300VertexProgUpdateParams(GLcontext *ctx, struct r300_vertex_program *vp, float *dst)
{
	int pi;
	struct gl_vertex_program *mesa_vp = &vp->mesa_program;
	float *dst_o=dst;
        struct gl_program_parameter_list *paramList;
	
	if (mesa_vp->IsNVProgram) {
		_mesa_init_vp_per_primitive_registers(ctx);
		
		for (pi=0; pi < MAX_NV_VERTEX_PROGRAM_PARAMS; pi++) {
			*dst++=ctx->VertexProgram.Parameters[pi][0];
			*dst++=ctx->VertexProgram.Parameters[pi][1];
			*dst++=ctx->VertexProgram.Parameters[pi][2];
			*dst++=ctx->VertexProgram.Parameters[pi][3];
		}
		return dst - dst_o;
	}
	
	assert(mesa_vp->Base.Parameters);
	_mesa_load_state_parameters(ctx, mesa_vp->Base.Parameters);
	
	if(mesa_vp->Base.Parameters->NumParameters * 4 > VSF_MAX_FRAGMENT_LENGTH){
		fprintf(stderr, "%s:Params exhausted\n", __FUNCTION__);
		exit(-1);
	}
	
        paramList = mesa_vp->Base.Parameters;
	for(pi=0; pi < paramList->NumParameters; pi++){
		switch(paramList->Parameters[pi].Type){
			
		case PROGRAM_STATE_VAR:
		case PROGRAM_NAMED_PARAM:
			//fprintf(stderr, "%s", vp->Parameters->Parameters[pi].Name);
		case PROGRAM_CONSTANT:
			*dst++=paramList->ParameterValues[pi][0];
			*dst++=paramList->ParameterValues[pi][1];
			*dst++=paramList->ParameterValues[pi][2];
			*dst++=paramList->ParameterValues[pi][3];
		break;
		
		default: _mesa_problem(NULL, "Bad param type in %s", __FUNCTION__);
		}
	
	}
	
	return dst - dst_o;
}
		
static unsigned long t_dst_mask(GLuint mask)
{
	/* WRITEMASK_* is equivalent to VSF_FLAG_* */
	return mask & VSF_FLAG_ALL;
}

static unsigned long t_dst_class(enum register_file file)
{
	
	switch(file){
		case PROGRAM_TEMPORARY:
			return VSF_OUT_CLASS_TMP;
		case PROGRAM_OUTPUT:
			return VSF_OUT_CLASS_RESULT;
		case PROGRAM_ADDRESS:
			return VSF_OUT_CLASS_ADDR;
		/*	
		case PROGRAM_INPUT:
		case PROGRAM_LOCAL_PARAM:
		case PROGRAM_ENV_PARAM:
		case PROGRAM_NAMED_PARAM:
		case PROGRAM_STATE_VAR:
		case PROGRAM_WRITE_ONLY:
		case PROGRAM_ADDRESS:
		*/
		default:
			fprintf(stderr, "problem in %s", __FUNCTION__);
			exit(0);
	}
}

static unsigned long t_dst_index(struct r300_vertex_program *vp, struct prog_dst_register *dst)
{
	if(dst->File == PROGRAM_OUTPUT) {
		if (vp->outputs[dst->Index] != -1)
			return vp->outputs[dst->Index];
		else {
			WARN_ONCE("Unknown output %d\n", dst->Index);
			return 10;
		}
	}else if(dst->File == PROGRAM_ADDRESS) {
		assert(dst->Index == 0);
	}
	
	return dst->Index;
}

static unsigned long t_src_class(enum register_file file)
{
	
	switch(file){
		case PROGRAM_TEMPORARY:
			return VSF_IN_CLASS_TMP;
			
		case PROGRAM_INPUT:
			return VSF_IN_CLASS_ATTR;
			
		case PROGRAM_LOCAL_PARAM:
		case PROGRAM_ENV_PARAM:
		case PROGRAM_NAMED_PARAM:
		case PROGRAM_STATE_VAR:
			return VSF_IN_CLASS_PARAM;
		/*	
		case PROGRAM_OUTPUT:
		case PROGRAM_WRITE_ONLY:
		case PROGRAM_ADDRESS:
		*/
		default:
			fprintf(stderr, "problem in %s", __FUNCTION__);
			exit(0);
	}
}

static __inline unsigned long t_swizzle(GLubyte swizzle)
{
/* this is in fact a NOP as the Mesa SWIZZLE_* are all identical to VSF_IN_COMPONENT_* */
	return swizzle;
}

#if 0
static void vp_dump_inputs(struct r300_vertex_program *vp, char *caller)
{
	int i;
	
	if(vp == NULL){
		fprintf(stderr, "vp null in call to %s from %s\n", __FUNCTION__, caller);
		return ;
	}
	
	fprintf(stderr, "%s:<", caller);
	for(i=0; i < VERT_ATTRIB_MAX; i++)
		fprintf(stderr, "%d ", vp->inputs[i]);
	fprintf(stderr, ">\n");
	
}
#endif

static unsigned long t_src_index(struct r300_vertex_program *vp, struct prog_src_register *src)
{
	int i;
	int max_reg=-1;
	
	if(src->File == PROGRAM_INPUT){
		if(vp->inputs[src->Index] != -1)
			return vp->inputs[src->Index];
		
		for(i=0; i < VERT_ATTRIB_MAX; i++)
			if(vp->inputs[i] > max_reg)
				max_reg=vp->inputs[i];
		
		vp->inputs[src->Index]=max_reg+1;
		
		//vp_dump_inputs(vp, __FUNCTION__);	
		
		return vp->inputs[src->Index];
	}else{
		if (src->Index < 0) {
			fprintf(stderr, "WARNING negative offsets for indirect addressing do not work\n");
			return 0;
		}
		return src->Index;
	}
}

static unsigned long t_src(struct r300_vertex_program *vp, struct prog_src_register *src)
{
	/* src->NegateBase uses the NEGATE_ flags from program_instruction.h,
	 * which equal our VSF_FLAGS_ values, so it's safe to just pass it here.
	 */
	return MAKE_VSF_SOURCE(t_src_index(vp, src),
				t_swizzle(GET_SWZ(src->Swizzle, 0)),
				t_swizzle(GET_SWZ(src->Swizzle, 1)),
				t_swizzle(GET_SWZ(src->Swizzle, 2)),
				t_swizzle(GET_SWZ(src->Swizzle, 3)),
				t_src_class(src->File),
				src->NegateBase) | (src->RelAddr << 4);
}

static unsigned long t_src_scalar(struct r300_vertex_program *vp, struct prog_src_register *src)
{
			
	return MAKE_VSF_SOURCE(t_src_index(vp, src),
				t_swizzle(GET_SWZ(src->Swizzle, 0)),
				t_swizzle(GET_SWZ(src->Swizzle, 0)),
				t_swizzle(GET_SWZ(src->Swizzle, 0)),
				t_swizzle(GET_SWZ(src->Swizzle, 0)),
				t_src_class(src->File),
				src->NegateBase ? VSF_FLAG_ALL : VSF_FLAG_NONE) | (src->RelAddr << 4);
}

static unsigned long t_opcode(enum prog_opcode opcode)
{

	switch(opcode){
		case OPCODE_ARL: return R300_VPI_OUT_OP_ARL;
		case OPCODE_DST: return R300_VPI_OUT_OP_DST;
		case OPCODE_EX2: return R300_VPI_OUT_OP_EX2;
		case OPCODE_EXP: return R300_VPI_OUT_OP_EXP;
		case OPCODE_FRC: return R300_VPI_OUT_OP_FRC;
		case OPCODE_LG2: return R300_VPI_OUT_OP_LG2;
		case OPCODE_LOG: return R300_VPI_OUT_OP_LOG;
		case OPCODE_MAX: return R300_VPI_OUT_OP_MAX;
		case OPCODE_MIN: return R300_VPI_OUT_OP_MIN;
		case OPCODE_MUL: return R300_VPI_OUT_OP_MUL;
		case OPCODE_RCP: return R300_VPI_OUT_OP_RCP;
		case OPCODE_RSQ: return R300_VPI_OUT_OP_RSQ;
		case OPCODE_SGE: return R300_VPI_OUT_OP_SGE;
		case OPCODE_SLT: return R300_VPI_OUT_OP_SLT;
		case OPCODE_DP4: return R300_VPI_OUT_OP_DOT;
		
		default: 
			fprintf(stderr, "%s: Should not be called with opcode %d!", __FUNCTION__, opcode);
	}
	exit(-1);
	return 0;
}

static unsigned long op_operands(enum prog_opcode opcode)
{
	int i;
	
	/* Can we trust mesas opcodes to be in order ? */
	for(i=0; i < sizeof(op_names) / sizeof(*op_names); i++)
		if(op_names[i].opcode == opcode)
			return op_names[i].ip;
	
	fprintf(stderr, "op %d not found in op_names\n", opcode);
	exit(-1);
	return 0;
}

/* TODO: Get rid of t_src_class call */
#define CMP_SRCS(a, b) ((a.RelAddr != b.RelAddr) || (a.Index != b.Index && \
		       ((t_src_class(a.File) == VSF_IN_CLASS_PARAM && \
			 t_src_class(b.File) == VSF_IN_CLASS_PARAM) || \
			(t_src_class(a.File) == VSF_IN_CLASS_ATTR && \
			 t_src_class(b.File) == VSF_IN_CLASS_ATTR)))) \
			 
#define ZERO_SRC_0 (MAKE_VSF_SOURCE(t_src_index(vp, &src[0]), \
				    SWIZZLE_ZERO, SWIZZLE_ZERO, \
				    SWIZZLE_ZERO, SWIZZLE_ZERO, \
				    t_src_class(src[0].File), VSF_FLAG_NONE) | (src[0].RelAddr << 4))
				   
#define ZERO_SRC_1 (MAKE_VSF_SOURCE(t_src_index(vp, &src[1]), \
				    SWIZZLE_ZERO, SWIZZLE_ZERO, \
				    SWIZZLE_ZERO, SWIZZLE_ZERO, \
				    t_src_class(src[1].File), VSF_FLAG_NONE) | (src[1].RelAddr << 4))

#define ZERO_SRC_2 (MAKE_VSF_SOURCE(t_src_index(vp, &src[2]), \
				    SWIZZLE_ZERO, SWIZZLE_ZERO, \
				    SWIZZLE_ZERO, SWIZZLE_ZERO, \
				    t_src_class(src[2].File), VSF_FLAG_NONE) | (src[2].RelAddr << 4))
				   
#define ONE_SRC_0 (MAKE_VSF_SOURCE(t_src_index(vp, &src[0]), \
				    SWIZZLE_ONE, SWIZZLE_ONE, \
				    SWIZZLE_ONE, SWIZZLE_ONE, \
				    t_src_class(src[0].File), VSF_FLAG_NONE) | (src[0].RelAddr << 4))
				   
#define ONE_SRC_1 (MAKE_VSF_SOURCE(t_src_index(vp, &src[1]), \
				    SWIZZLE_ONE, SWIZZLE_ONE, \
				    SWIZZLE_ONE, SWIZZLE_ONE, \
				    t_src_class(src[1].File), VSF_FLAG_NONE) | (src[1].RelAddr << 4))

#define ONE_SRC_2 (MAKE_VSF_SOURCE(t_src_index(vp, &src[2]), \
				    SWIZZLE_ONE, SWIZZLE_ONE, \
				    SWIZZLE_ONE, SWIZZLE_ONE, \
				    t_src_class(src[2].File), VSF_FLAG_NONE) | (src[2].RelAddr << 4))
				   
/* DP4 version seems to trigger some hw peculiarity */
//#define PREFER_DP4

#define FREE_TEMPS() \
	do { \
		if(u_temp_i < vp->num_temporaries) { \
			WARN_ONCE("Ran out of temps, num temps %d, us %d\n", vp->num_temporaries, u_temp_i); \
			vp->native = GL_FALSE; \
		} \
		u_temp_i=VSF_MAX_FRAGMENT_TEMPS-1; \
	} while (0)

void r300_translate_vertex_shader(struct r300_vertex_program *vp)
{
	struct gl_vertex_program *mesa_vp= &vp->mesa_program;
	struct prog_instruction *vpi;
	int i, cur_reg=0;
	VERTEX_SHADER_INSTRUCTION *o_inst;
	unsigned long operands;
	int are_srcs_scalar;
	unsigned long hw_op;
	/* Initial value should be last tmp reg that hw supports.
	   Strangely enough r300 doesnt mind even though these would be out of range.
	   Smart enough to realize that it doesnt need it? */
	int u_temp_i=VSF_MAX_FRAGMENT_TEMPS-1;
	struct prog_src_register src[3];

	if (mesa_vp->Base.NumInstructions == 0)
		return;

	if (getenv("R300_VP_SAFETY")) {
		WARN_ONCE("R300_VP_SAFETY enabled.\n");
		
		vpi = malloc((mesa_vp->Base.NumInstructions + VSF_MAX_FRAGMENT_TEMPS) * sizeof(struct prog_instruction));
		memset(vpi, 0, VSF_MAX_FRAGMENT_TEMPS * sizeof(struct prog_instruction));
		
		for (i=0; i < VSF_MAX_FRAGMENT_TEMPS; i++) {
			vpi[i].Opcode = OPCODE_MOV;
			vpi[i].StringPos = 0;
			vpi[i].Data = 0;
			
			vpi[i].DstReg.File = PROGRAM_TEMPORARY;
			vpi[i].DstReg.Index = i;
			vpi[i].DstReg.WriteMask = WRITEMASK_XYZW;
			vpi[i].DstReg.CondMask = COND_TR;
					
			vpi[i].SrcReg[0].File = PROGRAM_STATE_VAR;
			vpi[i].SrcReg[0].Index = 0;
			vpi[i].SrcReg[0].Swizzle = MAKE_SWIZZLE4(SWIZZLE_ONE, SWIZZLE_ONE, SWIZZLE_ONE, SWIZZLE_ONE);
		}
		
		memcpy(&vpi[i], mesa_vp->Base.Instructions, mesa_vp->Base.NumInstructions * sizeof(struct prog_instruction));
		
		free(mesa_vp->Base.Instructions);
		
		mesa_vp->Base.Instructions = vpi;
		
		mesa_vp->Base.NumInstructions += VSF_MAX_FRAGMENT_TEMPS;
		vpi = &mesa_vp->Base.Instructions[mesa_vp->Base.NumInstructions-1];
		
		assert(vpi->Opcode == OPCODE_END);
	}
	
	if (mesa_vp->IsPositionInvariant) {
		struct gl_program_parameter_list *paramList;
		GLint tokens[6] = { STATE_MATRIX, STATE_MVP, 0, 0, 0, STATE_MATRIX };

#ifdef PREFER_DP4
		tokens[5] = STATE_MATRIX;
#else
		tokens[5] = STATE_MATRIX_TRANSPOSE;
#endif
		paramList = mesa_vp->Base.Parameters;
		
		vpi = malloc((mesa_vp->Base.NumInstructions + 4) * sizeof(struct prog_instruction));
		memset(vpi, 0, 4 * sizeof(struct prog_instruction));
		
		for (i=0; i < 4; i++) {
			GLint idx;
			tokens[3] = tokens[4] = i;
			idx = _mesa_add_state_reference(paramList, tokens);
#ifdef PREFER_DP4
			vpi[i].Opcode = OPCODE_DP4;
			vpi[i].StringPos = 0;
			vpi[i].Data = 0;
			
			vpi[i].DstReg.File = PROGRAM_OUTPUT;
			vpi[i].DstReg.Index = VERT_RESULT_HPOS;
			vpi[i].DstReg.WriteMask = 1 << i;
			vpi[i].DstReg.CondMask = COND_TR;
					
			vpi[i].SrcReg[0].File = PROGRAM_STATE_VAR;
			vpi[i].SrcReg[0].Index = idx;
			vpi[i].SrcReg[0].Swizzle = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_W);
			
			vpi[i].SrcReg[1].File = PROGRAM_INPUT;
			vpi[i].SrcReg[1].Index = VERT_ATTRIB_POS;
			vpi[i].SrcReg[1].Swizzle = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_W);
#else
			if (i == 0)
				vpi[i].Opcode = OPCODE_MUL;
			else
				vpi[i].Opcode = OPCODE_MAD;
			
			vpi[i].StringPos = 0;
			vpi[i].Data = 0;
			
			if (i == 3)
				vpi[i].DstReg.File = PROGRAM_OUTPUT;
			else
				vpi[i].DstReg.File = PROGRAM_TEMPORARY;
			vpi[i].DstReg.Index = 0;
			vpi[i].DstReg.WriteMask = 0xf;
			vpi[i].DstReg.CondMask = COND_TR;
					
			vpi[i].SrcReg[0].File = PROGRAM_STATE_VAR;
			vpi[i].SrcReg[0].Index = idx;
			vpi[i].SrcReg[0].Swizzle = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_W);
			
			vpi[i].SrcReg[1].File = PROGRAM_INPUT;
			vpi[i].SrcReg[1].Index = VERT_ATTRIB_POS;
			vpi[i].SrcReg[1].Swizzle = MAKE_SWIZZLE4(i, i, i, i);
			
			if (i > 0) {
				vpi[i].SrcReg[2].File = PROGRAM_TEMPORARY;
				vpi[i].SrcReg[2].Index = 0;
				vpi[i].SrcReg[2].Swizzle = MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_W);
			}
#endif					
		}
		
		memcpy(&vpi[i], mesa_vp->Base.Instructions, mesa_vp->Base.NumInstructions * sizeof(struct prog_instruction));
		
		free(mesa_vp->Base.Instructions);
		
		mesa_vp->Base.Instructions = vpi;
		
		mesa_vp->Base.NumInstructions += 4;
		vpi = &mesa_vp->Base.Instructions[mesa_vp->Base.NumInstructions-1];
		
		assert(vpi->Opcode == OPCODE_END);
		
		mesa_vp->Base.InputsRead |= (1 << VERT_ATTRIB_POS);
		mesa_vp->Base.OutputsWritten |= (1 << VERT_RESULT_HPOS);
		
		//fprintf(stderr, "IsPositionInvariant is set!\n");
		//_mesa_print_program(&mesa_vp->Base);
	}
	
	vp->pos_end=0; /* Not supported yet */
	vp->program.length=0;
	vp->num_temporaries=mesa_vp->Base.NumTemporaries;
	
	for(i=0; i < VERT_ATTRIB_MAX; i++)
		vp->inputs[i] = -1;

	for(i=0; i < VERT_RESULT_MAX; i++)
		vp->outputs[i] = -1;
	
	assert(mesa_vp->Base.OutputsWritten & (1 << VERT_RESULT_HPOS));
	
	/* Assign outputs */
	if(mesa_vp->Base.OutputsWritten & (1 << VERT_RESULT_HPOS))
		vp->outputs[VERT_RESULT_HPOS] = cur_reg++;
	
	if(mesa_vp->Base.OutputsWritten & (1 << VERT_RESULT_PSIZ))
		vp->outputs[VERT_RESULT_PSIZ] = cur_reg++;
	
	if(mesa_vp->Base.OutputsWritten & (1 << VERT_RESULT_COL0))
		vp->outputs[VERT_RESULT_COL0] = cur_reg++;
	
	if(mesa_vp->Base.OutputsWritten & (1 << VERT_RESULT_COL1))
		vp->outputs[VERT_RESULT_COL1] = cur_reg++;
	
#if 0 /* Not supported yet */
	if(mesa_vp->Base.OutputsWritten & (1 << VERT_RESULT_BFC0))
		vp->outputs[VERT_RESULT_BFC0] = cur_reg++;
	
	if(mesa_vp->Base.OutputsWritten & (1 << VERT_RESULT_BFC1))
		vp->outputs[VERT_RESULT_BFC1] = cur_reg++;
	
	if(mesa_vp->Base.OutputsWritten & (1 << VERT_RESULT_FOGC))
		vp->outputs[VERT_RESULT_FOGC] = cur_reg++;
#endif
	
	for(i=VERT_RESULT_TEX0; i <= VERT_RESULT_TEX7; i++)
		if(mesa_vp->Base.OutputsWritten & (1 << i))
			vp->outputs[i] = cur_reg++;
	
	vp->translated = GL_TRUE;
	vp->native = GL_TRUE;
	
	o_inst=vp->program.body.i;
	for(vpi=mesa_vp->Base.Instructions; vpi->Opcode != OPCODE_END; vpi++, o_inst++){
		FREE_TEMPS();
		
		operands=op_operands(vpi->Opcode);
		are_srcs_scalar=operands & SCALAR_FLAG;
		operands &= OP_MASK;
		
		for(i=0; i < operands; i++)
			src[i]=vpi->SrcReg[i];
		
		if(operands == 3){ /* TODO: scalars */
			if( CMP_SRCS(src[1], src[2]) || CMP_SRCS(src[0], src[2]) ){
				o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_ADD, u_temp_i,
						VSF_FLAG_ALL, VSF_OUT_CLASS_TMP);
				
				o_inst->src1=MAKE_VSF_SOURCE(t_src_index(vp, &src[2]),
						SWIZZLE_X, SWIZZLE_Y,
						SWIZZLE_Z, SWIZZLE_W,
						t_src_class(src[2].File), VSF_FLAG_NONE) | (src[2].RelAddr << 4);

				o_inst->src2=ZERO_SRC_2;
				o_inst->src3=ZERO_SRC_2;
				o_inst++;
						
				src[2].File=PROGRAM_TEMPORARY;
				src[2].Index=u_temp_i;
				src[2].RelAddr=0;
				u_temp_i--;
			}
			
		}
		
		if(operands >= 2){
			if( CMP_SRCS(src[1], src[0]) ){
				o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_ADD, u_temp_i,
						VSF_FLAG_ALL, VSF_OUT_CLASS_TMP);
				
				o_inst->src1=MAKE_VSF_SOURCE(t_src_index(vp, &src[0]),
						SWIZZLE_X, SWIZZLE_Y,
						SWIZZLE_Z, SWIZZLE_W,
						t_src_class(src[0].File), VSF_FLAG_NONE) | (src[0].RelAddr << 4);

				o_inst->src2=ZERO_SRC_0;
				o_inst->src3=ZERO_SRC_0;
				o_inst++;
						
				src[0].File=PROGRAM_TEMPORARY;
				src[0].Index=u_temp_i;
				src[0].RelAddr=0;
				u_temp_i--;
			}
		}
		
		/* These ops need special handling. */
		switch(vpi->Opcode){
		case OPCODE_POW:
			o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_POW, t_dst_index(vp, &vpi->DstReg),
					t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			o_inst->src1=t_src_scalar(vp, &src[0]);
			o_inst->src2=ZERO_SRC_0;
			o_inst->src3=t_src_scalar(vp, &src[1]);
			goto next;
			
		case OPCODE_MOV://ADD RESULT 1.X Y Z W PARAM 0{} {X Y Z W} PARAM 0{} {ZERO ZERO ZERO ZERO} 
		case OPCODE_SWZ:
#if 1
			o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_ADD, t_dst_index(vp, &vpi->DstReg),
					t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			o_inst->src1=t_src(vp, &src[0]);
			o_inst->src2=ZERO_SRC_0;
			o_inst->src3=ZERO_SRC_0;
#else
			hw_op=(src[0].File == PROGRAM_TEMPORARY) ? R300_VPI_OUT_OP_MAD_2 : R300_VPI_OUT_OP_MAD;
			
			o_inst->op=MAKE_VSF_OP(hw_op, t_dst_index(vp, &vpi->DstReg),
				t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			o_inst->src1=t_src(vp, &src[0]);
			o_inst->src2=ONE_SRC_0;
			o_inst->src3=ZERO_SRC_0;
#endif			

			goto next;
			
		case OPCODE_ADD:
#if 1
			hw_op=(src[0].File == PROGRAM_TEMPORARY &&
				src[1].File == PROGRAM_TEMPORARY) ? R300_VPI_OUT_OP_MAD_2 : R300_VPI_OUT_OP_MAD;
			
			o_inst->op=MAKE_VSF_OP(hw_op, t_dst_index(vp, &vpi->DstReg),
				t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			o_inst->src1=ONE_SRC_0;
			o_inst->src2=t_src(vp, &src[0]);
			o_inst->src3=t_src(vp, &src[1]);
#else
			o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_ADD, t_dst_index(vp, &vpi->DstReg),
					t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			o_inst->src1=t_src(vp, &src[0]);
			o_inst->src2=t_src(vp, &src[1]);
			o_inst->src3=ZERO_SRC_1;
			
#endif
			goto next;
			
		case OPCODE_MAD:
			hw_op=(src[0].File == PROGRAM_TEMPORARY &&
				src[1].File == PROGRAM_TEMPORARY &&
				src[2].File == PROGRAM_TEMPORARY) ? R300_VPI_OUT_OP_MAD_2 : R300_VPI_OUT_OP_MAD;
			
			o_inst->op=MAKE_VSF_OP(hw_op, t_dst_index(vp, &vpi->DstReg),
				t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			o_inst->src1=t_src(vp, &src[0]);
			o_inst->src2=t_src(vp, &src[1]);
			o_inst->src3=t_src(vp, &src[2]);
			goto next;
			
		case OPCODE_MUL: /* HW mul can take third arg but appears to have some other limitations. */
			hw_op=(src[0].File == PROGRAM_TEMPORARY &&
				src[1].File == PROGRAM_TEMPORARY) ? R300_VPI_OUT_OP_MAD_2 : R300_VPI_OUT_OP_MAD;
			
			o_inst->op=MAKE_VSF_OP(hw_op, t_dst_index(vp, &vpi->DstReg),
				t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			o_inst->src1=t_src(vp, &src[0]);
			o_inst->src2=t_src(vp, &src[1]);

			o_inst->src3=ZERO_SRC_1;
			goto next;
			
		case OPCODE_DP3://DOT RESULT 1.X Y Z W PARAM 0{} {X Y Z ZERO} PARAM 0{} {X Y Z ZERO} 
			o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_DOT, t_dst_index(vp, &vpi->DstReg),
					t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			
			o_inst->src1=MAKE_VSF_SOURCE(t_src_index(vp, &src[0]),
					t_swizzle(GET_SWZ(src[0].Swizzle, 0)),
					t_swizzle(GET_SWZ(src[0].Swizzle, 1)),
					t_swizzle(GET_SWZ(src[0].Swizzle, 2)),
					SWIZZLE_ZERO,
					t_src_class(src[0].File),
					src[0].NegateBase ? VSF_FLAG_XYZ : VSF_FLAG_NONE) | (src[0].RelAddr << 4);
			
			o_inst->src2=MAKE_VSF_SOURCE(t_src_index(vp, &src[1]),
					t_swizzle(GET_SWZ(src[1].Swizzle, 0)),
					t_swizzle(GET_SWZ(src[1].Swizzle, 1)),
					t_swizzle(GET_SWZ(src[1].Swizzle, 2)),
					SWIZZLE_ZERO,
					t_src_class(src[1].File),
					src[1].NegateBase ? VSF_FLAG_XYZ : VSF_FLAG_NONE) | (src[1].RelAddr << 4);

			o_inst->src3=ZERO_SRC_1;
			goto next;

		case OPCODE_SUB://ADD RESULT 1.X Y Z W TMP 0{} {X Y Z W} PARAM 1{X Y Z W } {X Y Z W} neg Xneg Yneg Zneg W
#if 1
			hw_op=(src[0].File == PROGRAM_TEMPORARY &&
				src[1].File == PROGRAM_TEMPORARY) ? R300_VPI_OUT_OP_MAD_2 : R300_VPI_OUT_OP_MAD;
			
			o_inst->op=MAKE_VSF_OP(hw_op, t_dst_index(vp, &vpi->DstReg),
				t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			o_inst->src1=t_src(vp, &src[0]);
			o_inst->src2=ONE_SRC_0;
			o_inst->src3=MAKE_VSF_SOURCE(t_src_index(vp, &src[1]),
					t_swizzle(GET_SWZ(src[1].Swizzle, 0)),
					t_swizzle(GET_SWZ(src[1].Swizzle, 1)),
					t_swizzle(GET_SWZ(src[1].Swizzle, 2)),
					t_swizzle(GET_SWZ(src[1].Swizzle, 3)),
					t_src_class(src[1].File),
					(!src[1].NegateBase) ? VSF_FLAG_ALL : VSF_FLAG_NONE)  | (src[1].RelAddr << 4);
#else
			o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_ADD, t_dst_index(vp, &vpi->DstReg),
					t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			
			o_inst->src1=t_src(vp, &src[0]);
			o_inst->src2=MAKE_VSF_SOURCE(t_src_index(vp, &src[1]),
					t_swizzle(GET_SWZ(src[1].Swizzle, 0)),
					t_swizzle(GET_SWZ(src[1].Swizzle, 1)),
					t_swizzle(GET_SWZ(src[1].Swizzle, 2)),
					t_swizzle(GET_SWZ(src[1].Swizzle, 3)),
					t_src_class(src[1].File),
					(!src[1].NegateBase) ? VSF_FLAG_ALL : VSF_FLAG_NONE) | (src[1].RelAddr << 4);
			o_inst->src3=0;
#endif
			goto next;
			
		case OPCODE_ABS://MAX RESULT 1.X Y Z W PARAM 0{} {X Y Z W} PARAM 0{X Y Z W } {X Y Z W} neg Xneg Yneg Zneg W
			o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_MAX, t_dst_index(vp, &vpi->DstReg),
					t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			
			o_inst->src1=t_src(vp, &src[0]);
			o_inst->src2=MAKE_VSF_SOURCE(t_src_index(vp, &src[0]),
					t_swizzle(GET_SWZ(src[0].Swizzle, 0)),
					t_swizzle(GET_SWZ(src[0].Swizzle, 1)),
					t_swizzle(GET_SWZ(src[0].Swizzle, 2)),
					t_swizzle(GET_SWZ(src[0].Swizzle, 3)),
					t_src_class(src[0].File),
					(!src[0].NegateBase) ? VSF_FLAG_ALL : VSF_FLAG_NONE) | (src[0].RelAddr << 4);
			o_inst->src3=0;
			goto next;
			
		case OPCODE_FLR:
		/* FRC TMP 0.X Y Z W PARAM 0{} {X Y Z W} 
		   ADD RESULT 1.X Y Z W PARAM 0{} {X Y Z W} TMP 0{X Y Z W } {X Y Z W} neg Xneg Yneg Zneg W */

			o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_FRC, u_temp_i,
					t_dst_mask(vpi->DstReg.WriteMask), VSF_OUT_CLASS_TMP);
			
			o_inst->src1=t_src(vp, &src[0]);
			o_inst->src2=ZERO_SRC_0;
			o_inst->src3=ZERO_SRC_0;
			o_inst++;
			
			o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_ADD, t_dst_index(vp, &vpi->DstReg),
					t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			
			o_inst->src1=t_src(vp, &src[0]);
			o_inst->src2=MAKE_VSF_SOURCE(u_temp_i,
					VSF_IN_COMPONENT_X,
					VSF_IN_COMPONENT_Y,
					VSF_IN_COMPONENT_Z,
					VSF_IN_COMPONENT_W,
					VSF_IN_CLASS_TMP,
					/* Not 100% sure about this */
					(!src[0].NegateBase) ? VSF_FLAG_ALL : VSF_FLAG_NONE/*VSF_FLAG_ALL*/);

			o_inst->src3=ZERO_SRC_0;
			u_temp_i--;
			goto next;
			
		case OPCODE_LG2:// LG2 RESULT 1.X Y Z W PARAM 0{} {X X X X}
			o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_LG2, t_dst_index(vp, &vpi->DstReg),
					t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			
			o_inst->src1=MAKE_VSF_SOURCE(t_src_index(vp, &src[0]),
					t_swizzle(GET_SWZ(src[0].Swizzle, 0)),
					t_swizzle(GET_SWZ(src[0].Swizzle, 0)),
					t_swizzle(GET_SWZ(src[0].Swizzle, 0)),
					t_swizzle(GET_SWZ(src[0].Swizzle, 0)),
					t_src_class(src[0].File),
					src[0].NegateBase ? VSF_FLAG_ALL : VSF_FLAG_NONE) | (src[0].RelAddr << 4);
			o_inst->src2=ZERO_SRC_0;
			o_inst->src3=ZERO_SRC_0;
			goto next;
			
		case OPCODE_LIT://LIT TMP 1.Y Z TMP 1{} {X W Z Y} TMP 1{} {Y W Z X} TMP 1{} {Y X Z W} 
			o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_LIT, t_dst_index(vp, &vpi->DstReg),
					t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			/* NOTE: Users swizzling might not work. */
			o_inst->src1=MAKE_VSF_SOURCE(t_src_index(vp, &src[0]),
					t_swizzle(GET_SWZ(src[0].Swizzle, 0)), // x
					t_swizzle(GET_SWZ(src[0].Swizzle, 3)), // w
					VSF_IN_COMPONENT_ZERO, // z
					t_swizzle(GET_SWZ(src[0].Swizzle, 1)), // y
					t_src_class(src[0].File),
					src[0].NegateBase ? VSF_FLAG_ALL : VSF_FLAG_NONE) | (src[0].RelAddr << 4);
			o_inst->src2=MAKE_VSF_SOURCE(t_src_index(vp, &src[0]),
					t_swizzle(GET_SWZ(src[0].Swizzle, 1)), // y
					t_swizzle(GET_SWZ(src[0].Swizzle, 3)), // w
					VSF_IN_COMPONENT_ZERO, // z
					t_swizzle(GET_SWZ(src[0].Swizzle, 0)), // x
					t_src_class(src[0].File),
					src[0].NegateBase ? VSF_FLAG_ALL : VSF_FLAG_NONE) | (src[0].RelAddr << 4);
			o_inst->src3=MAKE_VSF_SOURCE(t_src_index(vp, &src[0]),
					t_swizzle(GET_SWZ(src[0].Swizzle, 1)), // y
					t_swizzle(GET_SWZ(src[0].Swizzle, 0)), // x
					VSF_IN_COMPONENT_ZERO, // z
					t_swizzle(GET_SWZ(src[0].Swizzle, 3)), // w
					t_src_class(src[0].File),
					src[0].NegateBase ? VSF_FLAG_ALL : VSF_FLAG_NONE) | (src[0].RelAddr << 4);
			goto next;
			
		case OPCODE_DPH://DOT RESULT 1.X Y Z W PARAM 0{} {X Y Z ONE} PARAM 0{} {X Y Z W} 
			o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_DOT, t_dst_index(vp, &vpi->DstReg),
					t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			
			o_inst->src1=MAKE_VSF_SOURCE(t_src_index(vp, &src[0]),
					t_swizzle(GET_SWZ(src[0].Swizzle, 0)),
					t_swizzle(GET_SWZ(src[0].Swizzle, 1)),
					t_swizzle(GET_SWZ(src[0].Swizzle, 2)),
					VSF_IN_COMPONENT_ONE,
					t_src_class(src[0].File),
					src[0].NegateBase ? VSF_FLAG_XYZ : VSF_FLAG_NONE) | (src[0].RelAddr << 4);
			o_inst->src2=t_src(vp, &src[1]);
			o_inst->src3=ZERO_SRC_1;
			goto next;
			
		case OPCODE_XPD:
			/* mul r0, r1.yzxw, r2.zxyw
			   mad r0, -r2.yzxw, r1.zxyw, r0
			   NOTE: might need MAD_2
			 */
			
			o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_MAD, u_temp_i,
					t_dst_mask(vpi->DstReg.WriteMask), VSF_OUT_CLASS_TMP);
			
			o_inst->src1=MAKE_VSF_SOURCE(t_src_index(vp, &src[0]),
					t_swizzle(GET_SWZ(src[0].Swizzle, 1)), // y
					t_swizzle(GET_SWZ(src[0].Swizzle, 2)), // z
					t_swizzle(GET_SWZ(src[0].Swizzle, 0)), // x
					t_swizzle(GET_SWZ(src[0].Swizzle, 3)), // w
					t_src_class(src[0].File),
					src[0].NegateBase ? VSF_FLAG_ALL : VSF_FLAG_NONE) | (src[0].RelAddr << 4);
			
			o_inst->src2=MAKE_VSF_SOURCE(t_src_index(vp, &src[1]),
					t_swizzle(GET_SWZ(src[1].Swizzle, 2)), // z
					t_swizzle(GET_SWZ(src[1].Swizzle, 0)), // x
					t_swizzle(GET_SWZ(src[1].Swizzle, 1)), // y
					t_swizzle(GET_SWZ(src[1].Swizzle, 3)), // w
					t_src_class(src[1].File),
					src[1].NegateBase ? VSF_FLAG_ALL : VSF_FLAG_NONE) | (src[1].RelAddr << 4);
			
			o_inst->src3=ZERO_SRC_1;
			o_inst++;
			u_temp_i--;
			
			o_inst->op=MAKE_VSF_OP(R300_VPI_OUT_OP_MAD, t_dst_index(vp, &vpi->DstReg),
					t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
			
			o_inst->src1=MAKE_VSF_SOURCE(t_src_index(vp, &src[1]),
					t_swizzle(GET_SWZ(src[1].Swizzle, 1)), // y
					t_swizzle(GET_SWZ(src[1].Swizzle, 2)), // z
					t_swizzle(GET_SWZ(src[1].Swizzle, 0)), // x
					t_swizzle(GET_SWZ(src[1].Swizzle, 3)), // w
					t_src_class(src[1].File),
					(!src[1].NegateBase) ? VSF_FLAG_ALL : VSF_FLAG_NONE) | (src[1].RelAddr << 4);
			
			o_inst->src2=MAKE_VSF_SOURCE(t_src_index(vp, &src[0]),
					t_swizzle(GET_SWZ(src[0].Swizzle, 2)), // z
					t_swizzle(GET_SWZ(src[0].Swizzle, 0)), // x
					t_swizzle(GET_SWZ(src[0].Swizzle, 1)), // y
					t_swizzle(GET_SWZ(src[0].Swizzle, 3)), // w
					t_src_class(src[0].File),
					src[0].NegateBase ? VSF_FLAG_ALL : VSF_FLAG_NONE) | (src[0].RelAddr << 4);
			
			o_inst->src3=MAKE_VSF_SOURCE(u_temp_i+1,
					VSF_IN_COMPONENT_X,
					VSF_IN_COMPONENT_Y,
					VSF_IN_COMPONENT_Z,
					VSF_IN_COMPONENT_W,
					VSF_IN_CLASS_TMP,
					VSF_FLAG_NONE);
		
			goto next;

		case OPCODE_RCC:
			fprintf(stderr, "Dont know how to handle op %d yet\n", vpi->Opcode);
			exit(-1);
		break;
		case OPCODE_END:
			break;
		default:
			break;
		}
	
		o_inst->op=MAKE_VSF_OP(t_opcode(vpi->Opcode), t_dst_index(vp, &vpi->DstReg),
				t_dst_mask(vpi->DstReg.WriteMask), t_dst_class(vpi->DstReg.File));
		
		if(are_srcs_scalar){
			switch(operands){
				case 1:
					o_inst->src1=t_src_scalar(vp, &src[0]);
					o_inst->src2=ZERO_SRC_0;
					o_inst->src3=ZERO_SRC_0;
				break;
				
				case 2:
					o_inst->src1=t_src_scalar(vp, &src[0]);
					o_inst->src2=t_src_scalar(vp, &src[1]);
					o_inst->src3=ZERO_SRC_1;
				break;
				
				case 3:
					o_inst->src1=t_src_scalar(vp, &src[0]);
					o_inst->src2=t_src_scalar(vp, &src[1]);
					o_inst->src3=t_src_scalar(vp, &src[2]);
				break;
				
				default:
					fprintf(stderr, "scalars and op RCC not handled yet");
					exit(-1);
				break;
			}
		}else{
			switch(operands){
				case 1:
					o_inst->src1=t_src(vp, &src[0]);
					o_inst->src2=ZERO_SRC_0;
					o_inst->src3=ZERO_SRC_0;
				break;
			
				case 2:
					o_inst->src1=t_src(vp, &src[0]);
					o_inst->src2=t_src(vp, &src[1]);
					o_inst->src3=ZERO_SRC_1;
				break;
			
				case 3:
					o_inst->src1=t_src(vp, &src[0]);
					o_inst->src2=t_src(vp, &src[1]);
					o_inst->src3=t_src(vp, &src[2]);
				break;
			
				default:
					fprintf(stderr, "scalars and op RCC not handled yet");
					exit(-1);
				break;
			}
		}
		next: ;
	}
	
	/* Will most likely segfault before we get here... fix later. */
	if(o_inst - vp->program.body.i >= VSF_MAX_FRAGMENT_LENGTH/4) {
		vp->program.length = 0;
		vp->native = GL_FALSE;
		return ;
	}
	vp->program.length=(o_inst - vp->program.body.i) * 4;
#if 0
	fprintf(stderr, "hw program:\n");
	for(i=0; i < vp->program.length; i++)
		fprintf(stderr, "%08x\n", vp->program.body.d[i]);
#endif
}

