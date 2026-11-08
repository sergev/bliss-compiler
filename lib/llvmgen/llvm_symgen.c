/*
 *++
 * llvm_symgen.c - Symbol generation for the LLVM back-end.
 *
 *
 * This module generates LLVM for symbols - literals,
 * data segments, BINDs, routines, labels, psects, and
 * modules.
 *
 * Besides generating the symbols themselves, this module
 * also handles generating addresses for the symbols needed
 * in fetch and assignment expressions.
 *
 * The front-end's symbols module has hooks that allow the
 * code generator to reserve additional memory for each
 * symbol, for tracking purposes.  We use this for stashing
 * the LLVM values for symbols, saving us an extra lookup
 * (and allowing for cases, like BINDs, where the BLISS name
 * doesn't necessarily map to an LLVM name).
 *
 * Copyright © 2013-2020, Matthew Madison.
 * All rights reserved.
 * Distributed under license. See LICENSE.TXT for details.
 *--
 */
#include "llvmgen.h"

// Upper bound on the number of expressions we'll process in
// a PRESET attribute.

/*
 * Tracking structures attached to symbols, used with
 * the sym_genspace hooks.
 */
struct llvm_litsym_s {
    LLVMValueRef        value;
};
typedef struct llvm_litsym_s llvm_litsym_t;

struct llvm_datasym_s {
    LLVMValueRef        value;
    LLVMTypeRef         type;
    unsigned int        flags;
    llvm_stgclass_t     vclass;
    int                 deref;
    int                 pending;    // deferred to block entry: LLVMGEN_K_PEND_xxx
    unsigned int        units;
};
typedef struct llvm_datasym_s llvm_datasym_t;

#define LLVMGEN_K_PEND_NONE 0
#define LLVMGEN_K_PEND_INIT 1       // LOCAL initializer
#define LLVMGEN_K_PEND_BIND 2       // local BIND value

struct llvm_rtnsym_s {
    LLVMTypeRef         type;
    LLVMTypeRef         returntype;
    LLVMValueRef        func;
    int                 pending;    // BIND ROUTINE value deferred to block entry
};
typedef struct llvm_rtnsym_s llvm_rtnsym_t;

struct llvm_label_s {
    llvm_btrack_t       *btrack;
};
typedef struct llvm_label_s llvm_label_t;

struct llvm_module_s {
    LLVMModuleRef        module;
};
typedef struct llvm_module_s llvm_module_t;

/*
 * machine_psects_init
 *
 * Set up the default PSECTs and their predeclared names.
 * XXX this is for Mach-O; needs to be selectable by target
 * object format.
 */
void
machine_psects_init (machinedef_t *mach, void *scope) {

    scopectx_t kwdscope = scope;
    machine_ctx_t mctx = machine_context(mach);
    name_t *np;
    strdesc_t datadata =   STRDEF("__DATA,__data");
    strdesc_t textconst =  STRDEF("__TEXT,__const");
    strdesc_t texttext =   STRDEF("__TEXT,__text");
    strdesc_t dotdata =    STRDEF(".data");
    strdesc_t dottext =    STRDEF(".text");
    strdesc_t dotrodata =  STRDEF(".rodata");

    np = psect_declare(kwdscope, (mctx->is_macho ? &datadata : &dotdata),
                       PSECT_M_ATTR_WRITE, 0);
    scope_sclass_psectname_set(kwdscope, SCLASS_OWN, np);
    scope_sclass_psectname_set(kwdscope, SCLASS_GLOBAL, np);
    // Read-only data: PLITs may hold addresses, so keep them out of .text
    np = psect_declare(kwdscope, (mctx->is_macho ? &textconst : &dotrodata), 0, 0);
    scope_sclass_psectname_set(kwdscope, SCLASS_PLIT, np);
    np = psect_declare(kwdscope, (mctx->is_macho ? &texttext : &dottext), PSECT_M_ATTR_EXEC, 0);
    scope_sclass_psectname_set(kwdscope, SCLASS_CODE, np);

} /* machine_psects_init */

/*
 * llvmgen_label_btrack
 *
 * Returns the pointer to the branch-tracking structure
 * associated with a label.  Used for handling LEAVE expressions.
 */
llvm_btrack_t *
llvmgen_label_btrack (name_t *np)
{
    llvm_label_t *lbl = sym_genspace(np);
    if (name_type(np) != LEXTYPE_NAME_LABEL) return 0;
    return lbl->btrack;

} /* llvmgen_label_btrack */

/*
 * llvmgen_label_btrack_set
 *
 * Sets the branch tracking context for a label.
 */
void
llvmgen_label_btrack_set (name_t *np, llvm_btrack_t *bt)
{
    llvm_label_t *lbl = sym_genspace(np);
    if (name_type(np) == LEXTYPE_NAME_LABEL) {
        lbl->btrack = bt;
    }

} /* llvmegen_label_btrack_set */

/*
 * litsym_generator
 *
 * Generates LLVM for a GLOBAL or EXTERNAL LITERAL.  A literal's
 * link-time value is the address of a symbol: a GLOBAL LITERAL is
 * exported as an absolute symbol, and an EXTERNAL LITERAL refers
 * to one.  Other literals are bound to their values by the front end.
 */
static int
litsym_generator (void *vctx, name_t *np, void *p)
{
    gencodectx_t gctx = vctx;
    literal_attr_t *attr = litsym_attr(np);
    llvm_litsym_t *ll = p;
    LLVMValueRef v;

    if (gctx->module == 0) {
        return 1;
    }
    if (attr->sc == SYMSCOPE_EXTERNAL) {
        v = LLVMGetNamedGlobal(gctx->module, name_azstring(np));
        if (v == 0) {
            v = LLVMGetNamedGlobalAlias(gctx->module, name_azstring(np), strlen(name_azstring(np)));
        }
        if (v == 0) {
            v = LLVMAddGlobal(gctx->module, gctx->unittype, name_azstring(np));
        }
        if ((attr->flags & SYM_M_WEAK) != 0) LLVMSetLinkage(v, LLVMExternalWeakLinkage);
        ll->value = v;
    } else if (attr->sc == SYMSCOPE_GLOBAL) {
        v = LLVMConstInt(gctx->fullwordtype, (unsigned long long int) name_value_signed(np), 1);
        v = LLVMConstIntToPtr(v, LLVMPointerTypeInContext(gctx->llvmctx, 0));
        ll->value = LLVMAddAlias2(gctx->module, gctx->unittype, 0, v, name_azstring(np));
        if ((attr->flags & SYM_M_WEAK) != 0) LLVMSetLinkage(ll->value, LLVMWeakAnyLinkage);
    }

    return 1;

} /* litsym_generator */

/*
 * initializer_arraysize
 *
 * Calculates the size of the array of LLVMValueRefs needed for
 * setting up an initializer.
 */
static unsigned int
initializer_arraysize (initval_t *ivlist)
{
    initval_t *iv;
    unsigned int count = 0;

    for (iv = ivlist; iv != 0; iv = iv->next) {
        if (iv->type == IVTYPE_LIST) {
            unsigned int thiscount = initializer_arraysize(iv->data.listptr);
            count += iv->repcount * thiscount;
        } else {
            count += iv->repcount;
        }
    }

    return count;

} /* initializer_arraysize */

/*
 * llvmgen_initializer
 *
 * Generates an array of LLVMValueRefs populated with the values
 * for an initializer.  Used for INITIAL attribute handling.
 */
static LLVMValueRef
llvmgen_initializer (gencodectx_t gctx, initval_t *ivlist, unsigned int padcount,
		     LLVMValueRef **arrp, unsigned int arrlen)
{
    unsigned int arrsize;
    unsigned int bpunit = machine_unit_bits(gctx->mach);
    LLVMValueRef *valarr, *valp, *lastvalp, oneval = 0, initconst;
    initval_t *iv;
    int allsametype = 1;

    valarr = (arrp == 0 ? 0 : *arrp);
    if (valarr == 0) {
        arrsize = initializer_arraysize(ivlist) + padcount;
        if (arrsize > 1) {
            valarr = malloc(arrsize * sizeof(LLVMValueRef));
            memset(valarr, 0, arrsize * sizeof(LLVMValueRef));
        } else {
            valarr = &oneval;
        }
    } else {
	arrsize = arrlen;
    }

    for (iv = ivlist, valp = valarr, lastvalp = 0; iv != 0; iv = iv->next) {
        unsigned int count = iv->repcount;
        LLVMValueRef thisval = 0;
        switch (iv->type) {
            case IVTYPE_LIST: {
                LLVMValueRef *savep = valp;
                LLVMValueRef *srcp, *dstp;

                llvmgen_initializer(gctx, iv->data.listptr, 0, &valp,
                                    (unsigned int) (arrsize-(valp-valarr)));
                dstp = valp;
                // Handle (> 1) repeat count by appending copies of the pointer(s) just inserted
                while (--count > 0) {
                    for (srcp = savep; srcp < valp; srcp++) *dstp++ = *srcp;
                }
                valp = dstp;
                // Compare first here against the last iteration of the loop
                if (allsametype && lastvalp != 0 && LLVMTypeOf(*lastvalp) != LLVMTypeOf(*savep)) {
                    allsametype = 0;
                }
                lastvalp = savep;
                // Now check all of the values just inserted
                while (allsametype && savep != valp) {
                    if (savep != valarr && LLVMTypeOf(*savep) != LLVMTypeOf(*(savep-1))) {
                        allsametype = 0;
                    }
                    savep += 1;
                }
                break;
            }
            case IVTYPE_STRING:
                thisval = LLVMConstStringInContext(gctx->llvmctx, iv->data.string->ptr,
                                                   (unsigned int) iv->data.string->len, 1);
                break;
            case IVTYPE_SCALAR:
                thisval = LLVMConstInt(LLVMIntTypeInContext(gctx->llvmctx,
                                                            iv->data.scalar.width * bpunit),
                                       (unsigned long long int) iv->data.scalar.value,
                                       iv->data.scalar.signext);
                break;
            case IVTYPE_EXPR_EXP: {
                expr_node_t *exp = iv->data.scalar.expr;
                thisval = llvmgen_expression(gctx, exp, 0);
                break;
            }
        }
        if (iv->type == IVTYPE_STRING || iv->type == IVTYPE_SCALAR ||
            iv->type == IVTYPE_EXPR_EXP) {
            LLVMValueRef *dstp = valp;
            while (count-- > 0) *dstp++ = thisval;
            if (allsametype && lastvalp != 0 && LLVMTypeOf(thisval) != LLVMTypeOf(*lastvalp)) {
                allsametype = 0;
            }
            lastvalp = valp;
            valp = dstp;
        }
    }

    if (arrp != 0) {
        *arrp = valp;
        return 0;
    }

    if (padcount > 0) {
        LLVMTypeRef utype = LLVMIntTypeInContext(gctx->llvmctx, bpunit);
        if (allsametype && LLVMTypeOf(*valarr) != utype) {
            allsametype = 0;
        }
        while (padcount-- > 0) {
            *valp++ = LLVMConstNull(utype);
        }
    }

    if (arrsize == 1) {
        return oneval;
    }

    if (allsametype) {
        initconst = LLVMConstArray(LLVMTypeOf(*valarr), valarr, arrsize);
    } else {
        initconst = LLVMConstStructInContext(gctx->llvmctx, valarr, arrsize, 1);
    }
    free(valarr);
    return initconst;

} /* llvmgen_initializer */

/*
 * preset_field
 *
 * Gets the unit offset, bit position, size and sign extension of
 * a PRESET item's field.
 */
static int
preset_field (gencodectx_t gctx, expr_node_t *exp, unsigned int *o,
              unsigned int *p, unsigned int *s, int *e)
{
    if (expr_type(exp) == EXPTYPE_PRIM_STRUREF) {
        exp = expr_struref_accexpr(exp);
    }
    if (expr_type(exp) == EXPTYPE_PRIM_SEG) {
        *o = (unsigned int) expr_seg_offset(exp);
        *p = 0;
        *s = expr_seg_width(exp);
        *e = expr_seg_signext(exp);
    } else if (expr_type(exp) == EXPTYPE_PRIM_FLDREF) {
        *o = (unsigned int) expr_seg_offset(expr_fldref_addr(exp));
        *p = (unsigned int) expr_litval(expr_fldref_pos(exp));
        *s = (unsigned int) expr_litval(expr_fldref_size(exp));
        *e = expr_fldref_signext(exp);
    } else {
        expr_signal(gctx->ectx, STC__INTCMPERR, "preset_field");
        return 0;
    }
    return 1;

} /* preset_field */

/*
 * llvmgen_presetter
 *
 * Builds the initializer for a PRESET attribute. Literal values are
 * OR-ed into a byte image of the segment, so fields that share a unit
 * work; addresses (relocatable) become separate elements between
 * the byte runs.
 */
static LLVMValueRef
llvmgen_presetter (gencodectx_t gctx, initval_t *ivlist, unsigned int padding)
{
    unsigned int bpunit = machine_unit_bits(gctx->mach);
    unsigned int bpaddr = machine_addr_bits(gctx->mach);
    struct addrcell_s {
        unsigned int o;
        LLVMValueRef val;
    } *acells, tmp;
    unsigned char *image, *used;
    unsigned int o, p, s, b, nbytes, ncells, nacells, i, j, k, start;
    int e;
    initval_t *iv;
    LLVMValueRef *varr, result;

    if (bpunit != 8 || bpaddr % 8 != 0) {
        expr_signal(gctx->ectx, STC__INTCMPERR, "llvmgen_presetter[0]");
        return 0;
    }
    nbytes = ncells = 0;
    for (iv = ivlist; iv != 0; iv = iv->next) {
        if (!preset_field(gctx, iv->preset_expr, &o, &p, &s, &e)) continue;
        if (o + (p + s + 7) / 8 > nbytes) nbytes = o + (p + s + 7) / 8;
        ncells += 1;
    }
    if (ncells == 0) {
        expr_signal(gctx->ectx, STC__INTCMPERR, "llvmgen_presetter[1]");
        return 0;
    }
    nbytes += padding;
    image = calloc(nbytes, 1);
    used = calloc(nbytes, 1);
    acells = calloc(ncells, sizeof(struct addrcell_s));
    nacells = 0;
    for (iv = ivlist; iv != 0; iv = iv->next) {
        expr_node_t *exp = iv->data.scalar.expr;
        unsigned int first;
        if (!preset_field(gctx, iv->preset_expr, &o, &p, &s, &e)) continue;
        if (expr_type(exp) == EXPTYPE_PRIM_SEG && (s != bpaddr || p % 8 != 0)) {
            expr_signal(gctx->ectx, STC__PRINVADSZ);
            continue;
        }
        if (expr_type(exp) != EXPTYPE_PRIM_LIT && expr_type(exp) != EXPTYPE_PRIM_SEG) {
            expr_signal(gctx->ectx, STC__PRBADVALUE);
            continue;
        }
        first = o * 8 + p;
        for (b = first; b < first + s; b++) {
            if (used[b / 8] & (1 << (b % 8))) break;
        }
        if (b < first + s) {
            expr_signal(gctx->ectx, STC__PROVERLAP);
            continue;
        }
        for (b = first; b < first + s; b++) {
            used[b / 8] |= 1 << (b % 8);
        }
        if (expr_type(exp) == EXPTYPE_PRIM_LIT) {
            unsigned long long v = (unsigned long long) expr_litval(exp);
            for (b = 0; b < s && b < 64; b++) {
                if ((v >> b) & 1) {
                    image[(first + b) / 8] |= 1 << ((first + b) % 8);
                }
            }
        } else {
            long offset = expr_seg_offset(exp);
            LLVMValueRef v = llvmgen_segaddress(gctx, expr_seg_name(exp), 0, 0, 0);
            if (offset != 0) {
                LLVMValueRef off = LLVMConstInt(gctx->fullwordtype,
                                                (unsigned long long) offset, 1);
                v = LLVMConstGEP2(gctx->unittype, v, &off, 1);
            }
            acells[nacells].o = first / 8;
            acells[nacells].val = v;
            nacells += 1;
        }
    }

    // Sort the address cells by offset
    for (i = 1; i < nacells; i++) {
        tmp = acells[i];
        for (j = i; j > 0 && acells[j-1].o > tmp.o; j--) {
            acells[j] = acells[j-1];
        }
        acells[j] = tmp;
    }

    // Byte runs between the address cells
    varr = malloc((2 * nacells + 1) * sizeof(LLVMValueRef));
    k = start = 0;
    for (i = 0; i < nacells; i++) {
        if (acells[i].o > start) {
            varr[k++] = LLVMConstStringInContext(gctx->llvmctx, (char *) image + start,
                                                 acells[i].o - start, 1);
        }
        varr[k++] = acells[i].val;
        start = acells[i].o + bpaddr / 8;
    }
    if (start < nbytes) {
        varr[k++] = LLVMConstStringInContext(gctx->llvmctx, (char *) image + start,
                                             nbytes - start, 1);
    }
    result = (k == 1) ? varr[0] : LLVMConstStructInContext(gctx->llvmctx, varr, k, 1);
    free(varr);
    free(acells);
    free(used);
    free(image);
    return result;

} /* llvmgen_presetter */

/*
 * handle_initializer
 *
 * Handles initialization (INITIAL and PRESET) attributes for a data symbol.
 * Specifics depend on the type of symbol (OWN/GLOBAL/PLIT or LOCAL), and whether
 * we're doing INITIAL or PRESET.
 */
static void
handle_initializer (gencodectx_t gctx, llvm_datasym_t *ld, name_t *np, unsigned int typesize)
{
    unsigned int bpunit = machine_unit_bits(gctx->mach);
    data_attr_t *attr = datasym_attr(np);
    unsigned int nunits, padding;
    LLVMValueRef initval = 0;

    nunits = initval_size(gctx->symctx, attr->ivlist);
    if (nunits > typesize) {
        if (typesize != 0) {
            log_signal(expr_logctx(gctx->ectx), name_defpos(np), STC__INITSZERR, name_string(np));
        }
        padding = 0;
    } else {
        padding = typesize - nunits;
        if (padding > 1024) {
            expr_signal(gctx->ectx, STC__INTCMPERR, "handle_initializer[1]");
        }
    }
    if (attr->ivlist->preset_expr == 0) {
        initval = llvmgen_initializer(gctx, attr->ivlist, padding, 0, 0);
    } else {
        initval_t *iv;
        for (iv = attr->ivlist; iv != 0; iv = iv->next) {
            expr_node_t *exp = iv->data.scalar.expr;
            if (!expr_is_ltce(exp)) break;
        }
        if (iv == 0) {
            initval = llvmgen_presetter(gctx, attr->ivlist, padding);
        } else {
            llvmgen_memset(gctx, ld->value, LLVMConstNull(LLVMIntTypeInContext(gctx->llvmctx, bpunit)),
                           LLVMConstInt(gctx->fullwordtype, attr->units, 0));
            for (iv = attr->ivlist; iv != 0; iv = iv->next) {
                llvmgen_assignment(gctx, iv->preset_expr, iv->data.scalar.expr);
            }
        }
    }
    if (initval == 0) {
        expr_signal(gctx->ectx, STC__INTCMPERR, "handle_initializer");
        return;
    }
    if (ld->vclass == LLVM_GLOBAL) {
        ld->type = LLVMTypeOf(initval);
        ld->value = LLVMAddGlobal(gctx->module, ld->type, name_azstring(np));
        LLVMSetInitializer(ld->value, initval);
    } else {
        if (typesize <= machine_scalar_bits(gctx->mach)) {
            LLVMValueRef addr = llvmgen_adjustval(gctx, ld->value, LLVMPointerType(LLVMTypeOf(initval), 0), 0);
            LLVMBuildStore(gctx->curfn->builder, initval, addr);
        } else {
            // Copy an aggregate initializer from a private constant
            LLVMTypeKind k = LLVMGetTypeKind(LLVMTypeOf(initval));
            if (k == LLVMStructTypeKind || k == LLVMArrayTypeKind) {
                LLVMValueRef g = LLVMAddGlobal(gctx->module, LLVMTypeOf(initval), llvmgen_global(gctx));
                LLVMSetLinkage(g, LLVMPrivateLinkage);
                LLVMSetGlobalConstant(g, 1);
                LLVMSetInitializer(g, initval);
                initval = g;
            }
            llvmgen_memcpy(gctx, ld->value, initval, LLVMConstInt(gctx->fullwordtype, typesize, 0));
        }
    }

    if (typesize == 0 && attr->units == 0) {
        attr->units = nunits;
    }

} /* handle_initializer */

/*
 * gendatatype
 *
 * Generates the appropriate LLVM type for a data symbol.
 */
static LLVMTypeRef
gendatatype (gencodectx_t gctx, data_attr_t *attr, unsigned int *ucountp)
{
    unsigned int bpunit = machine_unit_bits(gctx->mach);
    int isref = (attr->flags & SYM_M_REF) != 0;
    LLVMTypeRef basetype;

    if (attr->struc != 0 || attr->units > machine_scalar_units(gctx->mach)) {
        // BINDs cannot be arrays, since they don't allocate storage.
        if (attr->units == 0 || (attr->flags & SYM_M_BIND) != 0) {
            basetype = LLVMIntTypeInContext(gctx->llvmctx, bpunit);
            if (attr->dclass != DCLASS_ARG) {
                basetype = LLVMPointerType(basetype, 0);
            }
        } else {
            basetype = LLVMArrayType(LLVMIntTypeInContext(gctx->llvmctx, bpunit), attr->units);
        }
    } else {
        basetype = LLVMIntTypeInContext(gctx->llvmctx, attr->units * bpunit);
    }
    if (isref) {
        if (ucountp != 0) *ucountp = machine_addr_bits(gctx->mach)/bpunit;
        return LLVMPointerType(basetype, 0);
    }
    if (ucountp != 0) *ucountp = attr->units;
    return basetype;

} /* gendatatype */

/*
 * datasym_generator
 *
 * Generates a data symbol.  Routine arguments and local BINDs are LLVM registers
 * (they don't actually allocate any storage).
 */
static int
datasym_generator (void *vctx, name_t *np, void *p)
{
    gencodectx_t gctx = vctx;
    namectx_t namectx = scope_namectx(name_scope(np));
    llvm_datasym_t *ld = p;
    data_attr_t *attr = datasym_attr(np);
    LLVMTypeRef type;
    unsigned int units;

    if ((attr->flags & SYM_M_FORWARD) != 0) {
        return 1;
    }

    ld->flags = (((attr->flags & SYM_M_SIGNEXT) != 0 ? LLVMGEN_M_SEG_SIGNEXT : 0) |
                 ((attr->flags & SYM_M_VOLATILE) != 0 ? LLVMGEN_M_SEG_VOLATILE : 0) |
                 ((attr->flags & SYM_M_REF) != 0 ? LLVMGEN_M_SEG_ISREF : 0) |
                 ((attr->flags & SYM_M_BIND) != 0 ? LLVMGEN_M_SEG_ISBIND : 0));

    if (attr->dclass == DCLASS_ARG) {
        ld->vclass = LLVM_REG;
    } else if ((attr->flags & SYM_M_BIND) != 0) {
        initval_t *iv = attr->ivlist;
        LLVMValueRef bindval;
        type = gendatatype(gctx, attr, &units);
        if (attr->struc == 0 && attr->units <= machine_scalar_units(gctx->mach)) {
            type = LLVMPointerType(type, 0);
            ld->flags |= LLVMGEN_M_SEG_BINDPTR;
        }
        if (iv->type == IVTYPE_SCALAR) {
            unsigned int bpunit = machine_unit_bits(gctx->mach);
            LLVMValueRef sval;
            sval = LLVMConstInt(LLVMIntTypeInContext(gctx->llvmctx,
                                                     iv->data.scalar.width * bpunit),
                                (unsigned long long int) iv->data.scalar.value, iv->data.scalar.signext);
            bindval = llvmgen_adjustval(gctx, sval, type,
                                        (attr->flags & SYM_M_SIGNEXT) != 0);
        } else {
            expr_node_t *bindexp = iv->data.scalar.expr;
            if (attr->sc == SYMSCOPE_LOCAL && gctx->curfn != 0 &&
                !expr_is_ctce(bindexp) && !expr_is_ltce(bindexp)) {
                // Run-time value: computed at entry to the declaring block
                ld->type = type;
                ld->vclass = LLVM_REG;
                ld->pending = LLVMGEN_K_PEND_BIND;
                return 1;
            }
            bindval = llvmgen_expression(gctx, bindexp, type);
        }
        if (attr->sc == SYMSCOPE_LOCAL) {
            ld->value = bindval;
            ld->type = type;
            if (LLVMIsAInstruction(ld->value)) LLVMSetValueName(ld->value, name_azstring(np));
            ld->vclass = LLVM_REG;
        } else if (attr->sc == SYMSCOPE_GLOBAL && LLVMIsConstant(bindval)) {
            // The bound address is used directly; export it as a symbol
            // whose address is that value.
            ld->value = bindval;
            ld->type = type;
            ld->vclass = LLVM_REG;
            if (LLVMGetTypeKind(LLVMTypeOf(bindval)) == LLVMPointerTypeKind) {
                LLVMValueRef a = LLVMAddAlias2(gctx->module, gctx->unittype, 0, bindval,
                                               name_azstring(np));
                if ((attr->flags & SYM_M_WEAK) != 0) LLVMSetLinkage(a, LLVMWeakAnyLinkage);
            } else {
                char buf[256];
                snprintf(buf, sizeof(buf), ".%s %s\n.set %s, %lld",
                         (attr->flags & SYM_M_WEAK) != 0 ? "weak" : "globl", name_azstring(np),
                         name_azstring(np), LLVMConstIntGetSExtValue(bindval));
                LLVMAppendModuleInlineAsm(gctx->module, buf, strlen(buf));
            }
        } else {
            ld->value = LLVMAddGlobal(gctx->module, type, name_azstring(np));
            ld->type = type;
            LLVMSetInitializer(ld->value, bindval);
            ld->vclass = LLVM_GLOBAL;
            if (name_globalname(namectx, np) == 0) {
                LLVMSetLinkage(ld->value, LLVMInternalLinkage);
            }
        }
    } else if (attr->dclass == DCLASS_STATIC) {
        ld->vclass = LLVM_GLOBAL;
        type = gendatatype(gctx, attr, &units);
        if (attr->ivlist != 0) {
            handle_initializer(gctx, ld, np, units);
        } else {
            ld->value = LLVMAddGlobal(gctx->module, type, name_azstring(np));
            ld->type = type;
            if (attr->sc != SYMSCOPE_EXTERNAL) {
                LLVMSetInitializer(ld->value, LLVMConstNull(type));
            }
        }
        if (attr->owner != 0) {
            LLVMSetSection(ld->value, name_azstring(attr->owner));
        }
        if (name_globalname(namectx, np) == 0 && attr->sc != SYMSCOPE_EXTERNAL) {
            LLVMSetLinkage(ld->value, LLVMInternalLinkage);
        } else if ((attr->flags & SYM_M_WEAK) != 0) {
            LLVMSetLinkage(ld->value, attr->sc == SYMSCOPE_EXTERNAL
                           ? LLVMExternalWeakLinkage : LLVMWeakAnyLinkage);
        }
        if (attr->alignment != 0) {
            LLVMSetAlignment(ld->value, 1U<<attr->alignment);
        }
    } else if (gctx->curfn == 0) {
        // LOCAL outside a routine (e.g. placeholder for an undeclared
        // name): no stack frame, so make it a static
        type = gendatatype(gctx, attr, &units);
        ld->value = LLVMAddGlobal(gctx->module, type, name_azstring(np));
        ld->type = type;
        LLVMSetInitializer(ld->value, LLVMConstNull(type));
        LLVMSetLinkage(ld->value, LLVMInternalLinkage);
        ld->vclass = LLVM_GLOBAL;
    } else {
        // Everything else is LOCAL, with an alloca, even REGISTER
        // declarations. XXX revisit later
        ld->vclass = LLVM_LOCAL;
        type = gendatatype(gctx, attr, &units);
        ld->value = LLVMBuildAlloca(gctx->curfn->builder, type, name_azstring(np));
        ld->type = type;
        if (attr->alignment != 0) {
            HelperSetAllocaAlignment(ld->value, 1U<<attr->alignment);
        }
        if (attr->ivlist != 0) {
            // Initialized at entry to the declaring block
            ld->units = units;
            ld->pending = LLVMGEN_K_PEND_INIT;
        }
    }

    return 1;

} /* datasym_generator */

/*
 * llvmgen_block_begin
 *
 * Emits, in declaration order, the run-time work deferred for the
 * names declared in a block: LOCAL initializers and local BIND values.
 */
void
llvmgen_block_begin (gencodectx_t gctx, scopectx_t scope)
{
    void *ctx = 0;
    name_t *np;

    if (scope == 0 || gctx->curfn == 0) return;
    for (np = scope_nextname(scope, &ctx); np != 0; np = scope_nextname(scope, &ctx)) {
        llvm_datasym_t *ld;
        if (name_type(np) == LEXTYPE_NAME_ROUTINE) {
            llvm_rtnsym_t *lr = sym_genspace(np);
            if (lr->pending) {
                routine_attr_t *attr = rtnsym_attr(np);
                lr->func = llvmgen_expression(gctx, attr->ivlist->data.scalar.expr,
                                              LLVMPointerTypeInContext(gctx->llvmctx, 0));
            }
            continue;
        }
        if (name_type(np) != LEXTYPE_NAME_DATA) continue;
        ld = sym_genspace(np);
        if (ld->pending == LLVMGEN_K_PEND_INIT) {
            handle_initializer(gctx, ld, np, ld->units);
        } else if (ld->pending == LLVMGEN_K_PEND_BIND) {
            data_attr_t *attr = datasym_attr(np);
            ld->value = llvmgen_expression(gctx, attr->ivlist->data.scalar.expr, ld->type);
            if (LLVMIsAInstruction(ld->value)) LLVMSetValueName(ld->value, name_azstring(np));
        }
    }

} /* llvmgen_block_begin */

/*
 * rtnsym_generator
 *
 * Generates a routine symbol.  For EXTERNAL and FORWARD declarations, we
 * generate a generic (varargs) LLVM function prototype.  When a FORWARD declaration
 * is later instantiated by a routine definition, we replace the original
 * prototype with the actual prototype that has the correct number of
 * parameters.
 */
static int
rtnsym_generator (void *vctx, name_t *np, void *p)
{
    gencodectx_t gctx = vctx;
    llvm_rtnsym_t *lr = p;
    routine_attr_t *attr = rtnsym_attr(np);
    nameref_t *ref;
    LLVMValueRef extantfn, arg;
    LLVMTypeRef  argtypes[LLVMGEN_K_MAXARGS];
    int proto_only = ((attr->flags & SYM_M_FORWARD) != 0 || attr->sc == SYMSCOPE_EXTERNAL);
    unsigned int argcount, i;

    extantfn = LLVMGetNamedFunction(gctx->module, name_azstring(np));
    if (extantfn != 0 && proto_only) {
        return 1;
    }
    if (extantfn != 0 && !LLVMIsDeclaration(extantfn)) {
        extantfn = 0;   // another routine of that name (nested): LLVM renames ours
    }
    lr->returntype = ((attr->flags & SYM_M_NOVALUE) == 0 ?
                      LLVMIntTypeInContext(gctx->llvmctx, machine_scalar_bits(gctx->mach)) :
                      LLVMVoidTypeInContext(gctx->llvmctx));
    if (proto_only) {
        // fastcc (JSB) cannot be varargs; calls give the real argument types
        lr->type = LLVMFunctionType(lr->returntype, 0, 0, attr->linkage != LNKTYPE_JSB);
        lr->func = LLVMAddFunction(gctx->module, name_azstring(np), lr->type);
        LLVMSetFunctionCallConv(lr->func, llvmgen_callconv(attr->linkage));
        if ((attr->flags & SYM_M_WEAK) != 0) LLVMSetLinkage(lr->func, LLVMExternalWeakLinkage);
        return 1;
    }
    if ((attr->flags & SYM_M_BIND) != 0) {
        // BIND ROUTINE: alias for the bound address, no function of its own
        expr_node_t *bexp = (attr->ivlist == 0 ? 0 : attr->ivlist->data.scalar.expr);
        LLVMValueRef v = 0;
        if (bexp != 0 && gctx->curfn != 0 && attr->sc == SYMSCOPE_LOCAL &&
            !expr_is_ctce(bexp) && !expr_is_ltce(bexp)) {
            // Run-time value: computed at entry to the declaring block
            lr->type = LLVMFunctionType(lr->returntype, 0, 0, 1);
            lr->func = 0;
            lr->pending = 1;
            return 1;
        }
        if (bexp != 0 && (gctx->curfn != 0 || expr_is_ctce(bexp) || expr_is_ltce(bexp))) {
            v = llvmgen_expression(gctx, bexp, LLVMPointerTypeInContext(gctx->llvmctx, 0));
        }
        if (v != 0 && LLVMIsAFunction(v)) {
            lr->func = v;
            lr->type = LLVMGlobalGetValueType(v);
            lr->returntype = LLVMGetReturnType(lr->type);
        } else {
            lr->type = LLVMFunctionType(lr->returntype, 0, 0, 1);
            lr->func = (v != 0 ? v : LLVMConstNull(LLVMPointerTypeInContext(gctx->llvmctx, 0)));
        }
        return 1;
    }
    argcount = namereflist_length(&attr->inargs);
    if (argcount > LLVMGEN_K_MAXARGS) {
        expr_signal(gctx->ectx, STC__EXCRTNPARS, name_string(np));
        argcount = LLVMGEN_K_MAXARGS;
    }
    for (i = 0, ref = namereflist_head(&attr->inargs); i < argcount; i++, ref = ref->tq_next) {
        argtypes[i] = gendatatype(gctx, datasym_attr(ref->np), 0);
    }
    // XXX need machine-specific VARARG linkage attribute
    lr->type = LLVMFunctionType(lr->returntype, argtypes, argcount, 0);
    if (extantfn != 0) {
        LLVMSetValueName(extantfn, "$$function.for.replacement");
    }
    lr->func = LLVMAddFunction(gctx->module, name_azstring(np), lr->type);
    LLVMSetFunctionCallConv(lr->func, llvmgen_callconv(attr->linkage));
    if (extantfn != 0) {
        LLVMReplaceAllUsesWith(extantfn, lr->func);
        LLVMDeleteFunction(extantfn);
    }
    if (name_globalname(expr_namectx(gctx->ectx), np) != 0) {
        LLVMSetLinkage(lr->func, (attr->flags & SYM_M_WEAK) != 0
                       ? LLVMWeakAnyLinkage : LLVMExternalLinkage);
    } else {
        LLVMSetLinkage(lr->func, LLVMInternalLinkage);
    }
    if (attr->owner != 0) {
        LLVMSetSection(lr->func, name_azstring(attr->owner));
    }
    for (i = 0, ref = namereflist_head(&attr->inargs); i < argcount; i++, ref = ref->tq_next) {
        llvm_datasym_t *ld = sym_genspace(ref->np);
        arg = LLVMGetParam(lr->func, i);
        ld->value = arg;
        ld->type = argtypes[i];
        LLVMSetValueName(arg, name_azstring(ref->np));
    }

    return 1;

} /* rtnsym_generator */


/*
 * llvmgen_deref_push
 *
 * Pushes notification of the need to automatically dereference a REF
 * symbol.  Called when computing the address for a fetch or an assignment
 * operation.  It's counted because multiple such derefs could appear in
 * a single expression.
 */
void
llvmgen_deref_push (gencodectx_t gctx, name_t *np)
{
    lextype_t ntype = name_type(np);
    llvm_datasym_t *ld;
    if (ntype != LEXTYPE_NAME_DATA) {
        expr_signal(gctx->ectx, STC__INTCMPERR, "llvmgen_deref_push");
    }
    ld = sym_genspace(np);
    ld->deref += 1;

} /* llvmgen_deref_push */

/*
 * llvmgen_deref_pop
 *
 * Pops a dereference notification.
 */
void
llvmgen_deref_pop (gencodectx_t gctx, name_t *np)
{
    lextype_t ntype = name_type(np);
    llvm_datasym_t *ld;
    if (ntype != LEXTYPE_NAME_DATA) {
        expr_signal(gctx->ectx, STC__INTCMPERR, "llvmgen_deref_pop");
    }
    ld = sym_genspace(np);
    ld->deref -= 1;

} /* llvmgen_deref_pop */

/*
 * llvmgen_segaddress
 *
 * Generates an address.  This should only be applied to ROUTINEs
 * and data segments.  Automatically handles auto-dereferencing of REF
 * pointers.
 */
LLVMValueRef
llvmgen_segaddress (gencodectx_t gctx, name_t *np, llvm_stgclass_t *segclassp,
                    unsigned int *flagsp, LLVMTypeRef *llvm_type)
{
    lextype_t ntype = name_type(np);

    if (ntype == LEXTYPE_NAME_ROUTINE) {
        llvm_rtnsym_t *lr = sym_genspace(np);
        if (lr->type == 0) rtnsym_generator(gctx, np, lr);     // e.g. from a library
        if (segclassp != 0) *segclassp = LLVM_GLOBAL;
        if (flagsp != 0) *flagsp = machine_addr_signed(gctx->mach) ? LLVMGEN_M_SEG_SIGNEXT : 0;
        if (llvm_type != 0) *llvm_type = lr->type;
        return lr->func;
    }
    if (ntype == LEXTYPE_NAME_DATA) {
        llvm_datasym_t *ld = sym_genspace(np);
        LLVMValueRef val;
        if (ld->value == 0 && ld->type == 0 && gctx->module != 0) {
            datasym_generator(gctx, np, ld);    // e.g. from a library
        }
        if (segclassp != 0) *segclassp = ld->vclass;
        if (flagsp != 0) *flagsp = ld->flags;
        if (llvm_type != 0) *llvm_type = ld->type;
        val = ld->value;
        if (ld->deref != 0 && (ld->flags & LLVMGEN_M_SEG_ISREF) != 0) {
            if (flagsp != 0) *flagsp |= LLVMGEN_M_SEG_DEREFED;
            if (ld->vclass != LLVM_REG) {
                val = LLVMBuildLoad2(gctx->curfn->builder, ld->type, val, llvmgen_temp(gctx));
            }
        }
        return val;
    }
    if (ntype == LEXTYPE_NAME_LITERAL) {
        // EXTERNAL LITERAL: the value is the address of the symbol
        llvm_litsym_t *ll = sym_genspace(np);
        if (ll->value == 0) litsym_generator(gctx, np, ll);    // e.g. from a library
        if (segclassp != 0) *segclassp = LLVM_GLOBAL;
        if (flagsp != 0) *flagsp = 0;
        if (llvm_type != 0) *llvm_type = gctx->unittype;
        return ll->value;
    }
    // Should never be called on any other name types
    expr_signal(gctx->ectx, STC__INTCMPERR, "llvm_segaddress");
    return 0;

} /* llvmgen_segaddress */

/*
 * llvmgen_args_to_locals
 *
 * Formal parameters are variables: they can be assigned to and
 * have their addresses taken.  Copy each incoming argument into
 * a stack slot at routine entry.
 */
void
llvmgen_args_to_locals (gencodectx_t gctx, name_t *np)
{
    routine_attr_t *attr = rtnsym_attr(np);
    nameref_t *ref;

    for (ref = namereflist_head(&attr->inargs); ref != 0; ref = ref->tq_next) {
        llvm_datasym_t *ld = sym_genspace(ref->np);
        LLVMValueRef slot;
        if (ld->value == 0 || ld->type == 0 || ld->vclass != LLVM_REG) continue;
        slot = LLVMBuildAlloca(gctx->curfn->builder, ld->type, name_azstring(ref->np));
        LLVMBuildStore(gctx->curfn->builder, ld->value, slot);
        ld->value = slot;
        ld->vclass = LLVM_LOCAL;
    }

} /* llvmgen_args_to_locals */

/*
 * llvmgen_callconv
 *
 * LLVM calling convention for a BLISS linkage type.
 */
unsigned int
llvmgen_callconv (int lnktype)
{
    return (lnktype == LNKTYPE_JSB ? LLVMFastCallConv : LLVMCCallConv);

} /* llvmgen_callconv */

/*
 * llvmgen_argcount_global
 *
 * The thread-local cell in which a caller passes the number of
 * arguments (for ACTUALCOUNT). Weak, so each module can define it.
 */
LLVMValueRef
llvmgen_argcount_global (gencodectx_t gctx)
{
    static const char name[] = "bliss_argcount";
    LLVMValueRef g = LLVMGetNamedGlobal(gctx->module, name);

    if (g == 0) {
        g = LLVMAddGlobal(gctx->module, gctx->fullwordtype, name);
        LLVMSetInitializer(g, LLVMConstNull(gctx->fullwordtype));
        LLVMSetLinkage(g, LLVMWeakAnyLinkage);
        LLVMSetThreadLocal(g, 1);
        LLVMSetThreadLocalMode(g, LLVMInitialExecTLSModel);
    }
    return g;

} /* llvmgen_argcount_global */

/*
 * llvmgen_rtn_make_varargs
 *
 * Turns a routine into a varargs function, for ACTUALPARAMETER and
 * ARGPTR, by moving its body into a new function.
 */
void
llvmgen_rtn_make_varargs (gencodectx_t gctx, name_t *np)
{
    llvm_rtnsym_t *lr = sym_genspace(np);
    LLVMValueRef oldfn = lr->func, newfn;
    LLVMTypeRef ptypes[LLVMGEN_K_MAXARGS];
    LLVMBasicBlockRef bb;
    const char *sect;
    unsigned int i, n = LLVMCountParamTypes(lr->type);

    if (LLVMIsFunctionVarArg(lr->type)) return;
    LLVMGetParamTypes(lr->type, ptypes);
    lr->type = LLVMFunctionType(lr->returntype, ptypes, n, 1);
    LLVMSetValueName(oldfn, "$$function.for.replacement");
    newfn = LLVMAddFunction(gctx->module, name_azstring(np), lr->type);
    LLVMSetLinkage(newfn, LLVMGetLinkage(oldfn));
    LLVMSetFunctionCallConv(newfn, LLVMGetFunctionCallConv(oldfn));
    sect = LLVMGetSection(oldfn);
    if (sect != 0 && *sect != '\0') LLVMSetSection(newfn, sect);
    for (i = 0; i < n; i++) {
        size_t len;
        LLVMValueRef oldp = LLVMGetParam(oldfn, i), newp = LLVMGetParam(newfn, i);
        LLVMSetValueName2(newp, LLVMGetValueName2(oldp, &len), len);
        LLVMReplaceAllUsesWith(oldp, newp);
    }
    while ((bb = LLVMGetFirstBasicBlock(oldfn)) != 0) {
        LLVMRemoveBasicBlockFromParent(bb);
        LLVMAppendExistingBasicBlock(newfn, bb);
    }
    LLVMReplaceAllUsesWith(oldfn, newfn);
    LLVMDeleteFunction(oldfn);
    lr->func = newfn;

} /* llvmgen_rtn_make_varargs */

/*
 * llvmgen_rtntype_info
 *
 * Returns LLVM type info about a routine.
 * Returns: function address
 */
LLVMValueRef
llvmgen_rtntype_info (gencodectx_t gctx, name_t *np, LLVMTypeRef *func_type, LLVMTypeRef *return_type)
{
    lextype_t ntype = name_type(np);

    if (ntype == LEXTYPE_NAME_ROUTINE) {
        llvm_rtnsym_t *lr = sym_genspace(np);
        if (lr->type == 0) {
            rtnsym_generator(gctx, np, lr);     // e.g. from a library
        }
        if (func_type != 0) *func_type = lr->type;
        if (return_type != 0) *return_type = lr->returntype;
        return lr->func;
    }
    // Should never be called on any other name types
    expr_signal(gctx->ectx, STC__INTCMPERR, "llvmgen_rtntype_info");
    return 0;

} /* llvmgen_rtntype_info */
/*
 * symbol_gen_dispatch
 *
 * Dispatcher routine for generating a symbol.  Called
 * by the front end.
 */
static int
symbol_gen_dispatch (void *vctx, name_t *np, void *p)
{
    gencodectx_t gctx = vctx;

    // No module when generating a library: nothing to emit
    if (gctx->module == 0) return 1;

    switch (name_type(np)) {
        case LEXTYPE_NAME_LITERAL:
            return litsym_generator(vctx, np, p);
        case LEXTYPE_NAME_DATA:
            return datasym_generator(vctx, np, p);
        case LEXTYPE_NAME_ROUTINE:
            return rtnsym_generator(vctx, np, p);
        default:
            return 1;
    }

} /* symbol_gen_dispatch */

/*
 * symbol_gensize
 *
 * Returns the amount of space needed for symbol tracking.
 * Called by the front end when defining a symbol.
 */
static unsigned int
symbol_gensize (void *vctx, lextype_t lt) {
    switch (lt) {
        case LEXTYPE_NAME_DATA:
            return sizeof(llvm_datasym_t);
        case LEXTYPE_NAME_ROUTINE:
            return sizeof(llvm_rtnsym_t);
        case LEXTYPE_NAME_LABEL:
            return sizeof(llvm_label_t);
        case LEXTYPE_NAME_MODULE:
            return sizeof(llvm_module_t);
        case LEXTYPE_NAME_LITERAL:
            return sizeof(llvm_litsym_t);
        default:
            return 0;
    }

} /* symbol_gensize */

/*
 * llvmgen_symgen_init
 *
 * Initializes the code generation size and dispatch
 * hooks invoked from the front-end symbols module.
 */
void
llvmgen_symgen_init (gencodectx_t gctx)
{
    static sym_genvec_t vec = {
        symbol_gensize, symbol_gen_dispatch, 0, 0, 0
    };

    symbols_gen_register(gctx->symctx, gctx, &vec);

} /* llvmgen_symgen_init */
