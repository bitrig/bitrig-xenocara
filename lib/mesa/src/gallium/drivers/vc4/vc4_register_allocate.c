/*
 * Copyright © 2014 Broadcom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "util/ralloc.h"
#include "util/register_allocate.h"
#include "vc4_context.h"
#include "vc4_qir.h"
#include "vc4_qpu.h"

#define QPU_R(file, index) { QPU_MUX_##file, index }

static const struct qpu_reg vc4_regs[] = {
        { QPU_MUX_R0, 0},
        { QPU_MUX_R1, 0},
        { QPU_MUX_R2, 0},
        { QPU_MUX_R3, 0},
        { QPU_MUX_R4, 0},
        QPU_R(A, 0),
        QPU_R(B, 0),
        QPU_R(A, 1),
        QPU_R(B, 1),
        QPU_R(A, 2),
        QPU_R(B, 2),
        QPU_R(A, 3),
        QPU_R(B, 3),
        QPU_R(A, 4),
        QPU_R(B, 4),
        QPU_R(A, 5),
        QPU_R(B, 5),
        QPU_R(A, 6),
        QPU_R(B, 6),
        QPU_R(A, 7),
        QPU_R(B, 7),
        QPU_R(A, 8),
        QPU_R(B, 8),
        QPU_R(A, 9),
        QPU_R(B, 9),
        QPU_R(A, 10),
        QPU_R(B, 10),
        QPU_R(A, 11),
        QPU_R(B, 11),
        QPU_R(A, 12),
        QPU_R(B, 12),
        QPU_R(A, 13),
        QPU_R(B, 13),
        QPU_R(A, 14),
        QPU_R(B, 14),
        QPU_R(A, 15),
        QPU_R(B, 15),
        QPU_R(A, 16),
        QPU_R(B, 16),
        QPU_R(A, 17),
        QPU_R(B, 17),
        QPU_R(A, 18),
        QPU_R(B, 18),
        QPU_R(A, 19),
        QPU_R(B, 19),
        QPU_R(A, 20),
        QPU_R(B, 20),
        QPU_R(A, 21),
        QPU_R(B, 21),
        QPU_R(A, 22),
        QPU_R(B, 22),
        QPU_R(A, 23),
        QPU_R(B, 23),
        QPU_R(A, 24),
        QPU_R(B, 24),
        QPU_R(A, 25),
        QPU_R(B, 25),
        QPU_R(A, 26),
        QPU_R(B, 26),
        QPU_R(A, 27),
        QPU_R(B, 27),
        QPU_R(A, 28),
        QPU_R(B, 28),
        QPU_R(A, 29),
        QPU_R(B, 29),
        QPU_R(A, 30),
        QPU_R(B, 30),
        QPU_R(A, 31),
        QPU_R(B, 31),
};
#define ACC_INDEX     0
#define AB_INDEX      (ACC_INDEX + 5)

static void
vc4_alloc_reg_set(struct vc4_context *vc4)
{
        assert(vc4_regs[AB_INDEX].addr == 0);
        assert(vc4_regs[AB_INDEX + 1].addr == 0);
        STATIC_ASSERT(ARRAY_SIZE(vc4_regs) == AB_INDEX + 64);

        if (vc4->regs)
                return;

        vc4->regs = ra_alloc_reg_set(vc4, ARRAY_SIZE(vc4_regs), true);

        vc4->reg_class_any = ra_alloc_reg_class(vc4->regs);
        vc4->reg_class_a_or_b_or_acc = ra_alloc_reg_class(vc4->regs);
        vc4->reg_class_r4_or_a = ra_alloc_reg_class(vc4->regs);
        vc4->reg_class_a = ra_alloc_reg_class(vc4->regs);
        for (uint32_t i = 0; i < ARRAY_SIZE(vc4_regs); i++) {
                /* Reserve ra31/rb31 for spilling fixup_raddr_conflict() in
                 * vc4_qpu_emit.c
                 */
                if (vc4_regs[i].addr == 31)
                        continue;

                /* R4 can't be written as a general purpose register. (it's
                 * TMU_NOSWAP as a write address).
                 */
                if (vc4_regs[i].mux == QPU_MUX_R4) {
                        ra_class_add_reg(vc4->regs, vc4->reg_class_r4_or_a, i);
                        ra_class_add_reg(vc4->regs, vc4->reg_class_any, i);
                        continue;
                }

                ra_class_add_reg(vc4->regs, vc4->reg_class_any, i);
                ra_class_add_reg(vc4->regs, vc4->reg_class_a_or_b_or_acc, i);
        }

        for (uint32_t i = AB_INDEX; i < AB_INDEX + 64; i += 2) {
                ra_class_add_reg(vc4->regs, vc4->reg_class_a, i);
                ra_class_add_reg(vc4->regs, vc4->reg_class_r4_or_a, i);
        }

        ra_set_finalize(vc4->regs, NULL);
}

struct node_to_temp_map {
        uint32_t temp;
        uint32_t priority;
};

static int
node_to_temp_priority(const void *in_a, const void *in_b)
{
        const struct node_to_temp_map *a = in_a;
        const struct node_to_temp_map *b = in_b;

        return a->priority - b->priority;
}

#define CLASS_BIT_A			(1 << 0)
#define CLASS_BIT_B_OR_ACC		(1 << 1)
#define CLASS_BIT_R4			(1 << 2)

/**
 * Returns a mapping from QFILE_TEMP indices to struct qpu_regs.
 *
 * The return value should be freed by the caller.
 */
struct qpu_reg *
vc4_register_allocate(struct vc4_context *vc4, struct vc4_compile *c)
{
        struct node_to_temp_map map[c->num_temps];
        uint32_t temp_to_node[c->num_temps];
        uint32_t def[c->num_temps];
        uint32_t use[c->num_temps];
        uint8_t class_bits[c->num_temps];
        struct qpu_reg *temp_registers = calloc(c->num_temps,
                                                sizeof(*temp_registers));
        for (int i = 0; i < ARRAY_SIZE(def); i++)
                def[i] = ~0;
        memset(use, 0, sizeof(use));

        /* If things aren't ever written (undefined values), just read from
         * r0.
         */
        for (uint32_t i = 0; i < c->num_temps; i++)
                temp_registers[i] = qpu_rn(0);

        vc4_alloc_reg_set(vc4);

        struct ra_graph *g = ra_alloc_interference_graph(vc4->regs,
                                                         c->num_temps);

        /* Compute the live ranges so we can figure out interference.
         */
        uint32_t ip = 0;
        list_for_each_entry(struct qinst, inst, &c->instructions, link) {
                if (inst->dst.file == QFILE_TEMP) {
                        def[inst->dst.index] = MIN2(ip, def[inst->dst.index]);
                        use[inst->dst.index] = ip;
                }

                for (int i = 0; i < qir_get_op_nsrc(inst->op); i++) {
                        if (inst->src[i].file == QFILE_TEMP)
                                use[inst->src[i].index] = ip;
                }

                switch (inst->op) {
                case QOP_FRAG_Z:
                case QOP_FRAG_W:
                        /* The payload registers have values implicitly loaded
                         * at the start of the program.
                         */
                        def[inst->dst.index] = 0;
                        break;
                default:
                        break;
                }

                ip++;
        }

        for (uint32_t i = 0; i < c->num_temps; i++) {
                map[i].temp = i;
                map[i].priority = use[i] - def[i];
        }
        qsort(map, c->num_temps, sizeof(map[0]), node_to_temp_priority);
        for (uint32_t i = 0; i < c->num_temps; i++) {
                temp_to_node[map[i].temp] = i;
        }

        /* Figure out our register classes and preallocated registers.  We
         * start with any temp being able to be in any file, then instructions
         * incrementally remove bits that the temp definitely can't be in.
         */
        memset(class_bits,
               CLASS_BIT_A | CLASS_BIT_B_OR_ACC | CLASS_BIT_R4,
               sizeof(class_bits));

        ip = 0;
        list_for_each_entry(struct qinst, inst, &c->instructions, link) {
                if (qir_writes_r4(inst)) {
                        /* This instruction writes r4 (and optionally moves
                         * its result to a temp), so nothing else can be
                         * stored in r4 across it.
                         */
                        for (int i = 0; i < c->num_temps; i++) {
                                if (def[i] < ip && use[i] > ip)
                                        class_bits[i] &= ~CLASS_BIT_R4;
                        }
                } else {
                        /* R4 can't be written as a general purpose
                         * register. (it's TMU_NOSWAP as a write address).
                         */
                        if (inst->dst.file == QFILE_TEMP)
                                class_bits[inst->dst.index] &= ~CLASS_BIT_R4;
                }

                switch (inst->op) {
                case QOP_FRAG_Z:
                        ra_set_node_reg(g, temp_to_node[inst->dst.index],
                                        AB_INDEX + QPU_R_FRAG_PAYLOAD_ZW * 2 + 1);
                        break;

                case QOP_FRAG_W:
                        ra_set_node_reg(g, temp_to_node[inst->dst.index],
                                        AB_INDEX + QPU_R_FRAG_PAYLOAD_ZW * 2);
                        break;

                default:
                        break;
                }

                if (inst->dst.pack && !qir_is_mul(inst)) {
                        /* The non-MUL pack flags require an A-file dst
                         * register.
                         */
                        class_bits[inst->dst.index] &= CLASS_BIT_A;
                }

                if (qir_src_needs_a_file(inst)) {
                        switch (inst->op) {
                        case QOP_UNPACK_8A_F:
                        case QOP_UNPACK_8B_F:
                        case QOP_UNPACK_8C_F:
                        case QOP_UNPACK_8D_F:
                                /* Special case: these can be done as R4
                                 * unpacks, as well.
                                 */
                                class_bits[inst->src[0].index] &= (CLASS_BIT_A |
                                                                   CLASS_BIT_R4);
                                break;
                        default:
                                class_bits[inst->src[0].index] &= CLASS_BIT_A;
                                break;
                        }
                }
                ip++;
        }

        for (uint32_t i = 0; i < c->num_temps; i++) {
                int node = temp_to_node[i];

                switch (class_bits[i]) {
                case CLASS_BIT_A | CLASS_BIT_B_OR_ACC | CLASS_BIT_R4:
                        ra_set_node_class(g, node, vc4->reg_class_any);
                        break;
                case CLASS_BIT_A | CLASS_BIT_B_OR_ACC:
                        ra_set_node_class(g, node, vc4->reg_class_a_or_b_or_acc);
                        break;
                case CLASS_BIT_A | CLASS_BIT_R4:
                        ra_set_node_class(g, node, vc4->reg_class_r4_or_a);
                        break;
                case CLASS_BIT_A:
                        ra_set_node_class(g, node, vc4->reg_class_a);
                        break;
                default:
                        fprintf(stderr, "temp %d: bad class bits: 0x%x\n",
                                i, class_bits[i]);
                        abort();
                        break;
                }
        }

        for (uint32_t i = 0; i < c->num_temps; i++) {
                for (uint32_t j = i + 1; j < c->num_temps; j++) {
                        if (!(def[i] >= use[j] || def[j] >= use[i])) {
                                ra_add_node_interference(g,
                                                         temp_to_node[i],
                                                         temp_to_node[j]);
                        }
                }
        }

        bool ok = ra_allocate(g);
        if (!ok) {
                fprintf(stderr, "Failed to register allocate:\n");
                qir_dump(c);
                abort();
        }

        for (uint32_t i = 0; i < c->num_temps; i++) {
                temp_registers[i] = vc4_regs[ra_get_node_reg(g, temp_to_node[i])];

                /* If the value's never used, just write to the NOP register
                 * for clarity in debug output.
                 */
                if (def[i] == use[i])
                        temp_registers[i] = qpu_ra(QPU_W_NOP);
        }

        ralloc_free(g);

        return temp_registers;
}
