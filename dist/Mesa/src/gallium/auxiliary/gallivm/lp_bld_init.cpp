/**************************************************************************
 *
 * Copyright 2009 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#include <llvm/Config/config.h>
#include <llvm/Target/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>

#include "pipe/p_config.h"

#include "lp_bld_init.h"


extern "C" void LLVMLinkInJIT();


extern "C" void
lp_build_init(void)
{
#if defined(PIPE_OS_WINDOWS) && defined(PIPE_ARCH_X86)
   /*
    * This is mis-detected on some hardware / software combinations.
    */
   llvm::StackAlignment = 4;
   llvm::RealignStack = true;
#endif

   /* Same as LLVMInitializeNativeTarget(); */
   llvm::InitializeNativeTarget();

   LLVMLinkInJIT();
}


/* 
 * Hack to allow the linking of release LLVM static libraries on a debug build.
 *
 * See also:
 * - http://social.msdn.microsoft.com/Forums/en-US/vclanguage/thread/7234ea2b-0042-42ed-b4e2-5d8644dfb57d
 */
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdefs.h>
extern "C" _CRTIMP void __cdecl
_invalid_parameter_noinfo(void) {}
#endif
