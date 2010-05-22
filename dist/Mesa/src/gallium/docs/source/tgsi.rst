TGSI
====

TGSI, Tungsten Graphics Shader Infrastructure, is an intermediate language
for describing shaders. Since Gallium is inherently shaderful, shaders are
an important part of the API. TGSI is the only intermediate representation
used by all drivers.

Basics
------

All TGSI instructions, known as *opcodes*, operate on arbitrary-precision
floating-point four-component vectors. An opcode may have up to one
destination register, known as *dst*, and between zero and three source
registers, called *src0* through *src2*, or simply *src* if there is only
one.

Some instructions, like :opcode:`I2F`, permit re-interpretation of vector
components as integers. Other instructions permit using registers as
two-component vectors with double precision; see :ref:`Double Opcodes`.

When an instruction has a scalar result, the result is usually copied into
each of the components of *dst*. When this happens, the result is said to be
*replicated* to *dst*. :opcode:`RCP` is one such instruction.

Instruction Set
---------------

From GL_NV_vertex_program
^^^^^^^^^^^^^^^^^^^^^^^^^


.. opcode:: ARL - Address Register Load

.. math::

  dst.x = \lfloor src.x\rfloor

  dst.y = \lfloor src.y\rfloor

  dst.z = \lfloor src.z\rfloor

  dst.w = \lfloor src.w\rfloor


.. opcode:: MOV - Move

.. math::

  dst.x = src.x

  dst.y = src.y

  dst.z = src.z

  dst.w = src.w


.. opcode:: LIT - Light Coefficients

.. math::

  dst.x = 1

  dst.y = max(src.x, 0)

  dst.z = (src.x > 0) ? max(src.y, 0)^{clamp(src.w, -128, 128))} : 0

  dst.w = 1


.. opcode:: RCP - Reciprocal

This instruction replicates its result.

.. math::

  dst = \frac{1}{src.x}


.. opcode:: RSQ - Reciprocal Square Root

This instruction replicates its result.

.. math::

  dst = \frac{1}{\sqrt{|src.x|}}


.. opcode:: EXP - Approximate Exponential Base 2

.. math::

  dst.x = 2^{\lfloor src.x\rfloor}

  dst.y = src.x - \lfloor src.x\rfloor

  dst.z = 2^{src.x}

  dst.w = 1


.. opcode:: LOG - Approximate Logarithm Base 2

.. math::

  dst.x = \lfloor\log_2{|src.x|}\rfloor

  dst.y = \frac{|src.x|}{2^{\lfloor\log_2{|src.x|}\rfloor}}

  dst.z = \log_2{|src.x|}

  dst.w = 1


.. opcode:: MUL - Multiply

.. math::

  dst.x = src0.x \times src1.x

  dst.y = src0.y \times src1.y

  dst.z = src0.z \times src1.z

  dst.w = src0.w \times src1.w


.. opcode:: ADD - Add

.. math::

  dst.x = src0.x + src1.x

  dst.y = src0.y + src1.y

  dst.z = src0.z + src1.z

  dst.w = src0.w + src1.w


.. opcode:: DP3 - 3-component Dot Product

This instruction replicates its result.

.. math::

  dst = src0.x \times src1.x + src0.y \times src1.y + src0.z \times src1.z


.. opcode:: DP4 - 4-component Dot Product

This instruction replicates its result.

.. math::

  dst = src0.x \times src1.x + src0.y \times src1.y + src0.z \times src1.z + src0.w \times src1.w


.. opcode:: DST - Distance Vector

.. math::

  dst.x = 1

  dst.y = src0.y \times src1.y

  dst.z = src0.z

  dst.w = src1.w


.. opcode:: MIN - Minimum

.. math::

  dst.x = min(src0.x, src1.x)

  dst.y = min(src0.y, src1.y)

  dst.z = min(src0.z, src1.z)

  dst.w = min(src0.w, src1.w)


.. opcode:: MAX - Maximum

.. math::

  dst.x = max(src0.x, src1.x)

  dst.y = max(src0.y, src1.y)

  dst.z = max(src0.z, src1.z)

  dst.w = max(src0.w, src1.w)


.. opcode:: SLT - Set On Less Than

.. math::

  dst.x = (src0.x < src1.x) ? 1 : 0

  dst.y = (src0.y < src1.y) ? 1 : 0

  dst.z = (src0.z < src1.z) ? 1 : 0

  dst.w = (src0.w < src1.w) ? 1 : 0


.. opcode:: SGE - Set On Greater Equal Than

.. math::

  dst.x = (src0.x >= src1.x) ? 1 : 0

  dst.y = (src0.y >= src1.y) ? 1 : 0

  dst.z = (src0.z >= src1.z) ? 1 : 0

  dst.w = (src0.w >= src1.w) ? 1 : 0


.. opcode:: MAD - Multiply And Add

.. math::

  dst.x = src0.x \times src1.x + src2.x

  dst.y = src0.y \times src1.y + src2.y

  dst.z = src0.z \times src1.z + src2.z

  dst.w = src0.w \times src1.w + src2.w


.. opcode:: SUB - Subtract

.. math::

  dst.x = src0.x - src1.x

  dst.y = src0.y - src1.y

  dst.z = src0.z - src1.z

  dst.w = src0.w - src1.w


.. opcode:: LRP - Linear Interpolate

.. math::

  dst.x = src0.x \times src1.x + (1 - src0.x) \times src2.x

  dst.y = src0.y \times src1.y + (1 - src0.y) \times src2.y

  dst.z = src0.z \times src1.z + (1 - src0.z) \times src2.z

  dst.w = src0.w \times src1.w + (1 - src0.w) \times src2.w


.. opcode:: CND - Condition

.. math::

  dst.x = (src2.x > 0.5) ? src0.x : src1.x

  dst.y = (src2.y > 0.5) ? src0.y : src1.y

  dst.z = (src2.z > 0.5) ? src0.z : src1.z

  dst.w = (src2.w > 0.5) ? src0.w : src1.w


.. opcode:: DP2A - 2-component Dot Product And Add

.. math::

  dst.x = src0.x \times src1.x + src0.y \times src1.y + src2.x

  dst.y = src0.x \times src1.x + src0.y \times src1.y + src2.x

  dst.z = src0.x \times src1.x + src0.y \times src1.y + src2.x

  dst.w = src0.x \times src1.x + src0.y \times src1.y + src2.x


.. opcode:: FRAC - Fraction

.. math::

  dst.x = src.x - \lfloor src.x\rfloor

  dst.y = src.y - \lfloor src.y\rfloor

  dst.z = src.z - \lfloor src.z\rfloor

  dst.w = src.w - \lfloor src.w\rfloor


.. opcode:: CLAMP - Clamp

.. math::

  dst.x = clamp(src0.x, src1.x, src2.x)

  dst.y = clamp(src0.y, src1.y, src2.y)

  dst.z = clamp(src0.z, src1.z, src2.z)

  dst.w = clamp(src0.w, src1.w, src2.w)


.. opcode:: FLR - Floor

This is identical to :opcode:`ARL`.

.. math::

  dst.x = \lfloor src.x\rfloor

  dst.y = \lfloor src.y\rfloor

  dst.z = \lfloor src.z\rfloor

  dst.w = \lfloor src.w\rfloor


.. opcode:: ROUND - Round

.. math::

  dst.x = round(src.x)

  dst.y = round(src.y)

  dst.z = round(src.z)

  dst.w = round(src.w)


.. opcode:: EX2 - Exponential Base 2

This instruction replicates its result.

.. math::

  dst = 2^{src.x}


.. opcode:: LG2 - Logarithm Base 2

This instruction replicates its result.

.. math::

  dst = \log_2{src.x}


.. opcode:: POW - Power

This instruction replicates its result.

.. math::

  dst = src0.x^{src1.x}

.. opcode:: XPD - Cross Product

.. math::

  dst.x = src0.y \times src1.z - src1.y \times src0.z

  dst.y = src0.z \times src1.x - src1.z \times src0.x

  dst.z = src0.x \times src1.y - src1.x \times src0.y

  dst.w = 1


.. opcode:: ABS - Absolute

.. math::

  dst.x = |src.x|

  dst.y = |src.y|

  dst.z = |src.z|

  dst.w = |src.w|


.. opcode:: RCC - Reciprocal Clamped

This instruction replicates its result.

XXX cleanup on aisle three

.. math::

  dst = (1 / src.x) > 0 ? clamp(1 / src.x, 5.42101e-020, 1.884467e+019) : clamp(1 / src.x, -1.884467e+019, -5.42101e-020)


.. opcode:: DPH - Homogeneous Dot Product

This instruction replicates its result.

.. math::

  dst = src0.x \times src1.x + src0.y \times src1.y + src0.z \times src1.z + src1.w


.. opcode:: COS - Cosine

This instruction replicates its result.

.. math::

  dst = \cos{src.x}


.. opcode:: DDX - Derivative Relative To X

.. math::

  dst.x = partialx(src.x)

  dst.y = partialx(src.y)

  dst.z = partialx(src.z)

  dst.w = partialx(src.w)


.. opcode:: DDY - Derivative Relative To Y

.. math::

  dst.x = partialy(src.x)

  dst.y = partialy(src.y)

  dst.z = partialy(src.z)

  dst.w = partialy(src.w)


.. opcode:: KILP - Predicated Discard

  discard


.. opcode:: PK2H - Pack Two 16-bit Floats

  TBD


.. opcode:: PK2US - Pack Two Unsigned 16-bit Scalars

  TBD


.. opcode:: PK4B - Pack Four Signed 8-bit Scalars

  TBD


.. opcode:: PK4UB - Pack Four Unsigned 8-bit Scalars

  TBD


.. opcode:: RFL - Reflection Vector

.. math::

  dst.x = 2 \times (src0.x \times src1.x + src0.y \times src1.y + src0.z \times src1.z) / (src0.x \times src0.x + src0.y \times src0.y + src0.z \times src0.z) \times src0.x - src1.x

  dst.y = 2 \times (src0.x \times src1.x + src0.y \times src1.y + src0.z \times src1.z) / (src0.x \times src0.x + src0.y \times src0.y + src0.z \times src0.z) \times src0.y - src1.y

  dst.z = 2 \times (src0.x \times src1.x + src0.y \times src1.y + src0.z \times src1.z) / (src0.x \times src0.x + src0.y \times src0.y + src0.z \times src0.z) \times src0.z - src1.z

  dst.w = 1

.. note::

   Considered for removal.


.. opcode:: SEQ - Set On Equal

.. math::

  dst.x = (src0.x == src1.x) ? 1 : 0

  dst.y = (src0.y == src1.y) ? 1 : 0

  dst.z = (src0.z == src1.z) ? 1 : 0

  dst.w = (src0.w == src1.w) ? 1 : 0


.. opcode:: SFL - Set On False

This instruction replicates its result.

.. math::

  dst = 0

.. note::

   Considered for removal.


.. opcode:: SGT - Set On Greater Than

.. math::

  dst.x = (src0.x > src1.x) ? 1 : 0

  dst.y = (src0.y > src1.y) ? 1 : 0

  dst.z = (src0.z > src1.z) ? 1 : 0

  dst.w = (src0.w > src1.w) ? 1 : 0


.. opcode:: SIN - Sine

This instruction replicates its result.

.. math::

  dst = \sin{src.x}


.. opcode:: SLE - Set On Less Equal Than

.. math::

  dst.x = (src0.x <= src1.x) ? 1 : 0

  dst.y = (src0.y <= src1.y) ? 1 : 0

  dst.z = (src0.z <= src1.z) ? 1 : 0

  dst.w = (src0.w <= src1.w) ? 1 : 0


.. opcode:: SNE - Set On Not Equal

.. math::

  dst.x = (src0.x != src1.x) ? 1 : 0

  dst.y = (src0.y != src1.y) ? 1 : 0

  dst.z = (src0.z != src1.z) ? 1 : 0

  dst.w = (src0.w != src1.w) ? 1 : 0


.. opcode:: STR - Set On True

This instruction replicates its result.

.. math::

  dst = 1


.. opcode:: TEX - Texture Lookup

  TBD


.. opcode:: TXD - Texture Lookup with Derivatives

  TBD


.. opcode:: TXP - Projective Texture Lookup

  TBD


.. opcode:: UP2H - Unpack Two 16-Bit Floats

  TBD

.. note::

   Considered for removal.

.. opcode:: UP2US - Unpack Two Unsigned 16-Bit Scalars

  TBD

.. note::

   Considered for removal.

.. opcode:: UP4B - Unpack Four Signed 8-Bit Values

  TBD

.. note::

   Considered for removal.

.. opcode:: UP4UB - Unpack Four Unsigned 8-Bit Scalars

  TBD

.. note::

   Considered for removal.

.. opcode:: X2D - 2D Coordinate Transformation

.. math::

  dst.x = src0.x + src1.x \times src2.x + src1.y \times src2.y

  dst.y = src0.y + src1.x \times src2.z + src1.y \times src2.w

  dst.z = src0.x + src1.x \times src2.x + src1.y \times src2.y

  dst.w = src0.y + src1.x \times src2.z + src1.y \times src2.w

.. note::

   Considered for removal.


From GL_NV_vertex_program2
^^^^^^^^^^^^^^^^^^^^^^^^^^


.. opcode:: ARA - Address Register Add

  TBD

.. note::

   Considered for removal.

.. opcode:: ARR - Address Register Load With Round

.. math::

  dst.x = round(src.x)

  dst.y = round(src.y)

  dst.z = round(src.z)

  dst.w = round(src.w)


.. opcode:: BRA - Branch

  pc = target

.. note::

   Considered for removal.

.. opcode:: CAL - Subroutine Call

  push(pc)
  pc = target


.. opcode:: RET - Subroutine Call Return

  pc = pop()

  Potential restrictions:  
  * Only occurs at end of function.

.. opcode:: SSG - Set Sign

.. math::

  dst.x = (src.x > 0) ? 1 : (src.x < 0) ? -1 : 0

  dst.y = (src.y > 0) ? 1 : (src.y < 0) ? -1 : 0

  dst.z = (src.z > 0) ? 1 : (src.z < 0) ? -1 : 0

  dst.w = (src.w > 0) ? 1 : (src.w < 0) ? -1 : 0


.. opcode:: CMP - Compare

.. math::

  dst.x = (src0.x < 0) ? src1.x : src2.x

  dst.y = (src0.y < 0) ? src1.y : src2.y

  dst.z = (src0.z < 0) ? src1.z : src2.z

  dst.w = (src0.w < 0) ? src1.w : src2.w


.. opcode:: KIL - Conditional Discard

.. math::

  if (src.x < 0 || src.y < 0 || src.z < 0 || src.w < 0)
    discard
  endif


.. opcode:: SCS - Sine Cosine

.. math::

  dst.x = \cos{src.x}

  dst.y = \sin{src.x}

  dst.z = 0

  dst.y = 1


.. opcode:: TXB - Texture Lookup With Bias

  TBD


.. opcode:: NRM - 3-component Vector Normalise

.. math::

  dst.x = src.x / (src.x \times src.x + src.y \times src.y + src.z \times src.z)

  dst.y = src.y / (src.x \times src.x + src.y \times src.y + src.z \times src.z)

  dst.z = src.z / (src.x \times src.x + src.y \times src.y + src.z \times src.z)

  dst.w = 1


.. opcode:: DIV - Divide

.. math::

  dst.x = \frac{src0.x}{src1.x}

  dst.y = \frac{src0.y}{src1.y}

  dst.z = \frac{src0.z}{src1.z}

  dst.w = \frac{src0.w}{src1.w}


.. opcode:: DP2 - 2-component Dot Product

This instruction replicates its result.

.. math::

  dst = src0.x \times src1.x + src0.y \times src1.y


.. opcode:: TXL - Texture Lookup With LOD

  TBD


.. opcode:: BRK - Break

  TBD


.. opcode:: IF - If

  TBD


.. opcode:: BGNFOR - Begin a For-Loop

  dst.x = floor(src.x)
  dst.y = floor(src.y)
  dst.z = floor(src.z)

  if (dst.y <= 0)
    pc = [matching ENDFOR] + 1
  endif

  Note: The destination must be a loop register.
        The source must be a constant register.

.. note::

   Considered for cleanup.

.. note::

   Considered for removal.


.. opcode:: REP - Repeat

  TBD


.. opcode:: ELSE - Else

  TBD


.. opcode:: ENDIF - End If

  TBD


.. opcode:: ENDFOR - End a For-Loop

  dst.x = dst.x + dst.z
  dst.y = dst.y - 1.0

  if (dst.y > 0)
    pc = [matching BGNFOR instruction] + 1
  endif

  Note: The destination must be a loop register.

.. note::

   Considered for cleanup.

.. note::

   Considered for removal.

.. opcode:: ENDREP - End Repeat

  TBD


.. opcode:: PUSHA - Push Address Register On Stack

  push(src.x)
  push(src.y)
  push(src.z)
  push(src.w)

.. note::

   Considered for cleanup.

.. note::

   Considered for removal.

.. opcode:: POPA - Pop Address Register From Stack

  dst.w = pop()
  dst.z = pop()
  dst.y = pop()
  dst.x = pop()

.. note::

   Considered for cleanup.

.. note::

   Considered for removal.


From GL_NV_gpu_program4
^^^^^^^^^^^^^^^^^^^^^^^^

Support for these opcodes indicated by a special pipe capability bit (TBD).

.. opcode:: CEIL - Ceiling

.. math::

  dst.x = \lceil src.x\rceil

  dst.y = \lceil src.y\rceil

  dst.z = \lceil src.z\rceil

  dst.w = \lceil src.w\rceil


.. opcode:: I2F - Integer To Float

.. math::

  dst.x = (float) src.x

  dst.y = (float) src.y

  dst.z = (float) src.z

  dst.w = (float) src.w


.. opcode:: NOT - Bitwise Not

.. math::

  dst.x = ~src.x

  dst.y = ~src.y

  dst.z = ~src.z

  dst.w = ~src.w


.. opcode:: TRUNC - Truncate

.. math::

  dst.x = trunc(src.x)

  dst.y = trunc(src.y)

  dst.z = trunc(src.z)

  dst.w = trunc(src.w)


.. opcode:: SHL - Shift Left

.. math::

  dst.x = src0.x << src1.x

  dst.y = src0.y << src1.x

  dst.z = src0.z << src1.x

  dst.w = src0.w << src1.x


.. opcode:: SHR - Shift Right

.. math::

  dst.x = src0.x >> src1.x

  dst.y = src0.y >> src1.x

  dst.z = src0.z >> src1.x

  dst.w = src0.w >> src1.x


.. opcode:: AND - Bitwise And

.. math::

  dst.x = src0.x & src1.x

  dst.y = src0.y & src1.y

  dst.z = src0.z & src1.z

  dst.w = src0.w & src1.w


.. opcode:: OR - Bitwise Or

.. math::

  dst.x = src0.x | src1.x

  dst.y = src0.y | src1.y

  dst.z = src0.z | src1.z

  dst.w = src0.w | src1.w


.. opcode:: MOD - Modulus

.. math::

  dst.x = src0.x \bmod src1.x

  dst.y = src0.y \bmod src1.y

  dst.z = src0.z \bmod src1.z

  dst.w = src0.w \bmod src1.w


.. opcode:: XOR - Bitwise Xor

.. math::

  dst.x = src0.x \oplus src1.x

  dst.y = src0.y \oplus src1.y

  dst.z = src0.z \oplus src1.z

  dst.w = src0.w \oplus src1.w


.. opcode:: SAD - Sum Of Absolute Differences

.. math::

  dst.x = |src0.x - src1.x| + src2.x

  dst.y = |src0.y - src1.y| + src2.y

  dst.z = |src0.z - src1.z| + src2.z

  dst.w = |src0.w - src1.w| + src2.w


.. opcode:: TXF - Texel Fetch

  TBD


.. opcode:: TXQ - Texture Size Query

  TBD


.. opcode:: CONT - Continue

  TBD


From GL_NV_geometry_program4
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^


.. opcode:: EMIT - Emit

  TBD


.. opcode:: ENDPRIM - End Primitive

  TBD


From GLSL
^^^^^^^^^^


.. opcode:: BGNLOOP - Begin a Loop

  TBD


.. opcode:: BGNSUB - Begin Subroutine

  TBD


.. opcode:: ENDLOOP - End a Loop

  TBD


.. opcode:: ENDSUB - End Subroutine

  TBD


.. opcode:: NOP - No Operation

  Do nothing.


.. opcode:: NRM4 - 4-component Vector Normalise

This instruction replicates its result.

.. math::

  dst = \frac{src.x}{src.x \times src.x + src.y \times src.y + src.z \times src.z + src.w \times src.w}


ps_2_x
^^^^^^^^^^^^


.. opcode:: CALLNZ - Subroutine Call If Not Zero

  TBD


.. opcode:: IFC - If

  TBD


.. opcode:: BREAKC - Break Conditional

  TBD

.. _doubleopcodes:

Double Opcodes
^^^^^^^^^^^^^^^

.. opcode:: DADD - Add Double

.. math::

  dst.xy = src0.xy + src1.xy

  dst.zw = src0.zw + src1.zw


.. opcode:: DDIV - Divide Double

.. math::

  dst.xy = src0.xy / src1.xy

  dst.zw = src0.zw / src1.zw

.. opcode:: DSEQ - Set Double on Equal

.. math::

  dst.xy = src0.xy == src1.xy ? 1.0F : 0.0F

  dst.zw = src0.zw == src1.zw ? 1.0F : 0.0F

.. opcode:: DSLT - Set Double on Less than

.. math::

  dst.xy = src0.xy < src1.xy ? 1.0F : 0.0F

  dst.zw = src0.zw < src1.zw ? 1.0F : 0.0F

.. opcode:: DFRAC - Double Fraction

.. math::

  dst.xy = src.xy - \lfloor src.xy\rfloor

  dst.zw = src.zw - \lfloor src.zw\rfloor


.. opcode:: DFRACEXP - Convert Double Number to Fractional and Integral Components

.. math::

  dst0.xy = frexp(src.xy, dst1.xy)

  dst0.zw = frexp(src.zw, dst1.zw)

.. opcode:: DLDEXP - Multiple Double Number by Integral Power of 2

.. math::

  dst.xy = ldexp(src0.xy, src1.xy)

  dst.zw = ldexp(src0.zw, src1.zw)

.. opcode:: DMIN - Minimum Double

.. math::

  dst.xy = min(src0.xy, src1.xy)

  dst.zw = min(src0.zw, src1.zw)

.. opcode:: DMAX - Maximum Double

.. math::

  dst.xy = max(src0.xy, src1.xy)

  dst.zw = max(src0.zw, src1.zw)

.. opcode:: DMUL - Multiply Double

.. math::

  dst.xy = src0.xy \times src1.xy

  dst.zw = src0.zw \times src1.zw


.. opcode:: DMAD - Multiply And Add Doubles

.. math::

  dst.xy = src0.xy \times src1.xy + src2.xy

  dst.zw = src0.zw \times src1.zw + src2.zw


.. opcode:: DRCP - Reciprocal Double

.. math::

   dst.xy = \frac{1}{src.xy}

   dst.zw = \frac{1}{src.zw}

.. opcode:: DSQRT - Square root double

.. math::

   dst.xy = \sqrt{src.xy}

   dst.zw = \sqrt{src.zw}


Explanation of symbols used
------------------------------


Functions
^^^^^^^^^^^^^^


  :math:`|x|`       Absolute value of `x`.

  :math:`\lceil x \rceil` Ceiling of `x`.

  clamp(x,y,z)      Clamp x between y and z.
                    (x < y) ? y : (x > z) ? z : x

  :math:`\lfloor x\rfloor` Floor of `x`.

  :math:`\log_2{x}` Logarithm of `x`, base 2.

  max(x,y)          Maximum of x and y.
                    (x > y) ? x : y

  min(x,y)          Minimum of x and y.
                    (x < y) ? x : y

  partialx(x)       Derivative of x relative to fragment's X.

  partialy(x)       Derivative of x relative to fragment's Y.

  pop()             Pop from stack.

  :math:`x^y`       `x` to the power `y`.

  push(x)           Push x on stack.

  round(x)          Round x.

  trunc(x)          Truncate x, i.e. drop the fraction bits.


Keywords
^^^^^^^^^^^^^


  discard           Discard fragment.

  pc                Program counter.

  target            Label of target instruction.


Other tokens
---------------


Declaration
^^^^^^^^^^^


Declares a register that is will be referenced as an operand in Instruction
tokens.

File field contains register file that is being declared and is one
of TGSI_FILE.

UsageMask field specifies which of the register components can be accessed
and is one of TGSI_WRITEMASK.

Interpolate field is only valid for fragment shader INPUT register files.
It specifes the way input is being interpolated by the rasteriser and is one
of TGSI_INTERPOLATE.

If Dimension flag is set to 1, a Declaration Dimension token follows.

If Semantic flag is set to 1, a Declaration Semantic token follows.

CylindricalWrap bitfield is only valid for fragment shader INPUT register
files. It specifies which register components should be subject to cylindrical
wrapping when interpolating by the rasteriser. If TGSI_CYLINDRICAL_WRAP_X
is set to 1, the X component should be interpolated according to cylindrical
wrapping rules.


Declaration Semantic
^^^^^^^^^^^^^^^^^^^^^^^^


  Follows Declaration token if Semantic bit is set.

  Since its purpose is to link a shader with other stages of the pipeline,
  it is valid to follow only those Declaration tokens that declare a register
  either in INPUT or OUTPUT file.

  SemanticName field contains the semantic name of the register being declared.
  There is no default value.

  SemanticIndex is an optional subscript that can be used to distinguish
  different register declarations with the same semantic name. The default value
  is 0.

  The meanings of the individual semantic names are explained in the following
  sections.

TGSI_SEMANTIC_POSITION
""""""""""""""""""""""

Position, sometimes known as HPOS or WPOS for historical reasons, is the
location of the vertex in space, in ``(x, y, z, w)`` format. ``x``, ``y``, and ``z``
are the Cartesian coordinates, and ``w`` is the homogenous coordinate and used
for the perspective divide, if enabled.

As a vertex shader output, position should be scaled to the viewport. When
used in fragment shaders, position will be in window coordinates. The convention
used depends on the FS_COORD_ORIGIN and FS_COORD_PIXEL_CENTER properties.

XXX additionally, is there a way to configure the perspective divide? it's
accelerated on most chipsets AFAIK...

Position, if not specified, usually defaults to ``(0, 0, 0, 1)``, and can
be partially specified as ``(x, y, 0, 1)`` or ``(x, y, z, 1)``.

XXX usually? can we solidify that?

TGSI_SEMANTIC_COLOR
"""""""""""""""""""

Colors are used to, well, color the primitives. Colors are always in
``(r, g, b, a)`` format.

If alpha is not specified, it defaults to 1.

TGSI_SEMANTIC_BCOLOR
""""""""""""""""""""

Back-facing colors are only used for back-facing polygons, and are only valid
in vertex shader outputs. After rasterization, all polygons are front-facing
and COLOR and BCOLOR end up occupying the same slots in the fragment, so
all BCOLORs effectively become regular COLORs in the fragment shader.

TGSI_SEMANTIC_FOG
"""""""""""""""""

The fog coordinate historically has been used to replace the depth coordinate
for generation of fog in dedicated fog blocks. Gallium, however, does not use
dedicated fog acceleration, placing it entirely in the fragment shader
instead.

The fog coordinate should be written in ``(f, 0, 0, 1)`` format. Only the first
component matters when writing from the vertex shader; the driver will ensure
that the coordinate is in this format when used as a fragment shader input.

TGSI_SEMANTIC_PSIZE
"""""""""""""""""""

PSIZE, or point size, is used to specify point sizes per-vertex. It should
be in ``(s, 0, 0, 1)`` format, where ``s`` is the (possibly clamped) point size.
Only the first component matters when writing from the vertex shader.

When using this semantic, be sure to set the appropriate state in the
:ref:`rasterizer` first.

TGSI_SEMANTIC_GENERIC
"""""""""""""""""""""

Generic semantics are nearly always used for texture coordinate attributes,
in ``(s, t, r, q)`` format. ``t`` and ``r`` may be unused for certain kinds
of lookups, and ``q`` is the level-of-detail bias for biased sampling.

These attributes are called "generic" because they may be used for anything
else, including parameters, texture generation information, or anything that
can be stored inside a four-component vector.

TGSI_SEMANTIC_NORMAL
""""""""""""""""""""

Vertex normal; could be used to implement per-pixel lighting for legacy APIs
that allow mixing fixed-function and programmable stages.

TGSI_SEMANTIC_FACE
""""""""""""""""""

FACE is the facing bit, to store the facing information for the fragment
shader. ``(f, 0, 0, 1)`` is the format. The first component will be positive
when the fragment is front-facing, and negative when the component is
back-facing.

TGSI_SEMANTIC_EDGEFLAG
""""""""""""""""""""""

XXX no clue


Properties
^^^^^^^^^^^^^^^^^^^^^^^^


  Properties are general directives that apply to the whole TGSI program.

FS_COORD_ORIGIN
"""""""""""""""

Specifies the fragment shader TGSI_SEMANTIC_POSITION coordinate origin.
The default value is UPPER_LEFT.

If UPPER_LEFT, the position will be (0,0) at the upper left corner and
increase downward and rightward.
If LOWER_LEFT, the position will be (0,0) at the lower left corner and
increase upward and rightward.

OpenGL defaults to LOWER_LEFT, and is configurable with the
GL_ARB_fragment_coord_conventions extension.

DirectX 9/10 use UPPER_LEFT.

FS_COORD_PIXEL_CENTER
"""""""""""""""""""""

Specifies the fragment shader TGSI_SEMANTIC_POSITION pixel center convention.
The default value is HALF_INTEGER.

If HALF_INTEGER, the fractionary part of the position will be 0.5
If INTEGER, the fractionary part of the position will be 0.0

Note that this does not affect the set of fragments generated by
rasterization, which is instead controlled by gl_rasterization_rules in the
rasterizer.

OpenGL defaults to HALF_INTEGER, and is configurable with the
GL_ARB_fragment_coord_conventions extension.

DirectX 9 uses INTEGER.
DirectX 10 uses HALF_INTEGER.



Texture Sampling and Texture Formats
------------------------------------

This table shows how texture image components are returned as (x,y,z,w) tuples
by TGSI texture instructions, such as :opcode:`TEX`, :opcode:`TXD`, and
:opcode:`TXP`. For reference, OpenGL and Direct3D conventions are shown as
well.

+--------------------+--------------+--------------------+--------------+
| Texture Components | Gallium      | OpenGL             | Direct3D 9   |
+====================+==============+====================+==============+
| R                  | XXX TBD      | (r, 0, 0, 1)       | (r, 1, 1, 1) |
+--------------------+--------------+--------------------+--------------+
| RG                 | XXX TBD      | (r, g, 0, 1)       | (r, g, 1, 1) |
+--------------------+--------------+--------------------+--------------+
| RGB                | (r, g, b, 1) | (r, g, b, 1)       | (r, g, b, 1) |
+--------------------+--------------+--------------------+--------------+
| RGBA               | (r, g, b, a) | (r, g, b, a)       | (r, g, b, a) |
+--------------------+--------------+--------------------+--------------+
| A                  | (0, 0, 0, a) | (0, 0, 0, a)       | (0, 0, 0, a) |
+--------------------+--------------+--------------------+--------------+
| L                  | (l, l, l, 1) | (l, l, l, 1)       | (l, l, l, 1) |
+--------------------+--------------+--------------------+--------------+
| LA                 | (l, l, l, a) | (l, l, l, a)       | (l, l, l, a) |
+--------------------+--------------+--------------------+--------------+
| I                  | (i, i, i, i) | (i, i, i, i)       | N/A          |
+--------------------+--------------+--------------------+--------------+
| UV                 | XXX TBD      | (0, 0, 0, 1)       | (u, v, 1, 1) |
|                    |              | [#envmap-bumpmap]_ |              |
+--------------------+--------------+--------------------+--------------+
| Z                  | XXX TBD      | (z, z, z, 1)       | (0, z, 0, 1) |
|                    |              | [#depth-tex-mode]_ |              |
+--------------------+--------------+--------------------+--------------+

.. [#envmap-bumpmap] http://www.opengl.org/registry/specs/ATI/envmap_bumpmap.txt
.. [#depth-tex-mode] the default is (z, z, z, 1) but may also be (0, 0, 0, z)
   or (z, z, z, z) depending on the value of GL_DEPTH_TEXTURE_MODE.
