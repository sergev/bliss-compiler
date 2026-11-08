/*
 *++
 * llvm_execfuncgen.c - Executable function generation for LLVM back-end
 *
 *
 * This module generates LLVM IR for control expressions.
 *
 * Functions implemented in this module should include
 * only generic LLVM code; machine-specific functions
 * should go into other modules.  XXX
 *
 * Copyright © 2013-2024, Matthew Madison.
 * All rights reserved.
 * Distributed under license. See LICENSE.TXT for details.
 *--
 */

#include "llvmgen.h"

#define FUNCGENS \
FUNCGENDEF("MAXA",             gen_MINMAX,          LLVMIntSGT) \
FUNCGENDEF("MINA",             gen_MINMAX,          LLVMIntSLT) \
FUNCGENDEF("MAX",              gen_MINMAX,          LLVMIntSGT) \
FUNCGENDEF("MAXU",             gen_MINMAX,          LLVMIntUGT) \
FUNCGENDEF("MIN",              gen_MINMAX,          LLVMIntSLT) \
FUNCGENDEF("MINU",             gen_MINMAX,          LLVMIntULT) \
FUNCGENDEF("SIGN",             gen_SIGN,            0) \
FUNCGENDEF("ABS",              gen_ABS,             0) \
FUNCGENDEF("CH$ALLOCATION",    gen_ch_allocation,   0) \
FUNCGENDEF("CH$PTR",           gen_ch_pointer,      0) \
FUNCGENDEF("CH$PLUS",          gen_ch_pointer,      0) \
FUNCGENDEF("CH$RCHAR",         gen_ch_rchar,        0) \
FUNCGENDEF("CH$A_RCHAR",       gen_ch_rchar_a,      0) \
FUNCGENDEF("CH$RCHAR_A",       gen_ch_rchar_a,      1) \
FUNCGENDEF("CH$WCHAR",         gen_ch_wchar,        0) \
FUNCGENDEF("CH$A_WCHAR",       gen_ch_wchar_a,      0) \
FUNCGENDEF("CH$WCHAR_A",       gen_ch_wchar_a,      1) \
FUNCGENDEF("CH$MOVE",          gen_ch_move,         0) \
FUNCGENDEF("CH$FILL",          gen_ch_fill,         0) \
FUNCGENDEF("CH$EQL",           gen_ch_compare,      LLVMIntEQ) \
FUNCGENDEF("CH$NEQ",           gen_ch_compare,      LLVMIntNE) \
FUNCGENDEF("CH$LSS",           gen_ch_compare,      LLVMIntSLT) \
FUNCGENDEF("CH$LEQ",           gen_ch_compare,      LLVMIntSLE) \
FUNCGENDEF("CH$GTR",           gen_ch_compare,      LLVMIntSGT) \
FUNCGENDEF("CH$GEQ",           gen_ch_compare,      LLVMIntSGE) \
FUNCGENDEF("CH$COMPARE",       gen_ch_compare,      -1) \
FUNCGENDEF("CH$FIND_CH",       gen_ch_find,         0) \
FUNCGENDEF("CH$FIND_NOT_CH",   gen_ch_find,         1) \
FUNCGENDEF("CH$FAIL",          gen_ch_fail,         0) \
FUNCGENDEF("CH$FIND_SUB",      gen_ch_findsub,      0) \
FUNCGENDEF("CH$COPY",          gen_ch_copy,         0) \
FUNCGENDEF("CH$TRANSLATE",     gen_ch_translate,    0)

#define FUNCGENDEF(s_, f_, c_) \
    static LLVMValueRef f_ (gencodectx_t gctx, void *fctx, \
                            expr_node_t *node, LLVMTypeRef neededtype);
FUNCGENS
#undef FUNCGENDEF

#define FUNCGENDEF(s_, f_, c_) { s_, f_, (void *)(c_) },
llvm_execfuncgen_t gentable[] = {
    FUNCGENS
};
#undef FUNCGENDEF

static LLVMValueRef gen_actualcount(gencodectx_t gctx, void *fctx,
                                    expr_node_t *node, LLVMTypeRef neededtype);
static LLVMValueRef gen_actualparameter(gencodectx_t gctx, void *fctx,
                                        expr_node_t *node, LLVMTypeRef neededtype);
static LLVMValueRef gen_argptr(gencodectx_t gctx, void *fctx,
                               expr_node_t *node, LLVMTypeRef neededtype);

static LLVMValueRef gen_signal(gencodectx_t gctx, void *fctx,
                               expr_node_t *node, LLVMTypeRef neededtype);
static LLVMValueRef gen_setunwind(gencodectx_t gctx, void *fctx,
                                  expr_node_t *node, LLVMTypeRef neededtype);

// Condition-handling functions (LRM 17.3), predeclared
static struct {
    const char *name;
    unsigned int numargs;
    unsigned int flags;
    void *fctx;
    llvmgen_execfunc_fn func;
} chffuncs[] = {
    { "SIGNAL",      1, FUNC_M_VARARGS,                (void *) 0, gen_signal },
    { "SIGNAL_STOP", 1, FUNC_M_VARARGS|FUNC_M_NOVALUE, (void *) 1, gen_signal },
    { "SETUNWIND",   0, 0,                             0,          gen_setunwind },
};

static LLVMValueRef gen_float(gencodectx_t gctx, void *fctx,
                              expr_node_t *node, LLVMTypeRef neededtype);
static LLVMTypeRef flt_type(gencodectx_t gctx, char t);

// Floating-point BUILTINs (BLISS-32 User Manual, chapter 4). All
// operands are addresses. Types: F single, D and G double, H quad,
// L and I signed longword.
struct fltop_s {
    const char *name;
    char        op;     // + - * / (arithmetic), c (compare),
                        // v (convert), r (convert, rounded)
    char        type;
    char        dtype;  // conversion result type
};
static struct fltop_s fltops[] = {
    { "ADDF", '+', 'F', 0 }, { "ADDD", '+', 'D', 0 }, { "ADDG", '+', 'G', 0 },
    { "SUBF", '-', 'F', 0 }, { "SUBD", '-', 'D', 0 }, { "SUBG", '-', 'G', 0 },
    { "MULF", '*', 'F', 0 }, { "MULD", '*', 'D', 0 }, { "MULG", '*', 'G', 0 },
    { "DIVF", '/', 'F', 0 }, { "DIVD", '/', 'D', 0 }, { "DIVG", '/', 'G', 0 },
    { "CMPF", 'c', 'F', 0 }, { "CMPD", 'c', 'D', 0 }, { "CMPG", 'c', 'G', 0 },
    { "ADDH", '+', 'H', 0 }, { "SUBH", '-', 'H', 0 }, { "MULH", '*', 'H', 0 },
    { "DIVH", '/', 'H', 0 }, { "CMPH", 'c', 'H', 0 },
    { "CVTFH", 'v', 'F', 'H' }, { "CVTHF", 'v', 'H', 'F' },
    { "CVTGH", 'v', 'G', 'H' }, { "CVTHG", 'v', 'H', 'G' },
    { "CVTLH", 'v', 'L', 'H' }, { "CVTHL", 'v', 'H', 'L' }, { "CVTRHL", 'r', 'H', 'L' },
    { "CVTFD", 'v', 'F', 'D' }, { "CVTFG", 'v', 'F', 'G' },
    { "CVTDF", 'v', 'D', 'F' }, { "CVTGF", 'v', 'G', 'F' },
    { "CVTFL", 'v', 'F', 'L' }, { "CVTFI", 'v', 'F', 'I' },
    { "CVTDL", 'v', 'D', 'L' }, { "CVTDI", 'v', 'D', 'I' },
    { "CVTGL", 'v', 'G', 'L' },
    { "CVTLF", 'v', 'L', 'F' }, { "CVTIF", 'v', 'I', 'F' },
    { "CVTLD", 'v', 'L', 'D' }, { "CVTID", 'v', 'I', 'D' },
    { "CVTLG", 'v', 'L', 'G' },
    { "CVTRFL", 'r', 'F', 'L' }, { "CVTRDL", 'r', 'D', 'L' }, { "CVTRGL", 'r', 'G', 'L' },
};

// Bits in a floating-point value; 0 for an integer
#define FLT_BITS(t_) ((t_) == 'F' ? 32 : (t_) == 'H' ? 128 : \
                      ((t_) == 'D' || (t_) == 'G') ? 64 : 0)

// A conversion that can overflow (to an integer, or narrowing)
// returns 1, or 0 on overflow
#define FLT_HASVALUE(f_) ((f_)->op == 'c' || \
    (((f_)->op == 'v' || (f_)->op == 'r') && FLT_BITS((f_)->type) != 0 && \
     (FLT_BITS((f_)->dtype) == 0 || FLT_BITS((f_)->dtype) < FLT_BITS((f_)->type))))

// Linkage functions (LRM 13.6); BUILTIN names
static struct {
    const char *name;
    unsigned int numargs;
    llvmgen_execfunc_fn func;
} lnkfuncs[] = {
    { "ACTUALCOUNT",     0, gen_actualcount },
    { "ACTUALPARAMETER", 1, gen_actualparameter },
    { "ARGPTR",          0, gen_argptr },
};

/*
 * gen_execfunc
 *
 * Dispatcher for the code generation functions.
 */
static LLVMValueRef
gen_execfunc (gencodectx_t gctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    funcdef_t *fd = name_extraspace(expr_func_name(exp));
    llvmgen_execfunc_fn realfnptr = (llvmgen_execfunc_fn)(fd->generator);

    if (realfnptr == 0) {
        unsigned int bpval = machine_scalar_bits(gctx->mach);
        LLVMTypeRef type = (neededtype == 0 ?
                            LLVMIntTypeInContext(gctx->llvmctx, bpval) : neededtype);
        expr_signal(gctx->ectx, STC__INTCMPERR, "gen_execfunc");
        return LLVMConstNull(type);
    }
    return (*realfnptr)(gctx, fd->genfnctx, exp, neededtype);

} /* gen_execfunc */

/*
 * llvmgen_execfuncgen_init
 *
 * Hooks the generators to the executable functions and
 * into the main expression-generation dispatcher, and sets
 * up the internal dispatcher for each function we support.
 *
 * The initialization call for setting up the generators
 * for the machine-specific executable functions (typically
 * BUILTINs) is also invoked here.
 */
void
llvmgen_execfuncgen_init (gencodectx_t gctx)
{
    scopectx_t kwdscope = parser_kwdscope(expr_parse_ctx(gctx->ectx));
    int addr_signed = machine_addr_signed(gctx->mach);
    unsigned int i;

    for (i = 0; i < sizeof(gentable)/sizeof(gentable[0]); i++) {
        funcdef_t *fd;
        name_t *np;
        np = name_search_typed(kwdscope, gentable[i].name, strlen(gentable[i].name),
                               LEXTYPE_NAME_FUNCTION, &fd);
        if (np == 0) {
            expr_signal(gctx->ectx, STC__INTCMPERR, "llvmgen_execfuncgen_init");
            continue;
        }
        fd->generator = (execfuncgenerator_fn) gentable[i].func;
        if (i == 0) {
            fd->genfnctx = (void *)(intptr_t)(addr_signed ? LLVMIntSGT : LLVMIntUGT);
        } else if (i == 1) {
            fd->genfnctx = (void *)(intptr_t)(addr_signed ? LLVMIntSLT : LLVMIntULT);
        } else {
            fd->genfnctx = gentable[i].fctx;
        }
    }
    for (i = 0; i < sizeof(lnkfuncs)/sizeof(lnkfuncs[0]); i++) {
        funcdef_t fdef;
        memset(&fdef, 0, sizeof(fdef));
        strcpy(fdef.name, lnkfuncs[i].name);
        fdef.namelen = strlen(lnkfuncs[i].name);
        fdef.numargs = lnkfuncs[i].numargs;
        fdef.flags = FUNC_M_BUILTIN;
        fdef.generator = (execfuncgenerator_fn) lnkfuncs[i].func;
        if (execfunc_define(kwdscope, &fdef, 0) == 0) {
            expr_signal(gctx->ectx, STC__INTCMPERR, "llvmgen_execfuncgen_init");
        }
    }
    for (i = 0; i < sizeof(fltops)/sizeof(fltops[0]); i++) {
        funcdef_t fdef;
        memset(&fdef, 0, sizeof(fdef));
        strcpy(fdef.name, fltops[i].name);
        fdef.namelen = strlen(fltops[i].name);
        fdef.numargs = (strchr("+-*/", fltops[i].op) != 0 ? 3 : 2);
        fdef.flags = FUNC_M_BUILTIN | (FLT_HASVALUE(&fltops[i]) ? 0 : FUNC_M_NOVALUE);
        fdef.generator = (execfuncgenerator_fn) gen_float;
        fdef.genfnctx = &fltops[i];
        if (execfunc_define(kwdscope, &fdef, 0) == 0) {
            expr_signal(gctx->ectx, STC__INTCMPERR, "llvmgen_execfuncgen_init");
        }
    }
    for (i = 0; i < sizeof(chffuncs)/sizeof(chffuncs[0]); i++) {
        funcdef_t fdef;
        memset(&fdef, 0, sizeof(fdef));
        strcpy(fdef.name, chffuncs[i].name);
        fdef.namelen = strlen(chffuncs[i].name);
        fdef.numargs = chffuncs[i].numargs;
        fdef.flags = chffuncs[i].flags;
        fdef.generator = (execfuncgenerator_fn) chffuncs[i].func;
        fdef.genfnctx = chffuncs[i].fctx;
        if (execfunc_define(kwdscope, &fdef, 0) == 0) {
            expr_signal(gctx->ectx, STC__INTCMPERR, "llvmgen_execfuncgen_init");
        }
    }
    gctx->builtinsctx = llvmgen_builtins_init(gctx, kwdscope);
    llvmgen_expgen_register(gctx, EXPTYPE_EXECFUN, gen_execfunc);

} /* llvmgen_execfuncgen_init */

/*
 * gen_SIGN
 *
 * Generates code for the SIGN standard function.
 */
static LLVMValueRef
gen_SIGN (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMBasicBlockRef exitblk = llvmgen_exitblock_create(gctx, 0);
    LLVMBasicBlockRef posblk = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    LLVMBasicBlockRef negblk = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    llvm_btrack_t *bt = llvmgen_btrack_create(gctx, exitblk);
    unsigned int bpval = machine_scalar_bits(gctx->mach);
    exprseq_t *args = expr_func_arglist(exp);
    LLVMBuilderRef builder = gctx->curfn->builder;
    LLVMValueRef val, test;

    if (neededtype == 0) neededtype = LLVMIntTypeInContext(gctx->llvmctx, bpval);
    val = llvmgen_expression(gctx, exprseq_head(args), 0);
    test = LLVMBuildICmp(builder, LLVMIntEQ, val, LLVMConstNull(LLVMTypeOf(val)), llvmgen_temp(gctx));
    LLVMBuildCondBr(builder, test, exitblk, posblk);
    llvmgen_btrack_update_phi(gctx, bt, 0, LLVMConstNull(neededtype));
    llvmgen_btrack_update_brcount(gctx, bt);
    LLVMPositionBuilderAtEnd(builder, posblk);
    test = LLVMBuildICmp(builder, LLVMIntSLT, val, LLVMConstNull(LLVMTypeOf(val)), llvmgen_temp(gctx));
    LLVMBuildCondBr(builder, test, negblk, exitblk);
    llvmgen_btrack_update_phi(gctx, bt, 0, LLVMConstInt(neededtype, 1, 0));
    llvmgen_btrack_update_brcount(gctx, bt);
    LLVMPositionBuilderAtEnd(builder, negblk);
    llvmgen_btrack_update(gctx, bt, LLVMConstAllOnes(neededtype));

    return llvmgen_btrack_finalize(gctx, bt, neededtype);

} /* gen_SIGN */

/*
 * gen_ABS
 *
 * Generates code for the ABS standard function.
 */
static LLVMValueRef
gen_ABS (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMBasicBlockRef exitblk = llvmgen_exitblock_create(gctx, 0);
    LLVMBasicBlockRef negblk = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    llvm_btrack_t *bt = llvmgen_btrack_create(gctx, exitblk);
    exprseq_t *args = expr_func_arglist(exp);
    LLVMBuilderRef builder = gctx->curfn->builder;
    LLVMValueRef val, test, result;

    val = llvmgen_expression(gctx, exprseq_head(args), 0);
    test = LLVMBuildICmp(builder, LLVMIntSLT, val, LLVMConstNull(LLVMTypeOf(val)), llvmgen_temp(gctx));
    LLVMBuildCondBr(builder, test, negblk, exitblk);
    llvmgen_btrack_update_phi(gctx, bt, 0, val);
    llvmgen_btrack_update_brcount(gctx, bt);
    LLVMPositionBuilderAtEnd(builder, negblk);
    val = LLVMBuildSub(builder, LLVMConstNull(LLVMTypeOf(val)), val, llvmgen_temp(gctx));
    llvmgen_btrack_update(gctx, bt, val);

    result = llvmgen_btrack_finalize(gctx, bt, LLVMTypeOf(val));

    return llvmgen_adjustval(gctx, result, neededtype, 0);

} /* gen_ABS */

/*
 * gen_MINMAX
 *
 * Generates code for the MIN and MAX families
 * of standard functions.
 */
static LLVMValueRef
gen_MINMAX (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    unsigned int bpval = machine_scalar_bits(gctx->mach);
    exprseq_t *args = expr_func_arglist(exp);
    LLVMIntPredicate pred = (LLVMIntPredicate) (uintptr_t) ctx;
    LLVMBuilderRef builder = gctx->curfn->builder;
    LLVMTypeRef inttype = LLVMIntTypeInContext(gctx->llvmctx, bpval);
    LLVMValueRef val, cmpval, test;
    expr_node_t *arg;

    arg = exprseq_head(args);
    val = llvmgen_expression(gctx, arg, inttype);
    for (arg = arg->tq_next; arg != 0; arg = arg->tq_next) {
        cmpval = llvmgen_expression(gctx, arg, inttype);
        test = LLVMBuildICmp(builder, pred, val, cmpval, llvmgen_temp(gctx));
        val = LLVMBuildSelect(builder, test, val, cmpval, llvmgen_temp(gctx));
    }

    return llvmgen_adjustval(gctx, val, neededtype,
                             (pred == LLVMIntSLT || pred == LLVMIntSGT));

} /* gen_MINMAX */

/*
 * gen_ch_allocation
 *
 * Generates CH$ALLOCATION code.
 * Note that we only support one character size here -- byte -- so we
 * rely on the frontend validating the character size parameter, if it was
 * specified.
 */
static LLVMValueRef
gen_ch_allocation (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMBuilderRef builder = gctx->curfn->builder;
    exprseq_t *args = expr_func_arglist(exp);
    LLVMValueRef len, result;
    expr_node_t *arg;

    arg = exprseq_head(args);
    len = llvmgen_expression(gctx, arg, gctx->fullwordtype);
    result = LLVMBuildMul(builder, len, LLVMConstInt(gctx->fullwordtype, 8, 0), llvmgen_temp(gctx));
    result = LLVMBuildAdd(builder, result,
                          LLVMConstInt(gctx->fullwordtype, machine_scalar_bits(gctx->mach)-1, 0),
                          llvmgen_temp(gctx));
    result = LLVMBuildUDiv(builder, result,
                           LLVMConstInt(gctx->fullwordtype, machine_scalar_bits(gctx->mach), 0),
                           llvmgen_temp(gctx));
    return llvmgen_adjustval(gctx, result, neededtype, 0);

} /* gen_ch_allocation */

/*
 * gen_ch_pointer
 *
 * Generates CH$PTR code.  Also handles CH$PLUS, since we don't
 * bother checking the third argument to CH$PTR.
 */
static LLVMValueRef
gen_ch_pointer (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMBuilderRef builder = gctx->curfn->builder;
    exprseq_t *args = expr_func_arglist(exp);
    LLVMValueRef result;
    expr_node_t *arg;

    arg = exprseq_head(args);
    result = llvmgen_expression(gctx, arg, gctx->unitptrtype);
    if (arg->tq_next != 0) {
        LLVMValueRef offset;
        arg = arg->tq_next;
        if (expr_type(arg) == EXPTYPE_PRIM_LIT) {
            if (expr_litval(arg) != 0) {
                offset = LLVMConstInt(gctx->fullwordtype, (unsigned long long int) expr_litval(arg), 0);
                if (LLVMIsConstant(result)) {
                    result = LLVMConstGEP2(gctx->unittype, result, &offset, 1);
                } else {
                    result = LLVMBuildGEP2(builder, gctx->unittype, result, &offset, 1, llvmgen_temp(gctx));
                }
            }
        } else {
            offset = llvmgen_expression(gctx, arg, gctx->fullwordtype);
            result = LLVMBuildPtrToInt(builder, result, gctx->fullwordtype, llvmgen_temp(gctx));
            result= LLVMBuildAdd(builder, result, offset, llvmgen_temp(gctx));
        }

    }

    return llvmgen_adjustval(gctx, result, neededtype, 0);

} /* gen_ch_pointer */

/*
 * gen_ch_rchar
 *
 * Generates CH$RCHAR.
 */
static LLVMValueRef
gen_ch_rchar (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMBuilderRef builder = gctx->curfn->builder;
    exprseq_t *args = expr_func_arglist(exp);
    LLVMValueRef addr, result;
    expr_node_t *arg;

    arg = exprseq_head(args);
    addr = llvmgen_expression(gctx, arg, gctx->unitptrtype);
    result = LLVMBuildLoad2(builder, gctx->unittype, addr, llvmgen_temp(gctx));

    return llvmgen_adjustval(gctx, result, neededtype, 0);

} /* gen_ch_rchar */

/*
 * gen_ch_rchar_a
 *
 * Generates CH$RCHAR_A and CH$A_RCHAR.
 */
static LLVMValueRef
gen_ch_rchar_a (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMBuilderRef builder = gctx->curfn->builder;
    exprseq_t *args = expr_func_arglist(exp);
    int postincrement = (int)(intptr_t) ctx;
    LLVMValueRef one = LLVMConstInt(gctx->fullwordtype, 1, 0);
    LLVMValueRef addraddr, addr, result;
    expr_node_t *arg;

    arg = exprseq_head(args);
    addraddr = llvmgen_expression(gctx, arg, gctx->unitptrtype);
    addr = LLVMBuildLoad2(builder, gctx->unitptrtype, addraddr, llvmgen_temp(gctx));
    if (!postincrement) {
        LLVMValueRef tmp = LLVMBuildGEP2(builder, gctx->unittype, addr, &one, 1, llvmgen_temp(gctx));
        LLVMBuildStore(builder, tmp, addraddr);
        addr = tmp;
    }
    result = LLVMBuildLoad2(builder, gctx->unittype, addr, llvmgen_temp(gctx));
    if (postincrement) {
        LLVMValueRef tmp = LLVMBuildGEP2(builder, gctx->unittype, addr, &one, 1, llvmgen_temp(gctx));
        LLVMBuildStore(builder, tmp, addraddr);
    }

    return llvmgen_adjustval(gctx, result, neededtype, 0);

} /* gen_ch_rchar_a */

/*
 * gen_ch_wchar
 *
 * Generates CH$WCHAR.
 */
static LLVMValueRef
gen_ch_wchar (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMBuilderRef builder = gctx->curfn->builder;
    exprseq_t *args = expr_func_arglist(exp);
    LLVMValueRef ch, addr;
    expr_node_t *arg;

    arg = exprseq_head(args);
    ch = llvmgen_expression(gctx, arg, LLVMInt8TypeInContext(gctx->llvmctx));
    arg = arg->tq_next;
    addr = llvmgen_expression(gctx, arg, gctx->unitptrtype);
    LLVMBuildStore(builder, ch, addr);

    if (neededtype != 0) {
        return LLVMConstNull(neededtype);
    }

    return 0;

} /* gen_ch_wchar */

/*
 * gen_ch_wchar_a
 *
 * Generates CH$WCHAR_A and CH$A_WCHAR.
 */
static LLVMValueRef
gen_ch_wchar_a (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMBuilderRef builder = gctx->curfn->builder;
    exprseq_t *args = expr_func_arglist(exp);
    int postincrement = (int)(intptr_t) ctx;
    LLVMValueRef one = LLVMConstInt(gctx->fullwordtype, 1, 0);
    LLVMValueRef ch, addraddr, addr;
    expr_node_t *arg;

    arg = exprseq_head(args);
    ch = llvmgen_expression(gctx, arg, LLVMInt8TypeInContext(gctx->llvmctx));
    arg = arg->tq_next;
    addraddr = llvmgen_expression(gctx, arg, gctx->unitptrtype);
    addr = LLVMBuildLoad2(builder, gctx->unitptrtype, addraddr, llvmgen_temp(gctx));
    if (!postincrement) {
        LLVMValueRef tmp = LLVMBuildGEP2(builder, gctx->unittype, addr, &one, 1, llvmgen_temp(gctx));
        LLVMBuildStore(builder, tmp, addraddr);
        addr = tmp;
    }
    LLVMBuildStore(builder, ch, addr);
    if (postincrement) {
        LLVMValueRef tmp = LLVMBuildGEP2(builder, gctx->unittype, addr, &one, 1, llvmgen_temp(gctx));
        LLVMBuildStore(builder, tmp, addraddr);
    }

    if (neededtype != 0) {
        return LLVMConstNull(neededtype);
    }

    return 0;

} /* gen_ch_wchar_a */

/*
 * gen_ch_move
 *
 * CH$MOVE(len, src, dst)
 * Returns: dst + len
 *
 * For x86, the MOVSB builtin provides this function.
 */
static LLVMValueRef
gen_ch_move (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{

    return llvmgen_builtinfunc(gctx, "MOVSB", exp, neededtype);

} /* gen_ch_move */

/*
 * gen_ch_fill
 *
 * CH$FILL(chr, len, dst)
 * Returns: dst + len
 *
 * For x86, the STOSB builtin provides this function.
 */
static LLVMValueRef
gen_ch_fill (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    return llvmgen_builtinfunc(gctx, "STOSB", exp, neededtype);

} /* gen_ch_fill */

/*
 * gen_ch_compare
 *
 * CH${EQL,NEQ,LSS,GTR,LEQ,GEQ,COMPARE}(len1, ptr1, len2, ptr2, padchr)
 * Returns:
 * for CH${EQL,NEQ,LSS,GTR,LEQ,GEQ}, 1 if true, 0 if false
 * for CH$COMPARE: -1 if less, 0 if equal, 1 if greater
 *
 * On x86, uses CMPSB to compare the string lengths and a form of
 * SCASB to compare against the padding character when the lengths
 * are not the same.
 */
static LLVMValueRef
gen_ch_compare (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMBasicBlockRef exitblk = llvmgen_exitblock_create(gctx, 0);
    LLVMBasicBlockRef eqchk = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    LLVMBasicBlockRef lenchk = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    LLVMBasicBlockRef str2longer = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    LLVMBasicBlockRef str1longer = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    llvm_btrack_t *bt = llvmgen_btrack_create(gctx, exitblk);
    exprseq_t *args = expr_func_arglist(exp);
    LLVMBuilderRef builder = gctx->curfn->builder;
    int do_general_compare = ((intptr_t) ctx == -1);
    expr_node_t *arg;
    LLVMIntPredicate pred;
    LLVMValueRef argvals[3], t0, t1, bound;
    LLVMValueRef len1, len2, ptr1, ptr2, fill, result, v;

    arg = exprseq_head(args);
    len1 = llvmgen_expression(gctx, arg, gctx->fullwordtype);
    arg = arg->tq_next;
    argvals[1] = ptr1 = llvmgen_expression(gctx, arg, gctx->unitptrtype);
    arg = arg->tq_next;
    len2 = llvmgen_expression(gctx, arg, gctx->fullwordtype);
    arg = arg->tq_next;
    argvals[2] = ptr2 = llvmgen_expression(gctx, arg, gctx->unitptrtype);
    arg = arg->tq_next;
    // Fill character is optional, defaults to 0
    fill = (arg == 0 ? LLVMConstNull(LLVMInt8TypeInContext(gctx->llvmctx))
            : llvmgen_expression(gctx, arg, LLVMInt8TypeInContext(gctx->llvmctx)));
    t0 = LLVMBuildICmp(builder, LLVMIntULE, len1, len2, llvmgen_temp(gctx));
    t1 = LLVMBuildICmp(builder, LLVMIntEQ, len1, len2, llvmgen_temp(gctx));
    argvals[0] = bound = LLVMBuildSelect(builder, t0, len1, len2, llvmgen_temp(gctx));
    result = llvmgen_asminstr(gctx, "CMPSB", argvals, 3);
    v = LLVMBuildSwitch(builder, result, exitblk, 1);
    LLVMAddCase(v, LLVMConstNull(gctx->fullwordtype), eqchk);
    llvmgen_btrack_update_phi(gctx, bt, LLVMGetInsertBlock(builder), result);
    llvmgen_btrack_update_brcount(gctx, bt);

    LLVMPositionBuilderAtEnd(builder, eqchk);
    LLVMBuildCondBr(builder, t1, exitblk, lenchk);
    llvmgen_btrack_update_phi(gctx, bt, LLVMGetInsertBlock(builder), result);
    llvmgen_btrack_update_brcount(gctx, bt);

    LLVMPositionBuilderAtEnd(builder, lenchk);
    LLVMBuildCondBr(builder, t0, str2longer, str1longer);

    LLVMPositionBuilderAtEnd(builder, str2longer);
    argvals[0] = LLVMBuildSub(builder, len2, bound, llvmgen_temp(gctx));
    argvals[1] = LLVMBuildGEP2(builder, gctx->unittype, ptr2, &bound, 1, llvmgen_temp(gctx));
    argvals[2] = fill;
    result = llvmgen_asminstr(gctx, "SCASB_CMP", argvals, 3);
    llvmgen_btrack_update(gctx, bt, result);

    LLVMPositionBuilderAtEnd(builder, str1longer);
    argvals[0] = LLVMBuildSub(builder, len1, bound, llvmgen_temp(gctx));
    argvals[1] = LLVMBuildGEP2(builder, gctx->unittype, ptr1, &bound, 1, llvmgen_temp(gctx));
    argvals[2] = fill;
    result = llvmgen_asminstr(gctx, "SCASB_CMP", argvals, 3);
    // SCASB computed fill - str1, i.e. str2 - str1: flip the sign
    result = LLVMBuildNeg(builder, result, llvmgen_temp(gctx));
    llvmgen_btrack_update(gctx, bt, result);

    result = llvmgen_btrack_finalize(gctx, bt, gctx->fullwordtype);
    if (!do_general_compare) {
        pred = (LLVMIntPredicate)(intptr_t)ctx;
        result = LLVMBuildICmp(builder, pred, result, LLVMConstNull(gctx->fullwordtype), llvmgen_temp(gctx));
    }

    return llvmgen_adjustval(gctx, result, neededtype, 0);

} /* gen_ch_compare */

/*
 * gen_ch_find
 *
 * CH$FIND_CH(len, ptr, char), CH$FIND_NOT_CH(len, ptr, char)
 * Returns: pointer to matching (FIND) or not-matching (FIND_NOT) character,
 * or null pointer on failure.
 *
 * On x86, uses SCASB.
 */
static LLVMValueRef
gen_ch_find (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{

    return llvmgen_builtinfunc(gctx, (ctx == 0 ? "SCASB_REPNE" : "SCASB_REPE"), exp, neededtype);


} /* gen_ch_find */

/*
 * gen_ch_fail
 *
 * CH$FAIL(ptr)
 * Returns: 1 if pointer is null, 0 if non-null
 */
static LLVMValueRef
gen_ch_fail (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMBuilderRef builder = gctx->curfn->builder;
    exprseq_t *args = expr_func_arglist(exp);
    LLVMValueRef result, arg;

    arg = llvmgen_expression(gctx, exprseq_head(args), gctx->fullwordtype);
    result = LLVMBuildIsNull(builder, arg, llvmgen_temp(gctx));

    return llvmgen_adjustval(gctx, result, neededtype, 0);

} /* gen_ch_fail */

/*
 * gen_ch_findsub
 *
 * CH$FIND_SUB(strlen, strptr, patlen, patptr)
 * Returns: pointer to start of match, or null pointer on failure
 *
 * Generates code for CH$FIND_SUB.  The algorithm is:
 * 1. If the pattern length is zero, return the start of the string.
 * 2. While the string length >= pattern length:
 * 2a. Scan for the first character of the pattern in the string.
 * 2b. If not found, return failure.
 * 2c. Calculate the remaining length from the start point.
 * 2d. If the remaining length < pattern length, return failure.
 * 2e. Do the comparison with the pattern from that start point.
 * 2f. If a match, return the found start point.
 * 2g. Set the current string pointer to one past the start point.
 * 2h. Set the current string length to (remaining length)-1.
 * 2i. Iterate.
 * 3. If the loop exits, return failure.
 *
 * XXX This is a fair amount of code.  Might make sense to shift this
 * to a runtime support routine instead of inlining.
 */
static LLVMValueRef
gen_ch_findsub (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMBasicBlockRef exitblk = llvmgen_exitblock_create(gctx, 0);
    LLVMBasicBlockRef loopblk = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    LLVMBasicBlockRef findblk = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    LLVMBasicBlockRef lenchkblk = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    LLVMBasicBlockRef cmpblk = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    LLVMBasicBlockRef updblk = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    llvm_btrack_t *bt = llvmgen_btrack_create(gctx, exitblk);
    exprseq_t *args = expr_func_arglist(exp);
    LLVMBuilderRef builder = gctx->curfn->builder;
    LLVMValueRef one = LLVMConstInt(gctx->fullwordtype, 1, 1);
    expr_node_t *arg;
    LLVMBasicBlockRef curblk;
    LLVMValueRef argvals[3], test, curptr, curlen, remain;
    LLVMValueRef slen, sptr, safter, plen, pptr, result, v, pfirst;

    arg = exprseq_head(args);
    slen = llvmgen_expression(gctx, arg, gctx->fullwordtype);
    arg = arg->tq_next;
    sptr = llvmgen_expression(gctx, arg, gctx->unitptrtype);
    safter = LLVMBuildAdd(builder, llvmgen_adjustval(gctx, sptr, gctx->fullwordtype, 1),
                          slen, llvmgen_temp(gctx));
    arg = arg->tq_next;
    plen = llvmgen_expression(gctx, arg, gctx->fullwordtype);
    arg = arg->tq_next;
    pptr = llvmgen_expression(gctx, arg, gctx->unitptrtype);
    pfirst = LLVMBuildLoad2(builder, gctx->unittype, pptr, llvmgen_temp(gctx));

    test = LLVMBuildIsNull(builder, plen, llvmgen_temp(gctx));
    curblk = LLVMGetInsertBlock(builder);
    LLVMBuildCondBr(builder, test, exitblk, loopblk);
    llvmgen_btrack_update_phi(gctx, bt, 0, sptr);
    llvmgen_btrack_update_brcount(gctx, bt);

    LLVMPositionBuilderAtEnd(builder, loopblk);
    curptr = LLVMBuildPhi(builder, gctx->unitptrtype, llvmgen_temp(gctx));
    LLVMAddIncoming(curptr, &sptr, &curblk, 1);
    curlen = LLVMBuildPhi(builder, gctx->fullwordtype, llvmgen_temp(gctx));
    LLVMAddIncoming(curlen, &slen, &curblk, 1);
    test = LLVMBuildICmp(builder, LLVMIntSGT, curlen, plen, llvmgen_temp(gctx));
    LLVMBuildCondBr(builder, test, findblk, exitblk);
    llvmgen_btrack_update_phi(gctx, bt, 0, LLVMConstNull(gctx->unitptrtype));
    llvmgen_btrack_update_brcount(gctx, bt);

    LLVMPositionBuilderAtEnd(builder, findblk);
    argvals[0] = curlen;
    argvals[1] = curptr;
    argvals[2] = pfirst;
    result = llvmgen_asminstr(gctx, "SCASB_REPNE", argvals, 3);
    test = LLVMBuildIsNull(builder, result, llvmgen_temp(gctx));
    LLVMBuildCondBr(builder, test, exitblk, lenchkblk);
    llvmgen_btrack_update_phi(gctx, bt, 0, result);
    llvmgen_btrack_update_brcount(gctx, bt);

    LLVMPositionBuilderAtEnd(builder, lenchkblk);
    remain = LLVMBuildSub(builder, safter,
                          llvmgen_adjustval(gctx, result, gctx->fullwordtype, 1),
                          llvmgen_temp(gctx));
    test = LLVMBuildICmp(builder, LLVMIntSLT, remain, plen, llvmgen_temp(gctx));
    LLVMBuildCondBr(builder, test, exitblk, cmpblk);
    llvmgen_btrack_update_phi(gctx, bt, 0, LLVMConstNull(gctx->unitptrtype));
    llvmgen_btrack_update_brcount(gctx, bt);

    LLVMPositionBuilderAtEnd(builder, cmpblk);
    argvals[0] = plen;
    argvals[1] = pptr;
    argvals[2] = result;
    v = llvmgen_asminstr(gctx, "CMPSB", argvals, 3);
    test = LLVMBuildIsNull(builder, v, llvmgen_temp(gctx));
    LLVMBuildCondBr(builder, test, exitblk, updblk);
    llvmgen_btrack_update_phi(gctx, bt, 0, result);
    llvmgen_btrack_update_brcount(gctx, bt);

    LLVMPositionBuilderAtEnd(builder, updblk);
    v = LLVMBuildGEP2(builder, gctx->unittype, result, &one, 1, llvmgen_temp(gctx));
    LLVMAddIncoming(curptr, &v, &updblk, 1);
    v = LLVMBuildSub(builder, remain, one, llvmgen_temp(gctx));
    LLVMAddIncoming(curlen, &v, &updblk, 1);
    LLVMBuildBr(builder, loopblk);

    result = llvmgen_btrack_finalize(gctx, bt, gctx->unitptrtype);
    return llvmgen_adjustval(gctx, result, neededtype, 0);
    
} /* gen_ch_findsub */

/*
 * gen_ch_copy
 *
 * CH$COPY(src1len, src1ptr, ..., fill, dstlen, dstptr)
 * Returns: (dstptr+dstlen)
 *
 * Generates code for CH$COPY.  Algorithm:
 * 1. Validate the argument count - the front end only checks for
 * an argument count >= 5
 * 2. Set curlen = dstlen, curptr = dstptr
 * 3. Iterate through the source argument pairs (slen, sptr) while curlen > 0:
 * 3a. If slen > curlen, slen = curlen
 * 3b. curptr = CH$MOVE(slen, sptr, dptr)
 * 3c. curptr = CH$PLUS(curptr, slen), curlen -= slen
 * 3d. Iterate.
 * 4. If curlen > 0 (ran out of source arguments): CH$FILL(curlen,curptr,fill)
 *
 * XXX This is a fair amount of code.  Might make sense to shift this
 * to a runtime support routine instead of inlining.
 */
static LLVMValueRef
gen_ch_copy (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMBasicBlockRef exitblk = llvmgen_exitblock_create(gctx, 0);
    LLVMBasicBlockRef fillblk = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    llvm_btrack_t *bt = llvmgen_btrack_create(gctx, exitblk);
    exprseq_t *args = expr_func_arglist(exp);
    unsigned int argcnt = exprseq_length(args);
    LLVMBuilderRef builder = gctx->curfn->builder;
    expr_node_t *arg;
    LLVMValueRef argvals[3], test, dstptr, dstlen;
    LLVMValueRef srclen, result, fill;
    unsigned int i;

    // Must be an odd number of arguments (the minimum of 5 check is
    // performed in the front end)
    if ((argcnt % 2) != 1) {
        log_signal(parser_logctx(expr_parse_ctx(gctx->ectx)), expr_textpos(exp),
                   STC__INSFUNARG, name_string(expr_func_name(exp)));
        return (neededtype == 0 ? 0 : LLVMConstNull(neededtype));
    }
    // Get to the 'fill', 'dlen', and 'dptr' args, which are the last 3
    for (arg = exprseq_head(args), i = 0; i < argcnt-3; arg = arg->tq_next, i++);
    fill = llvmgen_expression(gctx, arg, LLVMInt8TypeInContext(gctx->llvmctx));
    arg = arg->tq_next;
    dstlen = llvmgen_expression(gctx, arg, gctx->fullwordtype);
    arg = arg->tq_next;
    dstptr = llvmgen_expression(gctx, arg, gctx->unitptrtype);

    for (arg = exprseq_head(args), i = 0; i < argcnt-3;
         arg = arg->tq_next->tq_next, i += 2) {
        LLVMBasicBlockRef domove = LLVMInsertBasicBlockInContext(gctx->llvmctx, fillblk, llvmgen_label(gctx));
        test = LLVMBuildIsNull(builder, dstlen, llvmgen_temp(gctx));
        LLVMBuildCondBr(builder, test, exitblk, domove);
        llvmgen_btrack_update_phi(gctx, bt, 0, dstptr);
        llvmgen_btrack_update_brcount(gctx, bt);

        LLVMPositionBuilderAtEnd(builder, domove);
        srclen = llvmgen_expression(gctx, arg, gctx->fullwordtype);
        argvals[1] = llvmgen_expression(gctx, arg->tq_next, gctx->unitptrtype);
        test = LLVMBuildICmp(builder, LLVMIntSLT, dstlen, srclen, llvmgen_temp(gctx));
        argvals[0] = LLVMBuildSelect(builder, test, dstlen, srclen, llvmgen_temp(gctx));
        argvals[2] = dstptr;
        dstptr = llvmgen_asminstr(gctx, "MOVSB", argvals, 3);
        dstlen = LLVMBuildSub(builder, dstlen, argvals[0], llvmgen_temp(gctx));
    }

    test = LLVMBuildIsNull(builder, dstlen, llvmgen_temp(gctx));
    LLVMBuildCondBr(builder, test, exitblk, fillblk);
    llvmgen_btrack_update_phi(gctx, bt, 0, dstptr);
    llvmgen_btrack_update_brcount(gctx, bt);

    LLVMPositionBuilderAtEnd(builder, fillblk);
    argvals[0] = fill;
    argvals[1] = dstlen;
    argvals[2] = dstptr;
    dstptr = llvmgen_asminstr(gctx, "STOSB", argvals, 3);
    llvmgen_btrack_update(gctx, bt, dstptr);

    result = llvmgen_btrack_finalize(gctx, bt, gctx->unitptrtype);
    return llvmgen_adjustval(gctx, result, neededtype, 0);
    
} /* gen_ch_copy */

/*
 * gen_ch_translate
 *
 * CH$TRANSLATE(table, slen, sptr, fill, dlen, dptr)
 *
 * Generates code for CH$TRANSLATE, walking through the
 * source string and applying the translation one character
 * at a time.
 */
static LLVMValueRef
gen_ch_translate (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMBasicBlockRef exitblk = llvmgen_exitblock_create(gctx, 0);
    LLVMBasicBlockRef loopblk = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    LLVMBasicBlockRef loopbody = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    LLVMBasicBlockRef postloop = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    LLVMBasicBlockRef fillblk = LLVMInsertBasicBlockInContext(gctx->llvmctx, exitblk, llvmgen_label(gctx));
    llvm_btrack_t *bt = llvmgen_btrack_create(gctx, exitblk);
    exprseq_t *args = expr_func_arglist(exp);
    LLVMBuilderRef builder = gctx->curfn->builder;
    LLVMValueRef zero = LLVMConstNull(gctx->fullwordtype);
    LLVMValueRef one  = LLVMConstInt(gctx->fullwordtype, 1, 0);
    expr_node_t *arg;
    LLVMBasicBlockRef curblk;
    LLVMValueRef argvals[3], test, loopcount, loopphi, offphi, chr;
    LLVMValueRef transtab, slen, sptr, dlen, dptr, fill, result, v;

    arg = exprseq_head(args);
    transtab = llvmgen_expression(gctx, arg, gctx->unitptrtype);
    arg = arg->tq_next;
    slen = llvmgen_expression(gctx, arg, gctx->fullwordtype);
    arg = arg->tq_next;
    sptr = llvmgen_expression(gctx, arg, gctx->unitptrtype);
    arg = arg->tq_next;
    fill = llvmgen_expression(gctx, arg, LLVMInt8TypeInContext(gctx->llvmctx));
    arg = arg->tq_next;
    dlen = llvmgen_expression(gctx, arg, gctx->fullwordtype);
    arg = arg->tq_next;
    dptr = llvmgen_expression(gctx, arg, gctx->unitptrtype);
    test = LLVMBuildICmp(builder, LLVMIntULE, slen, dlen, llvmgen_temp(gctx));
    loopcount = LLVMBuildSelect(builder, test, slen, dlen, llvmgen_temp(gctx));
    curblk = LLVMGetInsertBlock(builder);
    LLVMBuildBr(builder, loopblk);

    LLVMPositionBuilderAtEnd(builder, loopblk);
    loopphi = LLVMBuildPhi(builder, gctx->fullwordtype, llvmgen_temp(gctx));
    LLVMAddIncoming(loopphi, &loopcount, &curblk, 1);
    offphi = LLVMBuildPhi(builder, gctx->fullwordtype, llvmgen_temp(gctx));
    LLVMAddIncoming(offphi, &zero, &curblk, 1);
    test = LLVMBuildIsNull(builder, loopphi, llvmgen_temp(gctx));
    LLVMBuildCondBr(builder, test, postloop, loopbody);

    LLVMPositionBuilderAtEnd(builder, loopbody);
    sptr = LLVMBuildGEP2(builder, gctx->unittype, sptr, &offphi, 1, llvmgen_temp(gctx));
    chr = LLVMBuildLoad2(builder, gctx->unittype, sptr, llvmgen_temp(gctx));
    v = LLVMBuildGEP2(builder, gctx->unittype, transtab, &chr, 1, llvmgen_temp(gctx));
    chr = LLVMBuildLoad2(builder, gctx->unittype, v, llvmgen_temp(gctx));
    v = LLVMBuildGEP2(builder, gctx->unittype, dptr, &offphi, 1, llvmgen_temp(gctx));
    LLVMBuildStore(builder, chr, v);
    v = LLVMBuildSub(builder, loopphi, one, llvmgen_temp(gctx));
    LLVMAddIncoming(loopphi, &v, &loopbody, 1);
    v = LLVMBuildAdd(builder, offphi, one, llvmgen_temp(gctx));
    LLVMAddIncoming(offphi, &v, &loopbody, 1);
    LLVMBuildBr(builder, loopblk);

    LLVMPositionBuilderAtEnd(builder, postloop);
    dptr = LLVMBuildGEP2(builder, gctx->unittype, dptr, &loopcount, 1, llvmgen_temp(gctx));
    test = LLVMBuildICmp(builder, LLVMIntULT, loopcount, dlen, llvmgen_temp(gctx));
    LLVMBuildCondBr(builder, test, fillblk, exitblk);
    llvmgen_btrack_update_phi(gctx, bt, LLVMGetInsertBlock(builder), dptr);
    llvmgen_btrack_update_brcount(gctx, bt);

    LLVMPositionBuilderAtEnd(builder, fillblk);
    argvals[0] = fill;
    argvals[1] = LLVMBuildSub(builder, dlen, offphi, llvmgen_temp(gctx));
    argvals[2] = dptr;
    dptr = llvmgen_asminstr(gctx, "STOSB", argvals, 3);
    llvmgen_btrack_update(gctx, bt, dptr);

    result = llvmgen_btrack_finalize(gctx, bt, gctx->unitptrtype);
    return llvmgen_adjustval(gctx, result, neededtype, 0);
    
} /* gen_ch_translate */

/*
 * Linkage functions: ACTUALCOUNT, ACTUALPARAMETER, ARGPTR (LRM 13.6).
 *
 * The caller stores the argument count in the thread-local
 * bliss_argcount just before each call; the callee loads it on entry.
 * Arguments beyond the formals are read with va_arg, so a routine that
 * needs them is turned into a varargs function at its end.
 */

#define entry_builder llvmgen_entry_builder

/*
 * call_va_intrinsic
 *
 * Calls llvm.va_start, llvm.va_copy or llvm.va_end.
 */
static void
call_va_intrinsic (gencodectx_t gctx, LLVMBuilderRef b, const char *name,
                   LLVMValueRef *args, unsigned int nargs)
{
    LLVMTypeRef ptr = LLVMPointerTypeInContext(gctx->llvmctx, 0);
    unsigned int id = LLVMLookupIntrinsicID(name, strlen(name));
    size_t ntypes = LLVMIntrinsicIsOverloaded(id) ? 1 : 0;
    LLVMValueRef fn = LLVMGetIntrinsicDeclaration(gctx->module, id, &ptr, ntypes);
    LLVMTypeRef ftype = LLVMIntrinsicGetType(gctx->llvmctx, id, &ptr, ntypes);

    LLVMBuildCall2(b, ftype, fn, args, nargs, "");

} /* call_va_intrinsic */

/*
 * in_routine
 */
static int
in_routine (gencodectx_t gctx, expr_node_t *exp)
{
    if (gctx->curfn == 0) {
        expr_signal(gctx->ectx, STC__LNKFNRTN, name_string(expr_func_name(exp)));
        return 0;
    }
    if (LLVMGetFunctionCallConv(gctx->curfn->func) != LLVMCCallConv) {
        expr_signal(gctx->ectx, STC__LNKFNLNK, name_string(expr_func_name(exp)));
        return 0;
    }
    return 1;

} /* in_routine */

/*
 * argcount_value
 */
static LLVMValueRef
argcount_value (gencodectx_t gctx)
{
    llvm_rtntrack_t *rt = gctx->curfn;

    if (rt->argcount == 0) {
        LLVMBuilderRef b = entry_builder(gctx);
        rt->argcount = LLVMBuildLoad2(b, gctx->fullwordtype,
                                      llvmgen_argcount_global(gctx), "actualcount");
        LLVMDisposeBuilder(b);
    }
    return rt->argcount;

} /* argcount_value */

/*
 * argblock_fill
 *
 * Fills the argument block (count, then each argument) and
 * returns its address.
 */
static LLVMValueRef
argblock_fill (gencodectx_t gctx)
{
    llvm_rtntrack_t *rt = gctx->curfn;
    LLVMBuilderRef bld = rt->builder;
    LLVMTypeRef wt = gctx->fullwordtype;
    LLVMTypeRef blktype = LLVMArrayType(wt, LLVMGEN_K_MAXARGS + 1);
    LLVMValueRef count = argcount_value(gctx);
    LLVMValueRef idx[2], v, n, i, inext, phi, args[2];
    LLVMBasicBlockRef curblk, testblk, bodyblk, doneblk;
    unsigned int k, nformals = LLVMCountParams(rt->func);

    if (rt->argblock == 0) {
        LLVMBuilderRef b = entry_builder(gctx);
        LLVMTypeRef vatype = (machine_scalar_bits(gctx->mach) == 64
                              ? LLVMArrayType(wt, 3)
                              : LLVMPointerTypeInContext(gctx->llvmctx, 0));
        rt->argblock = LLVMBuildAlloca(b, blktype, "argblock");
        rt->vacopy = LLVMBuildAlloca(b, vatype, "vacopy");
        rt->valist = LLVMBuildAlloca(b, vatype, "valist");
        call_va_intrinsic(gctx, b, "llvm.va_start", &rt->valist, 1);
        LLVMDisposeBuilder(b);
        rt->varargs = 1;
    }
    idx[0] = LLVMConstNull(wt);
    idx[1] = LLVMConstNull(wt);
    LLVMBuildStore(bld, count, LLVMBuildGEP2(bld, blktype, rt->argblock, idx, 2, ""));
    for (k = 0; k < nformals && k < LLVMGEN_K_MAXARGS; k++) {
        idx[1] = LLVMConstInt(wt, k + 1, 0);
        v = llvmgen_adjustval(gctx, LLVMGetParam(rt->func, k), wt, 0);
        LLVMBuildStore(bld, v, LLVMBuildGEP2(bld, blktype, rt->argblock, idx, 2, ""));
    }

    // The rest come through va_arg
    args[0] = rt->vacopy;
    args[1] = rt->valist;
    call_va_intrinsic(gctx, bld, "llvm.va_copy", args, 2);
    n = LLVMConstInt(wt, LLVMGEN_K_MAXARGS, 0);
    n = LLVMBuildSelect(bld, LLVMBuildICmp(bld, LLVMIntUGT, count, n, ""), n, count, "");
    curblk = LLVMGetInsertBlock(bld);
    doneblk = llvmgen_exitblock_create(gctx, 0);
    testblk = LLVMInsertBasicBlockInContext(gctx->llvmctx, doneblk, llvmgen_label(gctx));
    bodyblk = LLVMInsertBasicBlockInContext(gctx->llvmctx, doneblk, llvmgen_label(gctx));
    LLVMBuildBr(bld, testblk);
    LLVMPositionBuilderAtEnd(bld, testblk);
    phi = LLVMBuildPhi(bld, wt, "");
    i = LLVMConstInt(wt, nformals, 0);
    LLVMAddIncoming(phi, &i, &curblk, 1);
    LLVMBuildCondBr(bld, LLVMBuildICmp(bld, LLVMIntULT, phi, n, ""), bodyblk, doneblk);
    LLVMPositionBuilderAtEnd(bld, bodyblk);
    v = LLVMBuildVAArg(bld, rt->vacopy, wt, "");
    inext = LLVMBuildAdd(bld, phi, LLVMConstInt(wt, 1, 0), "");
    idx[1] = inext;
    LLVMBuildStore(bld, v, LLVMBuildGEP2(bld, blktype, rt->argblock, idx, 2, ""));
    LLVMBuildBr(bld, testblk);
    LLVMAddIncoming(phi, &inext, &bodyblk, 1);
    LLVMPositionBuilderAtEnd(bld, doneblk);
    call_va_intrinsic(gctx, bld, "llvm.va_end", &rt->vacopy, 1);

    return rt->argblock;

} /* argblock_fill */

/*
 * gen_actualcount
 */
static LLVMValueRef
gen_actualcount (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    if (!in_routine(gctx, exp)) {
        return LLVMConstNull(neededtype == 0 ? gctx->fullwordtype : neededtype);
    }
    return llvmgen_adjustval(gctx, argcount_value(gctx), neededtype, 0);

} /* gen_actualcount */

/*
 * gen_actualparameter
 */
static LLVMValueRef
gen_actualparameter (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    expr_node_t *arg = exprseq_head(expr_func_arglist(exp));
    LLVMTypeRef wt = gctx->fullwordtype;
    LLVMValueRef blk, idx[2];

    if (!in_routine(gctx, exp)) {
        return LLVMConstNull(neededtype == 0 ? wt : neededtype);
    }
    if (expr_type(arg) == EXPTYPE_PRIM_LIT) {
        long i = expr_litval(arg);
        if (i >= 1 && i <= (long) LLVMCountParams(gctx->curfn->func)) {
            return llvmgen_adjustval(gctx, LLVMGetParam(gctx->curfn->func, (unsigned) i - 1),
                                     neededtype, 0);
        }
    }
    idx[0] = LLVMConstNull(wt);
    idx[1] = llvmgen_expression(gctx, arg, wt);
    blk = argblock_fill(gctx);
    blk = LLVMBuildGEP2(gctx->curfn->builder, LLVMArrayType(wt, LLVMGEN_K_MAXARGS + 1),
                        blk, idx, 2, "");
    return llvmgen_adjustval(gctx, LLVMBuildLoad2(gctx->curfn->builder, wt, blk, ""),
                             neededtype, 0);

} /* gen_actualparameter */

/*
 * gen_argptr
 */
static LLVMValueRef
gen_argptr (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    if (!in_routine(gctx, exp)) {
        return LLVMConstNull(neededtype == 0 ? gctx->fullwordtype : neededtype);
    }
    return llvmgen_adjustval(gctx, argblock_fill(gctx), neededtype, 0);

} /* gen_argptr */

/*
 * gen_signal
 *
 * SIGNAL and SIGNAL_STOP (LRM 17.3.2): builds the signal vector
 * (count, condition value, parameters) and calls the run-time
 * library, which searches for handlers.
 */
static LLVMValueRef
gen_signal (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    exprseq_t *args = expr_func_arglist(exp);
    LLVMTypeRef wt = gctx->fullwordtype;
    unsigned int n = exprseq_length(args), i;
    LLVMTypeRef vtype = LLVMArrayType(wt, n + 1);
    LLVMValueRef sig, idx[2], v, cargs[2];
    LLVMBuilderRef b;
    expr_node_t *arg;

    if (gctx->curfn == 0) {
        expr_signal(gctx->ectx, STC__LNKFNRTN, name_string(expr_func_name(exp)));
        return (neededtype == 0 ? 0 : LLVMConstNull(neededtype));
    }
    b = llvmgen_entry_builder(gctx);
    sig = LLVMBuildAlloca(b, vtype, "sigvec");
    LLVMDisposeBuilder(b);
    b = gctx->curfn->builder;
    idx[0] = idx[1] = LLVMConstNull(wt);
    LLVMBuildStore(b, LLVMConstInt(wt, n, 0), LLVMBuildGEP2(b, vtype, sig, idx, 2, ""));
    for (i = 1, arg = exprseq_head(args); arg != 0; i++, arg = arg->tq_next) {
        v = llvmgen_expression(gctx, arg, wt);
        idx[1] = LLVMConstInt(wt, i, 0);
        LLVMBuildStore(b, v, LLVMBuildGEP2(b, vtype, sig, idx, 2, ""));
    }
    cargs[0] = sig;
    cargs[1] = LLVMConstInt(wt, (unsigned long long) (intptr_t) ctx, 0);
    v = llvmgen_runtime_call(gctx, "bliss_signal", wt, cargs, 2);
    if (ctx != 0) {
        // SIGNAL_STOP has no value
        if (neededtype != 0) {
            expr_signal(gctx->ectx, STC__EXPRVALRQ);
            return LLVMConstNull(neededtype);
        }
        return 0;
    }
    return llvmgen_adjustval(gctx, v, neededtype, 0);

} /* gen_signal */

/*
 * gen_setunwind
 *
 * SETUNWIND (LRM 17.4.3.3): asks for unwinding when the current
 * handler returns.
 */
static LLVMValueRef
gen_setunwind (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    LLVMValueRef v;

    if (gctx->curfn == 0) {
        expr_signal(gctx->ectx, STC__LNKFNRTN, name_string(expr_func_name(exp)));
        return (neededtype == 0 ? 0 : LLVMConstNull(neededtype));
    }
    v = llvmgen_runtime_call(gctx, "bliss_setunwind", gctx->fullwordtype, 0, 0);
    return llvmgen_adjustval(gctx, v, neededtype, 0);

} /* gen_setunwind */

/*
 * flt_type
 *
 * LLVM type for a floating-point (F, D, G) or longword (L, I) operand.
 */
static LLVMTypeRef
flt_type (gencodectx_t gctx, char t)
{
    switch (t) {
        case 'F': return LLVMFloatTypeInContext(gctx->llvmctx);
        case 'D':
        case 'G': return LLVMDoubleTypeInContext(gctx->llvmctx);
        case 'H': return LLVMFP128TypeInContext(gctx->llvmctx);
        default:  return LLVMInt32TypeInContext(gctx->llvmctx);
    }

} /* flt_type */

/*
 * flt_load, flt_store
 *
 * Operands can be at any address: no alignment assumed.
 */
static LLVMValueRef
flt_load (LLVMBuilderRef b, LLVMTypeRef t, LLVMValueRef addr, const char *name)
{
    LLVMValueRef v = LLVMBuildLoad2(b, t, addr, name);
    LLVMSetAlignment(v, 1);
    return v;
}

static void
flt_store (LLVMBuilderRef b, LLVMValueRef v, LLVMValueRef addr)
{
    LLVMSetAlignment(LLVMBuildStore(b, v, addr), 1);
}

/*
 * flt_intrinsic
 *
 * Calls an overloaded math intrinsic, e.g. llvm.fabs.f32.
 */
static LLVMValueRef
flt_intrinsic (gencodectx_t gctx, const char *name, LLVMTypeRef *types, size_t ntypes,
               LLVMValueRef arg)
{
    unsigned int id = LLVMLookupIntrinsicID(name, strlen(name));
    LLVMValueRef fn = LLVMGetIntrinsicDeclaration(gctx->module, id, types, ntypes);
    LLVMTypeRef ftype = LLVMIntrinsicGetType(gctx->llvmctx, id, types, ntypes);

    return LLVMBuildCall2(gctx->curfn->builder, ftype, fn, &arg, 1, "");

} /* flt_intrinsic */

/*
 * flt_convert
 *
 * CVTxy (src, dst).
 */
static LLVMValueRef
flt_convert (gencodectx_t gctx, struct fltop_s *f, LLVMValueRef src, LLVMValueRef dst,
             LLVMTypeRef neededtype)
{
    LLVMBuilderRef b = gctx->curfn->builder;
    LLVMTypeRef st = flt_type(gctx, f->type), dt = flt_type(gctx, f->dtype);
    int sint = (f->type == 'L' || f->type == 'I');
    int dint = (f->dtype == 'L' || f->dtype == 'I');
    LLVMValueRef x = flt_load(b, st, src, ""), r, ok = 0;

    if (sint) {
        r = LLVMBuildSIToFP(b, x, dt, "");
    } else if (dint) {
        LLVMTypeRef types[2];
        if (f->op == 'r') {
            // Round half away from zero without libm: truncate to a
            // 64-bit integer, then step by one if the fraction is >= 1/2
            LLVMTypeRef i64 = LLVMInt64TypeInContext(gctx->llvmctx);
            LLVMValueRef t, frac, up, down, adj, small;
            types[0] = i64;
            types[1] = st;
            t = flt_intrinsic(gctx, "llvm.fptosi.sat", types, 2, x);
            frac = LLVMBuildFSub(b, x, LLVMBuildSIToFP(b, t, st, ""), "");
            up = LLVMBuildZExt(b, LLVMBuildFCmp(b, LLVMRealOGE, frac,
                                                LLVMConstReal(st, 0.5), ""), i64, "");
            down = LLVMBuildZExt(b, LLVMBuildFCmp(b, LLVMRealOLE, frac,
                                                  LLVMConstReal(st, -0.5), ""), i64, "");
            adj = LLVMBuildSub(b, up, down, "");
            small = LLVMBuildFCmp(b, LLVMRealOLT, flt_intrinsic(gctx, "llvm.fabs", &st, 1, x),
                                  LLVMConstReal(st, 4611686018427387904.0), "");
            t = LLVMBuildAdd(b, t, LLVMBuildSelect(b, small, adj, LLVMConstNull(i64), ""), "");
            x = LLVMBuildSelect(b, small, LLVMBuildSIToFP(b, t, st, ""), x, "");
        }
        // In range: -2^31 <= x < 2^31 (false for NaN)
        ok = LLVMBuildAnd(b,
                LLVMBuildFCmp(b, LLVMRealOGE, x, LLVMConstReal(st, -2147483648.0), ""),
                LLVMBuildFCmp(b, LLVMRealOLT, x, LLVMConstReal(st, 2147483648.0), ""), "");
        types[0] = dt;
        types[1] = st;
        r = flt_intrinsic(gctx, "llvm.fptosi.sat", types, 2, x);
    } else if (FLT_BITS(f->dtype) == FLT_BITS(f->type)) {
        r = x;      // D <-> G
    } else if (FLT_BITS(f->dtype) < FLT_BITS(f->type)) {
        // Narrowing: overflow if the result is infinite and x was not
        LLVMValueRef inf_r, inf_x;
        r = LLVMBuildFPTrunc(b, x, dt, "");
        inf_r = LLVMBuildFCmp(b, LLVMRealOEQ, flt_intrinsic(gctx, "llvm.fabs", &dt, 1, r),
                              LLVMConstReal(dt, 1.0/0.0), "");
        inf_x = LLVMBuildFCmp(b, LLVMRealOEQ, flt_intrinsic(gctx, "llvm.fabs", &st, 1, x),
                              LLVMConstReal(st, 1.0/0.0), "");
        ok = LLVMBuildOr(b, LLVMBuildNot(b, inf_r, ""), inf_x, "");
    } else {
        r = LLVMBuildFPExt(b, x, dt, "");
    }
    flt_store(b, r, dst);
    if (ok != 0) {
        return llvmgen_adjustval(gctx, LLVMBuildZExt(b, ok, gctx->fullwordtype, ""),
                                 neededtype, 0);
    }
    if (neededtype != 0) {
        expr_signal(gctx->ectx, STC__EXPRVALRQ);
        return LLVMConstNull(neededtype);
    }
    return 0;

} /* flt_convert */

/*
 * gen_float
 *
 * Floating-point BUILTINs. ADDx (a, b, dst): dst = a + b;
 * SUBx (a, b, dst): dst = b - a; MULx; DIVx (divisor, dividend, quot):
 * quot = dividend / divisor; CMPx (a, b): -1, 0 or 1.
 * CVTxy (src, dst) converts; to an integer it truncates (CVTRxy rounds
 * half away from zero) and saturates.
 */
static LLVMValueRef
gen_float (gencodectx_t gctx, void *ctx, expr_node_t *exp, LLVMTypeRef neededtype)
{
    struct fltop_s *f = ctx;
    LLVMBuilderRef b;
    LLVMTypeRef ptrtype = LLVMPointerTypeInContext(gctx->llvmctx, 0);
    LLVMTypeRef t = flt_type(gctx, f->type);
    LLVMValueRef addr[3], x, y, r;
    expr_node_t *arg;
    unsigned int i;

    if (gctx->curfn == 0) {
        expr_signal(gctx->ectx, STC__LNKFNRTN, name_string(expr_func_name(exp)));
        return (neededtype == 0 ? 0 : LLVMConstNull(neededtype));
    }
    b = gctx->curfn->builder;
    for (i = 0, arg = exprseq_head(expr_func_arglist(exp)); arg != 0 && i < 3;
         i++, arg = arg->tq_next) {
        addr[i] = llvmgen_expression(gctx, arg, ptrtype);
    }
    if (f->op == 'v' || f->op == 'r') {
        return flt_convert(gctx, f, addr[0], addr[1], neededtype);
    }
    x = flt_load(b, t, addr[0], "");
    y = flt_load(b, t, addr[1], "");
    switch (f->op) {
        case '+': r = LLVMBuildFAdd(b, x, y, ""); break;
        case '-': r = LLVMBuildFSub(b, y, x, ""); break;
        case '*': r = LLVMBuildFMul(b, x, y, ""); break;
        case '/': r = LLVMBuildFDiv(b, y, x, ""); break;
        default: {
            LLVMValueRef gt = LLVMBuildFCmp(b, LLVMRealOGT, x, y, "");
            LLVMValueRef lt = LLVMBuildFCmp(b, LLVMRealOLT, x, y, "");
            r = LLVMBuildSub(b, LLVMBuildZExt(b, gt, gctx->fullwordtype, ""),
                             LLVMBuildZExt(b, lt, gctx->fullwordtype, ""), "");
            return llvmgen_adjustval(gctx, r, neededtype, 1);
        }
    }
    flt_store(b, r, addr[2]);
    if (neededtype != 0) {
        expr_signal(gctx->ectx, STC__EXPRVALRQ);
        return LLVMConstNull(neededtype);
    }
    return 0;

} /* gen_float */
