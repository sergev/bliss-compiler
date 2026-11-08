#ifndef switches_h__
#define switches_h__
/*
 *++
 * switches.h - Compiler switches definitions.
 *
 * Copyright © 2012, Matthew Madison.
 * All rights reserved.
 * Distributed under license. See LICENSE.TXT for details.
 *--
 */

#include "parser.h"
#include "nametable.h"
#include "lexeme.h"

typedef int (*switch_parse_handler_fn)(parse_ctx_t pctx, void *ctx,
                                       lextype_t dcltype, lexeme_t *swlex);
typedef int (*toggle_handler_fn)(parse_ctx_t pctx, void *ctx, int togidx,
                                 lextype_t dcltype, name_t *togname);
int switch_special_declare(scopectx_t scope, lextype_t swtype,
                           switch_parse_handler_fn handler, void *ctx);
int switch_toggle_declare(scopectx_t scope, strdesc_t *name,
                          toggle_handler_fn handler, void *ctx, int togidx,
                          name_t **on_name, name_t **off_name);
int value_toggle_declare(scopectx_t scope, strdesc_t *name,
                         toggle_handler_fn handler, void *ctx, int togidx,
                         name_t **on_name, name_t **off_name);
int value_toggle_dispatch(parse_ctx_t pctx, void *ctx, lextype_t dcltype,
                          name_t *togname);
void switches_ignored_declare(scopectx_t scope);

// On-off switches, for %SWITCHES
#define SWITCH_K_ERRS     0
#define SWITCH_K_OPTIMIZE 1
#define SWITCH_K_SAFE     2
#define SWITCH_K_UNAMES   3
#define SWITCH_K_ZIP      4
#define SWITCH_K_CODE     5
#define SWITCH_K_DEBUG    6
#define SWITCH_K_COUNT    7
void switch_record(parse_ctx_t pctx, int idx, int on);
int switch_test(scopectx_t scope, strdesc_t *name);
int parse_switches(parse_ctx_t pctx, lextype_t decltype, lextype_t terminator);

#endif /* switches_h__ */
