/*
 * Copyright © 2008 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <stdarg.h>

#include "brw_eu.h"

static const struct {
	const char *name;
	int nsrc;
	int ndst;
} opcode[128] = {
	[BRW_OPCODE_MOV] = { .name = "mov", .nsrc = 1, .ndst = 1 },
	[BRW_OPCODE_FRC] = { .name = "frc", .nsrc = 1, .ndst = 1 },
	[BRW_OPCODE_RNDU] = { .name = "rndu", .nsrc = 1, .ndst = 1 },
	[BRW_OPCODE_RNDD] = { .name = "rndd", .nsrc = 1, .ndst = 1 },
	[BRW_OPCODE_RNDE] = { .name = "rnde", .nsrc = 1, .ndst = 1 },
	[BRW_OPCODE_RNDZ] = { .name = "rndz", .nsrc = 1, .ndst = 1 },
	[BRW_OPCODE_NOT] = { .name = "not", .nsrc = 1, .ndst = 1 },
	[BRW_OPCODE_LZD] = { .name = "lzd", .nsrc = 1, .ndst = 1 },

	[BRW_OPCODE_MUL] = { .name = "mul", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_MAC] = { .name = "mac", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_MACH] = { .name = "mach", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_LINE] = { .name = "line", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_PLN] = { .name = "pln", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_SAD2] = { .name = "sad2", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_SADA2] = { .name = "sada2", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_DP4] = { .name = "dp4", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_DPH] = { .name = "dph", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_DP3] = { .name = "dp3", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_DP2] = { .name = "dp2", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_MATH] = { .name = "math", .nsrc = 2, .ndst = 1 },

	[BRW_OPCODE_AVG] = { .name = "avg", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_ADD] = { .name = "add", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_SEL] = { .name = "sel", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_AND] = { .name = "and", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_OR] = { .name = "or", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_XOR] = { .name = "xor", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_SHR] = { .name = "shr", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_SHL] = { .name = "shl", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_ASR] = { .name = "asr", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_CMP] = { .name = "cmp", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_CMPN] = { .name = "cmpn", .nsrc = 2, .ndst = 1 },

	[BRW_OPCODE_SEND] = { .name = "send", .nsrc = 1, .ndst = 1 },
	[BRW_OPCODE_SENDC] = { .name = "sendc", .nsrc = 1, .ndst = 1 },
	[BRW_OPCODE_NOP] = { .name = "nop", .nsrc = 0, .ndst = 0 },
	[BRW_OPCODE_JMPI] = { .name = "jmpi", .nsrc = 1, .ndst = 0 },
	[BRW_OPCODE_IF] = { .name = "if", .nsrc = 2, .ndst = 0 },
	[BRW_OPCODE_IFF] = { .name = "iff", .nsrc = 2, .ndst = 1 },
	[BRW_OPCODE_WHILE] = { .name = "while", .nsrc = 2, .ndst = 0 },
	[BRW_OPCODE_ELSE] = { .name = "else", .nsrc = 2, .ndst = 0 },
	[BRW_OPCODE_BREAK] = { .name = "break", .nsrc = 2, .ndst = 0 },
	[BRW_OPCODE_CONTINUE] = { .name = "cont", .nsrc = 1, .ndst = 0 },
	[BRW_OPCODE_HALT] = { .name = "halt", .nsrc = 1, .ndst = 0 },
	[BRW_OPCODE_MSAVE] = { .name = "msave", .nsrc = 1, .ndst = 1 },
	[BRW_OPCODE_PUSH] = { .name = "push", .nsrc = 1, .ndst = 1 },
	[BRW_OPCODE_MRESTORE] = { .name = "mrest", .nsrc = 1, .ndst = 1 },
	[BRW_OPCODE_POP] = { .name = "pop", .nsrc = 2, .ndst = 0 },
	[BRW_OPCODE_WAIT] = { .name = "wait", .nsrc = 1, .ndst = 0 },
	[BRW_OPCODE_DO] = { .name = "do", .nsrc = 0, .ndst = 0 },
	[BRW_OPCODE_ENDIF] = { .name = "endif", .nsrc = 2, .ndst = 0 },
};

static const char *conditional_modifier[16] = {
	[BRW_CONDITIONAL_NONE] = "",
	[BRW_CONDITIONAL_Z] = ".e",
	[BRW_CONDITIONAL_NZ] = ".ne",
	[BRW_CONDITIONAL_G] = ".g",
	[BRW_CONDITIONAL_GE] = ".ge",
	[BRW_CONDITIONAL_L] = ".l",
	[BRW_CONDITIONAL_LE] = ".le",
	[BRW_CONDITIONAL_R] = ".r",
	[BRW_CONDITIONAL_O] = ".o",
	[BRW_CONDITIONAL_U] = ".u",
};

static const char *negate[2] = {
	[0] = "",
	[1] = "-",
};

static const char *_abs[2] = {
	[0] = "",
	[1] = "(abs)",
};

static const char *vert_stride[16] = {
	[0] = "0",
	[1] = "1",
	[2] = "2",
	[3] = "4",
	[4] = "8",
	[5] = "16",
	[6] = "32",
	[15] = "VxH",
};

static const char *width[8] = {
	[0] = "1",
	[1] = "2",
	[2] = "4",
	[3] = "8",
	[4] = "16",
};

static const char *horiz_stride[4] = {
	[0] = "0",
	[1] = "1",
	[2] = "2",
	[3] = "4"
};

static const char *chan_sel[4] = {
	[0] = "x",
	[1] = "y",
	[2] = "z",
	[3] = "w",
};

#if 0
static const char *dest_condmod[16] = {
};

static const char *imm_encoding[8] = {
	[0] = "UD",
	[1] = "D",
	[2] = "UW",
	[3] = "W",
	[5] = "VF",
	[6] = "V",
	[7] = "F"
};
#endif

static const char *debug_ctrl[2] = {
	[0] = "",
	[1] = ".breakpoint"
};

static const char *saturate[2] = {
	[0] = "",
	[1] = ".sat"
};

static const char *accwr[2] = {
	[0] = "",
	[1] = "AccWrEnable"
};

static const char *wectrl[2] = {
	[0] = "WE_normal",
	[1] = "WE_all"
};

static const char *exec_size[8] = {
	[0] = "1",
	[1] = "2",
	[2] = "4",
	[3] = "8",
	[4] = "16",
	[5] = "32"
};

static const char *pred_inv[2] = {
	[0] = "+",
	[1] = "-"
};

static const char *pred_ctrl_align16[16] = {
	[1] = "",
	[2] = ".x",
	[3] = ".y",
	[4] = ".z",
	[5] = ".w",
	[6] = ".any4h",
	[7] = ".all4h",
};

static const char *pred_ctrl_align1[16] = {
	[1] = "",
	[2] = ".anyv",
	[3] = ".allv",
	[4] = ".any2h",
	[5] = ".all2h",
	[6] = ".any4h",
	[7] = ".all4h",
	[8] = ".any8h",
	[9] = ".all8h",
	[10] = ".any16h",
	[11] = ".all16h",
};

static const char *thread_ctrl[4] = {
	[0] = "",
	[2] = "switch"
};

static const char *compr_ctrl[4] = {
	[0] = "",
	[1] = "sechalf",
	[2] = "compr",
	[3] = "compr4",
};

static const char *dep_ctrl[4] = {
	[0] = "",
	[1] = "NoDDClr",
	[2] = "NoDDChk",
	[3] = "NoDDClr,NoDDChk",
};

static const char *mask_ctrl[4] = {
	[0] = "",
	[1] = "nomask",
};

static const char *access_mode[2] = {
	[0] = "align1",
	[1] = "align16",
};

static const char *reg_encoding[8] = {
	[0] = "UD",
	[1] = "D",
	[2] = "UW",
	[3] = "W",
	[4] = "UB",
	[5] = "B",
	[7] = "F"
};

static const int reg_type_size[8] = {
	[0] = 4,
	[1] = 4,
	[2] = 2,
	[3] = 2,
	[4] = 1,
	[5] = 1,
	[7] = 4
};

static const char *reg_file[4] = {
	[0] = "A",
	[1] = "g",
	[2] = "m",
	[3] = "imm",
};

static const char *writemask[16] = {
	[0x0] = ".",
	[0x1] = ".x",
	[0x2] = ".y",
	[0x3] = ".xy",
	[0x4] = ".z",
	[0x5] = ".xz",
	[0x6] = ".yz",
	[0x7] = ".xyz",
	[0x8] = ".w",
	[0x9] = ".xw",
	[0xa] = ".yw",
	[0xb] = ".xyw",
	[0xc] = ".zw",
	[0xd] = ".xzw",
	[0xe] = ".yzw",
	[0xf] = "",
};

static const char *end_of_thread[2] = {
	[0] = "",
	[1] = "EOT"
};

static const char *target_function[16] = {
	[BRW_SFID_NULL] = "null",
	[BRW_SFID_MATH] = "math",
	[BRW_SFID_SAMPLER] = "sampler",
	[BRW_SFID_MESSAGE_GATEWAY] = "gateway",
	[BRW_SFID_DATAPORT_READ] = "read",
	[BRW_SFID_DATAPORT_WRITE] = "write",
	[BRW_SFID_URB] = "urb",
	[BRW_SFID_THREAD_SPAWNER] = "thread_spawner"
};

static const char *target_function_gen6[16] = {
	[BRW_SFID_NULL] = "null",
	[BRW_SFID_MATH] = "math",
	[BRW_SFID_SAMPLER] = "sampler",
	[BRW_SFID_MESSAGE_GATEWAY] = "gateway",
	[BRW_SFID_URB] = "urb",
	[BRW_SFID_THREAD_SPAWNER] = "thread_spawner",
	[GEN6_SFID_DATAPORT_SAMPLER_CACHE] = "sampler",
	[GEN6_SFID_DATAPORT_RENDER_CACHE] = "render",
	[GEN6_SFID_DATAPORT_CONSTANT_CACHE] = "const",
	[GEN7_SFID_DATAPORT_DATA_CACHE] = "data"
};

static const char *dp_rc_msg_type_gen6[16] = {
	[BRW_DATAPORT_READ_MESSAGE_OWORD_BLOCK_READ] = "OWORD block read",
	[GEN6_DATAPORT_READ_MESSAGE_RENDER_UNORM_READ] = "RT UNORM read",
	[GEN6_DATAPORT_READ_MESSAGE_OWORD_DUAL_BLOCK_READ] = "OWORD dual block read",
	[GEN6_DATAPORT_READ_MESSAGE_MEDIA_BLOCK_READ] = "media block read",
	[GEN6_DATAPORT_READ_MESSAGE_OWORD_UNALIGN_BLOCK_READ] = "OWORD unaligned block read",
	[GEN6_DATAPORT_READ_MESSAGE_DWORD_SCATTERED_READ] = "DWORD scattered read",
	[GEN6_DATAPORT_WRITE_MESSAGE_DWORD_ATOMIC_WRITE] = "DWORD atomic write",
	[GEN6_DATAPORT_WRITE_MESSAGE_OWORD_BLOCK_WRITE] = "OWORD block write",
	[GEN6_DATAPORT_WRITE_MESSAGE_OWORD_DUAL_BLOCK_WRITE] = "OWORD dual block write",
	[GEN6_DATAPORT_WRITE_MESSAGE_MEDIA_BLOCK_WRITE] = "media block write",
	[GEN6_DATAPORT_WRITE_MESSAGE_DWORD_SCATTERED_WRITE] = "DWORD scattered write",
	[GEN6_DATAPORT_WRITE_MESSAGE_RENDER_TARGET_WRITE] = "RT write",
	[GEN6_DATAPORT_WRITE_MESSAGE_STREAMED_VB_WRITE] = "streamed VB write",
	[GEN6_DATAPORT_WRITE_MESSAGE_RENDER_TARGET_UNORM_WRITE] = "RT UNORMc write",
};

static const char *math_function[16] = {
	[BRW_MATH_FUNCTION_INV] = "inv",
	[BRW_MATH_FUNCTION_LOG] = "log",
	[BRW_MATH_FUNCTION_EXP] = "exp",
	[BRW_MATH_FUNCTION_SQRT] = "sqrt",
	[BRW_MATH_FUNCTION_RSQ] = "rsq",
	[BRW_MATH_FUNCTION_SIN] = "sin",
	[BRW_MATH_FUNCTION_COS] = "cos",
	[BRW_MATH_FUNCTION_SINCOS] = "sincos",
	[BRW_MATH_FUNCTION_TAN] = "tan",
	[BRW_MATH_FUNCTION_POW] = "pow",
	[BRW_MATH_FUNCTION_INT_DIV_QUOTIENT_AND_REMAINDER] = "intdivmod",
	[BRW_MATH_FUNCTION_INT_DIV_QUOTIENT] = "intdiv",
	[BRW_MATH_FUNCTION_INT_DIV_REMAINDER] = "intmod",
};

static const char *math_saturate[2] = {
	[0] = "",
	[1] = "sat"
};

static const char *math_signed[2] = {
	[0] = "",
	[1] = "signed"
};

static const char *math_scalar[2] = {
	[0] = "",
	[1] = "scalar"
};

static const char *math_precision[2] = {
	[0] = "",
	[1] = "partial_precision"
};

static const char *urb_opcode[2] = {
	[0] = "urb_write",
	[1] = "ff_sync",
};

static const char *urb_swizzle[4] = {
	[BRW_URB_SWIZZLE_NONE] = "",
	[BRW_URB_SWIZZLE_INTERLEAVE] = "interleave",
	[BRW_URB_SWIZZLE_TRANSPOSE] = "transpose",
};

static const char *urb_allocate[2] = {
	[0] = "",
	[1] = "allocate"
};

static const char *urb_used[2] = {
	[0] = "",
	[1] = "used"
};

static const char *urb_complete[2] = {
	[0] = "",
	[1] = "complete"
};

static const char *sampler_target_format[4] = {
	[0] = "F",
	[2] = "UD",
	[3] = "D"
};

static int column;

static int string(FILE *file, const char *str)
{
	fputs(str, file);
	column += strlen(str);
	return 0;
}

#if defined(__GNUC__) && (__GNUC__ > 2)
__attribute__((format(printf, 2, 3)))
#endif
static int format(FILE *f, const char *fmt, ...)
{
	char buf[1024];
	va_list	args;

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf) - 1, fmt, args);
	va_end(args);

	string(f, buf);
	return 0;
}

static void newline(FILE *f)
{
	putc('\n', f);
	column = 0;
}

static void pad(FILE *f, int c)
{
	do
		string(f, " ");
	while (column < c);
}

static void control(FILE *file, const char *name, const char *ctrl[], unsigned id, int *space)
{
	if (!ctrl[id]) {
		fprintf(file, "*** invalid %s value %d ",
			name, id);
		assert(0);
	}
	if (ctrl[id][0]) {
		if (space && *space)
			string(file, " ");
		string(file, ctrl[id]);
		if (space)
			*space = 1;
	}
}

static void print_opcode(FILE *file, int id)
{
	if (!opcode[id].name) {
		format(file, "*** invalid opcode value %d ", id);
		assert(0);
	}
	string(file, opcode[id].name);
}

static int reg(FILE *file, unsigned _reg_file, unsigned _reg_nr)
{
	/* Clear the Compr4 instruction compression bit. */
	if (_reg_file == BRW_MESSAGE_REGISTER_FILE)
		_reg_nr &= ~(1 << 7);

	if (_reg_file == BRW_ARCHITECTURE_REGISTER_FILE) {
		switch (_reg_nr & 0xf0) {
		case BRW_ARF_NULL:
			string(file, "null");
			return -1;
		case BRW_ARF_ADDRESS:
			format(file, "a%d", _reg_nr & 0x0f);
			break;
		case BRW_ARF_ACCUMULATOR:
			format(file, "acc%d", _reg_nr & 0x0f);
			break;
		case BRW_ARF_FLAG:
			format(file, "f%d", _reg_nr & 0x0f);
			break;
		case BRW_ARF_MASK:
			format(file, "mask%d", _reg_nr & 0x0f);
			break;
		case BRW_ARF_MASK_STACK:
			format(file, "msd%d", _reg_nr & 0x0f);
			break;
		case BRW_ARF_STATE:
			format(file, "sr%d", _reg_nr & 0x0f);
			break;
		case BRW_ARF_CONTROL:
			format(file, "cr%d", _reg_nr & 0x0f);
			break;
		case BRW_ARF_NOTIFICATION_COUNT:
			format(file, "n%d", _reg_nr & 0x0f);
			break;
		case BRW_ARF_IP:
			string(file, "ip");
			return -1;
		default:
			format(file, "ARF%d", _reg_nr);
			break;
		}
	} else {
		control(file, "src reg file", reg_file, _reg_file, NULL);
		format(file, "%d", _reg_nr);
	}
	return 0;
}

static void dest(FILE *file, const struct brw_instruction *inst)
{
	if (inst->header.access_mode == BRW_ALIGN_1) {
		if (inst->bits1.da1.dest_address_mode == BRW_ADDRESS_DIRECT) {
			if (reg(file, inst->bits1.da1.dest_reg_file, inst->bits1.da1.dest_reg_nr))
				return;

			if (inst->bits1.da1.dest_subreg_nr)
				format(file, ".%d", inst->bits1.da1.dest_subreg_nr /
				       reg_type_size[inst->bits1.da1.dest_reg_type]);
			format(file, "<%d>", inst->bits1.da1.dest_horiz_stride);
			control(file, "dest reg encoding", reg_encoding, inst->bits1.da1.dest_reg_type, NULL);
		} else {
			string(file, "g[a0");
			if (inst->bits1.ia1.dest_subreg_nr)
				format(file, ".%d", inst->bits1.ia1.dest_subreg_nr /
				       reg_type_size[inst->bits1.ia1.dest_reg_type]);
			if (inst->bits1.ia1.dest_indirect_offset)
				format(file, " %d", inst->bits1.ia1.dest_indirect_offset);
			string(file, "]");
			format(file, "<%d>", inst->bits1.ia1.dest_horiz_stride);
			control(file, "dest reg encoding", reg_encoding, inst->bits1.ia1.dest_reg_type, NULL);
		}
	} else {
		if (inst->bits1.da16.dest_address_mode == BRW_ADDRESS_DIRECT) {
			if (reg(file, inst->bits1.da16.dest_reg_file, inst->bits1.da16.dest_reg_nr))
				return;

			if (inst->bits1.da16.dest_subreg_nr)
				format(file, ".%d", inst->bits1.da16.dest_subreg_nr /
				       reg_type_size[inst->bits1.da16.dest_reg_type]);
			string(file, "<1>");
			control(file, "writemask", writemask, inst->bits1.da16.dest_writemask, NULL);
			control(file, "dest reg encoding", reg_encoding, inst->bits1.da16.dest_reg_type, NULL);
		} else {
			string(file, "Indirect align16 address mode not supported");
		}
	}
}

static void src_align1_region(FILE *file,
			      unsigned _vert_stride, unsigned _width, unsigned _horiz_stride)
{
	string(file, "<");
	control(file, "vert stride", vert_stride, _vert_stride, NULL);
	string(file, ",");
	control(file, "width", width, _width, NULL);
	string(file, ",");
	control(file, "horiz_stride", horiz_stride, _horiz_stride, NULL);
	string(file, ">");
}

static void src_da1(FILE *file, unsigned type, unsigned _reg_file,
		    unsigned _vert_stride, unsigned _width, unsigned _horiz_stride,
		    unsigned reg_num, unsigned sub_reg_num, unsigned __abs, unsigned _negate)
{
	control(file, "negate", negate, _negate, NULL);
	control(file, "abs", _abs, __abs, NULL);

	if (reg(file, _reg_file, reg_num))
		return;

	if (sub_reg_num)
		format(file, ".%d", sub_reg_num / reg_type_size[type]); /* use formal style like spec */
	src_align1_region(file, _vert_stride, _width, _horiz_stride);
	control(file, "src reg encoding", reg_encoding, type, NULL);
}

static void src_ia1(FILE *file,
		    unsigned type,
		    unsigned _reg_file,
		    int _addr_imm,
		    unsigned _addr_subreg_nr,
		    unsigned _negate,
		    unsigned __abs,
		    unsigned _addr_mode,
		    unsigned _horiz_stride,
		    unsigned _width,
		    unsigned _vert_stride)
{
	control(file, "negate", negate, _negate, NULL);
	control(file, "abs", _abs, __abs, NULL);

	string(file, "g[a0");
	if (_addr_subreg_nr)
		format(file, ".%d", _addr_subreg_nr);
	if (_addr_imm)
		format(file, " %d", _addr_imm);
	string(file, "]");
	src_align1_region(file, _vert_stride, _width, _horiz_stride);
	control(file, "src reg encoding", reg_encoding, type, NULL);
}

static void src_da16(FILE *file,
		     unsigned _reg_type,
		     unsigned _reg_file,
		     unsigned _vert_stride,
		     unsigned _reg_nr,
		     unsigned _subreg_nr,
		     unsigned __abs,
		     unsigned _negate,
		     unsigned swz_x,
		     unsigned swz_y,
		     unsigned swz_z,
		     unsigned swz_w)
{
	control(file, "negate", negate, _negate, NULL);
	control(file, "abs", _abs, __abs, NULL);

	if (reg(file, _reg_file, _reg_nr))
		return;

	if (_subreg_nr)
		/* bit4 for subreg number byte addressing. Make this same meaning as
		   in da1 case, so output looks consistent. */
		format(file, ".%d", 16 / reg_type_size[_reg_type]);
	string(file, "<");
	control(file, "vert stride", vert_stride, _vert_stride, NULL);
	string(file, ",4,1>");
	/*
	 * Three kinds of swizzle display:
	 *  identity - nothing printed
	 *  1->all	 - print the single channel
	 *  1->1     - print the mapping
	 */
	if (swz_x == BRW_CHANNEL_X &&
	    swz_y == BRW_CHANNEL_Y &&
	    swz_z == BRW_CHANNEL_Z &&
	    swz_w == BRW_CHANNEL_W)
	{
		;
	}
	else if (swz_x == swz_y && swz_x == swz_z && swz_x == swz_w)
	{
		string(file, ".");
		control(file, "channel select", chan_sel, swz_x, NULL);
	}
	else
	{
		string(file, ".");
		control(file, "channel select", chan_sel, swz_x, NULL);
		control(file, "channel select", chan_sel, swz_y, NULL);
		control(file, "channel select", chan_sel, swz_z, NULL);
		control(file, "channel select", chan_sel, swz_w, NULL);
	}
	control(file, "src da16 reg type", reg_encoding, _reg_type, NULL);
}

static void imm(FILE *file, unsigned type, const struct brw_instruction *inst)
{
	switch (type) {
	case BRW_REGISTER_TYPE_UD:
		format(file, "0x%08xUD", inst->bits3.ud);
		break;
	case BRW_REGISTER_TYPE_D:
		format(file, "%dD", inst->bits3.d);
		break;
	case BRW_REGISTER_TYPE_UW:
		format(file, "0x%04xUW", (uint16_t) inst->bits3.ud);
		break;
	case BRW_REGISTER_TYPE_W:
		format(file, "%dW", (int16_t) inst->bits3.d);
		break;
	case BRW_REGISTER_TYPE_UB:
		format(file, "0x%02xUB", (int8_t) inst->bits3.ud);
		break;
	case BRW_REGISTER_TYPE_VF:
		format(file, "Vector Float");
		break;
	case BRW_REGISTER_TYPE_V:
		format(file, "0x%08xV", inst->bits3.ud);
		break;
	case BRW_REGISTER_TYPE_F:
		format(file, "%-gF", inst->bits3.f);
	}
}

static void src0(FILE *file, const struct brw_instruction *inst)
{
	if (inst->bits1.da1.src0_reg_file == BRW_IMMEDIATE_VALUE)
		imm(file, inst->bits1.da1.src0_reg_type, inst);
	else if (inst->header.access_mode == BRW_ALIGN_1) {
		if (inst->bits2.da1.src0_address_mode == BRW_ADDRESS_DIRECT) {
			src_da1(file,
				inst->bits1.da1.src0_reg_type,
				inst->bits1.da1.src0_reg_file,
				inst->bits2.da1.src0_vert_stride,
				inst->bits2.da1.src0_width,
				inst->bits2.da1.src0_horiz_stride,
				inst->bits2.da1.src0_reg_nr,
				inst->bits2.da1.src0_subreg_nr,
				inst->bits2.da1.src0_abs,
				inst->bits2.da1.src0_negate);
		} else {
			src_ia1(file,
				inst->bits1.ia1.src0_reg_type,
				inst->bits1.ia1.src0_reg_file,
				inst->bits2.ia1.src0_indirect_offset,
				inst->bits2.ia1.src0_subreg_nr,
				inst->bits2.ia1.src0_negate,
				inst->bits2.ia1.src0_abs,
				inst->bits2.ia1.src0_address_mode,
				inst->bits2.ia1.src0_horiz_stride,
				inst->bits2.ia1.src0_width,
				inst->bits2.ia1.src0_vert_stride);
		}
	} else {
		if (inst->bits2.da16.src0_address_mode == BRW_ADDRESS_DIRECT) {
			src_da16(file,
				 inst->bits1.da16.src0_reg_type,
				 inst->bits1.da16.src0_reg_file,
				 inst->bits2.da16.src0_vert_stride,
				 inst->bits2.da16.src0_reg_nr,
				 inst->bits2.da16.src0_subreg_nr,
				 inst->bits2.da16.src0_abs,
				 inst->bits2.da16.src0_negate,
				 inst->bits2.da16.src0_swz_x,
				 inst->bits2.da16.src0_swz_y,
				 inst->bits2.da16.src0_swz_z,
				 inst->bits2.da16.src0_swz_w);
		} else {
			string(file, "Indirect align16 address mode not supported");
		}
	}
}

static void src1(FILE *file, const struct brw_instruction *inst)
{
	if (inst->bits1.da1.src1_reg_file == BRW_IMMEDIATE_VALUE)
		imm(file, inst->bits1.da1.src1_reg_type, inst);
	else if (inst->header.access_mode == BRW_ALIGN_1) {
		if (inst->bits3.da1.src1_address_mode == BRW_ADDRESS_DIRECT) {
			src_da1(file,
				inst->bits1.da1.src1_reg_type,
				inst->bits1.da1.src1_reg_file,
				inst->bits3.da1.src1_vert_stride,
				inst->bits3.da1.src1_width,
				inst->bits3.da1.src1_horiz_stride,
				inst->bits3.da1.src1_reg_nr,
				inst->bits3.da1.src1_subreg_nr,
				inst->bits3.da1.src1_abs,
				inst->bits3.da1.src1_negate);
		} else {
			src_ia1(file,
				inst->bits1.ia1.src1_reg_type,
				inst->bits1.ia1.src1_reg_file,
				inst->bits3.ia1.src1_indirect_offset,
				inst->bits3.ia1.src1_subreg_nr,
				inst->bits3.ia1.src1_negate,
				inst->bits3.ia1.src1_abs,
				inst->bits3.ia1.src1_address_mode,
				inst->bits3.ia1.src1_horiz_stride,
				inst->bits3.ia1.src1_width,
				inst->bits3.ia1.src1_vert_stride);
		}
	} else {
		if (inst->bits3.da16.src1_address_mode == BRW_ADDRESS_DIRECT) {
			src_da16(file,
				 inst->bits1.da16.src1_reg_type,
				 inst->bits1.da16.src1_reg_file,
				 inst->bits3.da16.src1_vert_stride,
				 inst->bits3.da16.src1_reg_nr,
				 inst->bits3.da16.src1_subreg_nr,
				 inst->bits3.da16.src1_abs,
				 inst->bits3.da16.src1_negate,
				 inst->bits3.da16.src1_swz_x,
				 inst->bits3.da16.src1_swz_y,
				 inst->bits3.da16.src1_swz_z,
				 inst->bits3.da16.src1_swz_w);
		} else {
			string(file, "Indirect align16 address mode not supported");
		}
	}
}

static const int esize[6] = {
	[0] = 1,
	[1] = 2,
	[2] = 4,
	[3] = 8,
	[4] = 16,
	[5] = 32,
};

static int qtr_ctrl(FILE *file, const struct brw_instruction *inst)
{
	int qtr_ctl = inst->header.compression_control;
	int size = esize[inst->header.execution_size];

	if (size == 8) {
		switch (qtr_ctl) {
		case 0:
			string(file, " 1Q");
			break;
		case 1:
			string(file, " 2Q");
			break;
		case 2:
			string(file, " 3Q");
			break;
		case 3:
			string(file, " 4Q");
			break;
		}
	} else if (size == 16){
		if (qtr_ctl < 2)
			string(file, " 1H");
		else
			string(file, " 2H");
	}
	return 0;
}

void brw_disasm(FILE *file, const struct brw_instruction *inst, int gen)
{
	int space = 0;

	format(file, "%08x %08x %08x %08x\n",
	       ((const uint32_t*)inst)[0],
	       ((const uint32_t*)inst)[1],
	       ((const uint32_t*)inst)[2],
	       ((const uint32_t*)inst)[3]);

	if (inst->header.predicate_control) {
		string(file, "(");
		control(file, "predicate inverse", pred_inv, inst->header.predicate_inverse, NULL);
		string(file, "f0");
		if (inst->bits2.da1.flag_subreg_nr)
			format(file, ".%d", inst->bits2.da1.flag_subreg_nr);
		if (inst->header.access_mode == BRW_ALIGN_1)
			control(file, "predicate control align1", pred_ctrl_align1,
				inst->header.predicate_control, NULL);
		else
			control(file, "predicate control align16", pred_ctrl_align16,
				inst->header.predicate_control, NULL);
		string(file, ") ");
	}

	print_opcode(file, inst->header.opcode);
	control(file, "saturate", saturate, inst->header.saturate, NULL);
	control(file, "debug control", debug_ctrl, inst->header.debug_control, NULL);

	if (inst->header.opcode == BRW_OPCODE_MATH) {
		string(file, " ");
		control(file, "function", math_function,
			inst->header.destreg__conditionalmod, NULL);
	} else if (inst->header.opcode != BRW_OPCODE_SEND &&
		   inst->header.opcode != BRW_OPCODE_SENDC)
		control(file, "conditional modifier", conditional_modifier,
			inst->header.destreg__conditionalmod, NULL);

	if (inst->header.opcode != BRW_OPCODE_NOP) {
		string(file, "(");
		control(file, "execution size", exec_size, inst->header.execution_size, NULL);
		string(file, ")");
	}

	if (inst->header.opcode == BRW_OPCODE_SEND && gen < 060)
		format(file, " %d", inst->header.destreg__conditionalmod);

	if (opcode[inst->header.opcode].ndst > 0) {
		pad(file, 16);
		dest(file, inst);
	} else if (gen >= 060 &&
		   (inst->header.opcode == BRW_OPCODE_IF ||
		    inst->header.opcode == BRW_OPCODE_ELSE ||
		    inst->header.opcode == BRW_OPCODE_ENDIF ||
		    inst->header.opcode == BRW_OPCODE_WHILE)) {
		format(file, " %d", inst->bits1.branch_gen6.jump_count);
	}

	if (opcode[inst->header.opcode].nsrc > 0) {
		pad(file, 32);
		src0(file, inst);
	}
	if (opcode[inst->header.opcode].nsrc > 1) {
		pad(file, 48);
		src1(file, inst);
	}

	if (inst->header.opcode == BRW_OPCODE_SEND ||
	    inst->header.opcode == BRW_OPCODE_SENDC) {
		enum brw_message_target target;

		if (gen >= 060)
			target = inst->header.destreg__conditionalmod;
		else if (gen >= 050)
			target = inst->bits2.send_gen5.sfid;
		else
			target = inst->bits3.generic.msg_target;

		newline (file);
		pad (file, 16);
		space = 0;

		if (gen >= 060) {
			control (file, "target function", target_function_gen6,
				 target, &space);
		} else {
			control (file, "target function", target_function,
				 target, &space);
		}

		switch (target) {
		case BRW_SFID_MATH:
			control (file, "math function", math_function,
				 inst->bits3.math.function, &space);
			control (file, "math saturate", math_saturate,
				 inst->bits3.math.saturate, &space);
			control (file, "math signed", math_signed,
				 inst->bits3.math.int_type, &space);
			control (file, "math scalar", math_scalar,
				 inst->bits3.math.data_type, &space);
			control (file, "math precision", math_precision,
				 inst->bits3.math.precision, &space);
			break;
		case BRW_SFID_SAMPLER:
			if (gen >= 070) {
				format (file, " (%d, %d, %d, %d)",
					inst->bits3.sampler_gen7.binding_table_index,
					inst->bits3.sampler_gen7.sampler,
					inst->bits3.sampler_gen7.msg_type,
					inst->bits3.sampler_gen7.simd_mode);
			} else if (gen >= 050) {
				format (file, " (%d, %d, %d, %d)",
					inst->bits3.sampler_gen5.binding_table_index,
					inst->bits3.sampler_gen5.sampler,
					inst->bits3.sampler_gen5.msg_type,
					inst->bits3.sampler_gen5.simd_mode);
			} else if (gen >= 045) {
				format (file, " (%d, %d)",
					inst->bits3.sampler_g4x.binding_table_index,
					inst->bits3.sampler_g4x.sampler);
			} else {
				format (file, " (%d, %d, ",
					inst->bits3.sampler.binding_table_index,
					inst->bits3.sampler.sampler);
				control (file, "sampler target format",
					 sampler_target_format,
					 inst->bits3.sampler.return_format, NULL);
				string (file, ")");
			}
			break;
		case BRW_SFID_DATAPORT_READ:
			if (gen >= 060) {
				format (file, " (%d, %d, %d, %d)",
					inst->bits3.gen6_dp.binding_table_index,
					inst->bits3.gen6_dp.msg_control,
					inst->bits3.gen6_dp.msg_type,
					inst->bits3.gen6_dp.send_commit_msg);
			} else if (gen >= 045) {
				format (file, " (%d, %d, %d)",
					inst->bits3.dp_read_gen5.binding_table_index,
					inst->bits3.dp_read_gen5.msg_control,
					inst->bits3.dp_read_gen5.msg_type);
			} else {
				format (file, " (%d, %d, %d)",
					inst->bits3.dp_read.binding_table_index,
					inst->bits3.dp_read.msg_control,
					inst->bits3.dp_read.msg_type);
			}
			break;

		case BRW_SFID_DATAPORT_WRITE:
			if (gen >= 070) {
				format (file, " (");

				control (file, "DP rc message type",
					 dp_rc_msg_type_gen6,
					 inst->bits3.gen7_dp.msg_type, &space);

				format (file, ", %d, %d, %d)",
					inst->bits3.gen7_dp.binding_table_index,
					inst->bits3.gen7_dp.msg_control,
					inst->bits3.gen7_dp.msg_type);
			} else if (gen >= 060) {
				format (file, " (");

				control (file, "DP rc message type",
					 dp_rc_msg_type_gen6,
					 inst->bits3.gen6_dp.msg_type, &space);

				format (file, ", %d, %d, %d, %d)",
					inst->bits3.gen6_dp.binding_table_index,
					inst->bits3.gen6_dp.msg_control,
					inst->bits3.gen6_dp.msg_type,
					inst->bits3.gen6_dp.send_commit_msg);
			} else {
				format (file, " (%d, %d, %d, %d)",
					inst->bits3.dp_write.binding_table_index,
					(inst->bits3.dp_write.last_render_target << 3) |
					inst->bits3.dp_write.msg_control,
					inst->bits3.dp_write.msg_type,
					inst->bits3.dp_write.send_commit_msg);
			}
			break;

		case BRW_SFID_URB:
			if (gen >= 050) {
				format (file, " %d", inst->bits3.urb_gen5.offset);
			} else {
				format (file, " %d", inst->bits3.urb.offset);
			}

			space = 1;
			if (gen >= 050) {
				control (file, "urb opcode", urb_opcode,
					 inst->bits3.urb_gen5.opcode, &space);
			}
			control (file, "urb swizzle", urb_swizzle,
				 inst->bits3.urb.swizzle_control, &space);
			control (file, "urb allocate", urb_allocate,
				 inst->bits3.urb.allocate, &space);
			control (file, "urb used", urb_used,
				 inst->bits3.urb.used, &space);
			control (file, "urb complete", urb_complete,
				 inst->bits3.urb.complete, &space);
			break;
		case BRW_SFID_THREAD_SPAWNER:
			break;
		case GEN7_SFID_DATAPORT_DATA_CACHE:
			format (file, " (%d, %d, %d)",
				inst->bits3.gen7_dp.binding_table_index,
				inst->bits3.gen7_dp.msg_control,
				inst->bits3.gen7_dp.msg_type);
			break;


		default:
			format (file, "unsupported target %d", target);
			break;
		}
		if (space)
			string (file, " ");
		if (gen >= 050) {
			format (file, "mlen %d",
				inst->bits3.generic_gen5.msg_length);
			format (file, " rlen %d",
				inst->bits3.generic_gen5.response_length);
		} else {
			format (file, "mlen %d",
				inst->bits3.generic.msg_length);
			format (file, " rlen %d",
				inst->bits3.generic.response_length);
		}
	}
	pad(file, 64);
	if (inst->header.opcode != BRW_OPCODE_NOP) {
		string(file, "{");
		space = 1;
		control(file, "access mode", access_mode, inst->header.access_mode, &space);
		if (gen >= 060)
			control(file, "write enable control", wectrl, inst->header.mask_control, &space);
		else
			control(file, "mask control", mask_ctrl, inst->header.mask_control, &space);
		control(file, "dependency control", dep_ctrl, inst->header.dependency_control, &space);

		if (gen >= 060)
			qtr_ctrl(file, inst);
		else {
			if (inst->header.compression_control == BRW_COMPRESSION_COMPRESSED &&
			    opcode[inst->header.opcode].ndst > 0 &&
			    inst->bits1.da1.dest_reg_file == BRW_MESSAGE_REGISTER_FILE &&
			    inst->bits1.da1.dest_reg_nr & (1 << 7)) {
				format(file, " compr4");
			} else {
				control(file, "compression control", compr_ctrl,
					inst->header.compression_control, &space);
			}
		}

		control(file, "thread control", thread_ctrl, inst->header.thread_control, &space);
		if (gen >= 060)
			control(file, "acc write control", accwr, inst->header.acc_wr_control, &space);
		if (inst->header.opcode == BRW_OPCODE_SEND ||
		    inst->header.opcode == BRW_OPCODE_SENDC)
			control(file, "end of thread", end_of_thread,
				inst->bits3.generic.end_of_thread, &space);
		if (space)
			string(file, " ");
		string(file, "}");
	}
	string(file, ";");
	newline(file);
}
