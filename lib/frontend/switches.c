/*
 *++
 * switches.c - Management of compiler switches
 *
 * This module implements the framework for handling
 * compiler switches and the parse routine for parsing
 * the contents of a SWITCHES declaration and the switches
 * that are specified on a MODULE declaration.
 *
 * There are two basic types of switches: toggle (called 'on-off' in
 * the LRM), and special.  Toggle switches have two entries in the
 * name table: the base name of the toggle, and the "NO" form;
 * the lextype is used to differentiate the two.  As an added bonus,
 * this module supports a more generic "value" toggle that has
 * the same mechanics, for use by other modules (cf. the listings
 * module).
 *
 * Special switches can take values in a variety of formats, and
 * so require a handler to parse the switch value and take the
 * appropriate action.  Each special switch is a separate keyword
 * with its own lextype.
 *
 * Copyright © 2012-2020, Matthew Madison.
 * All rights reserved.
 * Distributed under license. See LICENSE.TXT for details.
 *--
 */
#include "blissc/switches.h"
#include "blissc/symbols.h"

struct switch_special_s {
    switch_parse_handler_fn handler;
    void                    *ctx;
};

struct toggle_s {
    toggle_handler_fn handler;
    void              *ctx;
    int               togindex;
};

static namedef_t special_switches[LEXTYPE_SWITCH_MAX-LEXTYPE_SWITCH_MIN+1] = {
    NAMEDEF("IDENT", LEXTYPE_SWITCH_IDENT, 0),
    NAMEDEF("LANGUAGE", LEXTYPE_SWITCH_LANGUAGE, 0),
    NAMEDEF("LIST", LEXTYPE_SWITCH_LIST, 0),
    NAMEDEF("MAIN", LEXTYPE_SWITCH_MAIN, 0),
    NAMEDEF("OPTLEVEL", LEXTYPE_SWITCH_OPTLEVEL, 0),
    NAMEDEF("VERSION", LEXTYPE_SWITCH_VERSION, 0),
    NAMEDEF("ADDRESSING_MODE", LEXTYPE_SWITCH_ADDRMODE, 0),
    NAMEDEF("ENVIRONMENT", LEXTYPE_SWITCH_ENV, 0),
    NAMEDEF("ENTRY", LEXTYPE_SWITCH_ENTRY, 0),
    NAMEDEF("OTS", LEXTYPE_SWITCH_OTS, 0),
    NAMEDEF("OTS_LINKAGE", LEXTYPE_SWITCH_OTSLNKG, 0)
};

static strdesc_t linkage_kwd = STRDEF("LINKAGE");
static strdesc_t structure_kwd = STRDEF("STRUCTURE");

static lextype_t switch_types[] = {
    LEXTYPE_KWD_CODE,
    LEXTYPE_NAME_SW_TOGGLE_ON,
    LEXTYPE_NAME_SW_TOGGLE_OFF,
    LEXTYPE_DCL_STRUCTURE,
    LEXTYPE_DCL_LINKAGE,
    LEXTYPE_SWITCH_IDENT,
    LEXTYPE_SWITCH_LANGUAGE,
    LEXTYPE_SWITCH_LIST,
    LEXTYPE_SWITCH_MAIN,
    LEXTYPE_SWITCH_OPTLEVEL,
    LEXTYPE_SWITCH_VERSION,
    LEXTYPE_SWITCH_ADDRMODE,
    LEXTYPE_SWITCH_ENV,
    LEXTYPE_SWITCH_ENTRY,
    LEXTYPE_SWITCH_OTS,
    LEXTYPE_SWITCH_OTSLNKG
};

/*
 * switch_special_declare
 *
 * Declares one of the "special" switch names and provides
 * a handler for it.
 */
int
switch_special_declare (scopectx_t scope, lextype_t swtype,
                        switch_parse_handler_fn handler, void *ctx)
{
    name_t *np;
    struct switch_special_s *data;
    namedef_t *ndef;

    if (swtype != LEXTYPE_DCL_LINKAGE && swtype != LEXTYPE_DCL_STRUCTURE &&
        (swtype < LEXTYPE_SWITCH_MIN || swtype > LEXTYPE_SWITCH_MAX)) {
        return 0;
    }
    if (swtype == LEXTYPE_DCL_LINKAGE) {
        np = name_search(scope, linkage_kwd.ptr, linkage_kwd.len, 0);
    } else if (swtype == LEXTYPE_DCL_STRUCTURE) {
        np = name_search(scope, structure_kwd.ptr, structure_kwd.len, 0);
    } else {
        ndef = &special_switches[swtype-LEXTYPE_SWITCH_MIN];
        np = name_declare(scope, ndef, 0, 0, 0, 0);
    }

    if (np == 0) {
        return 0;
    }

    data = name_extraspace(np);
    data->handler = handler;
    data->ctx = ctx;
    return 1;

} /* switch_special_declare */

/*
 * toggle_declare
 *
 * Declares ON and OFF toggle names for switches or values.
 * Some toggle names may conflict with other keywords; the caller
 * is expected to know these and to pass null pointers in for
 * the ON or OFF name_t pointers, as needed.
 */
static int
toggle_declare (scopectx_t scope, int is_switch, strdesc_t *name,
                toggle_handler_fn handler, void *ctx, int togidx,
                name_t **onpp, name_t **offpp)
{
    name_t *onp, *offp;
    struct toggle_s *datap;
    char namebuf[NAME_SIZE];
    namedef_t ndef;

    memset(&ndef, 0, sizeof(ndef));
    ndef.lt = (is_switch ? LEXTYPE_NAME_SW_TOGGLE_OFF : LEXTYPE_NAME_TOGGLE_OFF);
    ndef.name = namebuf;
    ndef.namelen = (name->len > NAME_SIZE-3) ? NAME_SIZE-1 : name->len+2;
    namebuf[0] = 'N'; namebuf[1] = 'O';
    memcpy(namebuf+2, name->ptr, ndef.namelen-2);
    if (offpp != 0) {
        offp = name_declare(scope, &ndef, 0, 0, 0, &datap);
        if (offp == 0) {
            return 0;
        }
        datap->handler = handler;
        datap->ctx = ctx;
        datap->togindex = togidx;
        *offpp = offp;
    } else offp = 0;

    ndef.lt = (is_switch ? LEXTYPE_NAME_SW_TOGGLE_ON : LEXTYPE_NAME_TOGGLE_ON);
    ndef.name = namebuf + 2;
    ndef.namelen -= 2;
    if (onpp != 0) {
        onp = name_declare(scope, &ndef, 0, 0, 0, &datap);
        if (onp == 0) {
            if (offp != 0) name_free(offp);
            return 0;
        }
        datap->handler = handler;
        datap->ctx = ctx;
        datap->togindex = togidx;
        *onpp = onp;
    }
    return 1;

} /* toggle_declare */

int switch_toggle_declare (scopectx_t scope, strdesc_t *name, toggle_handler_fn handler,
                           void *ctx, int togidx, name_t **onpp, name_t **offpp) {
    return toggle_declare(scope, 1, name, handler, ctx, togidx, onpp, offpp);
}
int value_toggle_declare (scopectx_t scope, strdesc_t *name, toggle_handler_fn handler,
                           void *ctx, int togidx, name_t **onpp, name_t **offpp) {
    return toggle_declare(scope, 0, name, handler, ctx, togidx, onpp, offpp);
}

/*
 * value_toggle_dispatch
 *
 * Dispatch the handler for a value toggle.
 */
int
value_toggle_dispatch (parse_ctx_t pctx, void *ctx, lextype_t dcltype, name_t *togname)
{
    struct toggle_s *t = name_extraspace(togname);

    if (name_type(togname) != LEXTYPE_NAME_TOGGLE_OFF &&
        name_type(togname) != LEXTYPE_NAME_TOGGLE_ON) {
        return 0;
    }

    return (*t->handler)(pctx, ctx, t->togindex, dcltype, togname);

} /* value_toggle_dispatch */

/*
 * ignore_switch
 *
 * Accepts and ignores a switch that has no meaning for this
 * compiler's targets, e.g. ADDRESSING_MODE (...) or LANGUAGE (...).
 */
static int
ignore_switch (parse_ctx_t pctx, void *ctx, lextype_t dcltype, lexeme_t *swlex)
{
    if (parser_expect(pctx, QL_NORMAL, LEXTYPE_DELIM_LPAR, 0, 1)) {
        int depth = 1;
        while (depth > 0) {
            lextype_t lt = parser_next(pctx, QL_NORMAL, 0);
            if (lt == LEXTYPE_END || lt == LEXTYPE_NONE) break;
            if (lt == LEXTYPE_DELIM_LPAR) depth += 1;
            else if (lt == LEXTYPE_DELIM_RPAR) depth -= 1;
        }
    }
    return 1;

} /* ignore_switch */

/*
 * On-off switch state (LRM 19.2), for %SWITCHES. Kept per scope,
 * so a SWITCHES declaration lasts to the end of its block; a set
 * bit means the switch differs from its default.
 */
static strdesc_t onoff_names[SWITCH_K_COUNT] = {
    STRDEF("ERRS"), STRDEF("OPTIMIZE"), STRDEF("SAFE"), STRDEF("UNAMES"),
    STRDEF("ZIP"), STRDEF("CODE"), STRDEF("DEBUG")
};
#define SWITCH_M_DEFAULT_ON ((1U<<SWITCH_K_ERRS)|(1U<<SWITCH_K_OPTIMIZE)| \
                             (1U<<SWITCH_K_SAFE)|(1U<<SWITCH_K_CODE))

/*
 * switch_record
 *
 * Records the setting of an on-off switch in the current scope.
 */
void
switch_record (parse_ctx_t pctx, int idx, int on)
{
    scopectx_t scope = parser_scope_get(pctx);
    unsigned int sw = scope_switches(scope);
    unsigned int bit = 1U << idx;

    if (on == ((SWITCH_M_DEFAULT_ON & bit) != 0)) {
        sw &= ~bit;
    } else {
        sw |= bit;
    }
    scope_switches_set(scope, sw);

} /* switch_record */

/*
 * switch_test
 *
 * Returns 1 if the named setting (e.g. OPTIMIZE or NOZIP) is
 * current, 0 if not, -1 for an unknown switch.
 */
int
switch_test (scopectx_t scope, strdesc_t *name)
{
    unsigned int i, on;
    strdesc_t base = *name;
    int want = 1;

    if (base.len > 2 && base.ptr[0] == 'N' && base.ptr[1] == 'O') {
        base.ptr += 2;
        base.len -= 2;
        want = 0;
    }
    for (i = 0; i < SWITCH_K_COUNT; i++) {
        if (strings_eql(&base, &onoff_names[i])) {
            on = ((SWITCH_M_DEFAULT_ON ^ scope_switches(scope)) >> i) & 1;
            return (int) on == want;
        }
    }
    return -1;

} /* switch_test */

/*
 * Default linkage (LINKAGE switch), kept above the on-off bits
 */
#define SWITCH_V_LINKAGE 8
#define SWITCH_M_LINKAGE (3U<<SWITCH_V_LINKAGE)

void
switch_linkage_set (parse_ctx_t pctx, int lnktype)
{
    scopectx_t scope = parser_scope_get(pctx);
    scope_switches_set(scope, (scope_switches(scope) & ~SWITCH_M_LINKAGE) |
                       ((unsigned int) lnktype << SWITCH_V_LINKAGE));
}

int
switch_linkage (scopectx_t scope)
{
    return (int) ((scope_switches(scope) & SWITCH_M_LINKAGE) >> SWITCH_V_LINKAGE);
}

/*
 * linkage_switch
 *
 * LINKAGE (linkage-name): the default linkage.
 */
static int
linkage_switch (parse_ctx_t pctx, void *ctx, lextype_t dcltype, lexeme_t *swlex)
{
    lexeme_t *lex;

    if (!parser_expect(pctx, QL_NORMAL, LEXTYPE_DELIM_LPAR, 0, 1)) {
        log_signal(parser_logctx(pctx), parser_curpos(pctx), STC__DELIMEXP, "(");
        return 1;
    }
    if (parser_expect(pctx, QL_NORMAL, LEXTYPE_NAME_LINKAGE, &lex, 1)) {
        switch_linkage_set(pctx, linkage_type(lexeme_ctx_get(lex)));
        lexeme_free(parser_lexmemctx(pctx), lex);
    } else {
        log_signal(parser_logctx(pctx), parser_curpos(pctx), STC__LNKNAMEXP);
    }
    if (!parser_expect(pctx, QL_NORMAL, LEXTYPE_DELIM_RPAR, 0, 1)) {
        log_signal(parser_logctx(pctx), parser_curpos(pctx), STC__DELIMEXP, ")");
        parser_skip_to_delim(pctx, LEXTYPE_DELIM_RPAR);
    }
    return 1;

} /* linkage_switch */

/*
 * switch_linkage_declare
 *
 * Hooks up the LINKAGE switch; the LINKAGE keyword must exist.
 */
void
switch_linkage_declare (scopectx_t scope)
{
    switch_special_declare(scope, LEXTYPE_DCL_LINKAGE, linkage_switch, 0);

} /* switch_linkage_declare */

/*
 * record_toggle
 *
 * Handler for on-off switches that only need their state recorded.
 */
static int
record_toggle (parse_ctx_t pctx, void *ctx, int togidx, lextype_t dcltype, name_t *togname)
{
    switch_record(pctx, togidx, name_type(togname) == LEXTYPE_NAME_SW_TOGGLE_ON);
    return 1;

} /* record_toggle */

/*
 * switches_ignored_declare
 *
 * Declares the switches that are accepted but ignored.
 */
void
switches_ignored_declare (scopectx_t scope)
{
    static int toggles[] = {
        SWITCH_K_OPTIMIZE, SWITCH_K_SAFE, SWITCH_K_UNAMES, SWITCH_K_ZIP, SWITCH_K_DEBUG
    };
    name_t *onp, *offp;
    unsigned int i;

    for (i = 0; i < sizeof(toggles)/sizeof(toggles[0]); i++) {
        switch_toggle_declare(scope, &onoff_names[toggles[i]], record_toggle, 0,
                              toggles[i], &onp, &offp);
    }
    // CODE is also a keyword (PSECT CODE): declare only NOCODE here
    switch_toggle_declare(scope, &onoff_names[SWITCH_K_CODE], record_toggle, 0,
                          SWITCH_K_CODE, 0, &offp);
    switch_special_declare(scope, LEXTYPE_SWITCH_ADDRMODE, ignore_switch, 0);
    switch_special_declare(scope, LEXTYPE_SWITCH_LANGUAGE, ignore_switch, 0);
    switch_special_declare(scope, LEXTYPE_SWITCH_ENV, ignore_switch, 0);

} /* switches_ignored_declare */

/*
 * parse_switches
 *
 * Parses SWITCHES declarations and MODULE switches.
 *
 */
int
parse_switches (parse_ctx_t pctx, lextype_t decltype, lextype_t terminator)
{
    int i;
    lexeme_t *lex;
    name_t *np;

    while (1) {
        i = parser_expect_oneof(pctx, QL_NORMAL,
                                switch_types, sizeof(switch_types)/sizeof(switch_types[0]),
                                &lex, 1);
        if (i < 0) {
            log_signal(parser_logctx(pctx), parser_curpos(pctx), STC__SWITCHEXP);
            break;
        }
        np = lexeme_ctx_get(lex);
        if (lexeme_type(lex) == LEXTYPE_KWD_CODE) {
            switch_record(pctx, SWITCH_K_CODE, 1);
        } else if (np == 0) {
            log_signal(parser_logctx(pctx), parser_curpos(pctx), STC__SWITCHUNS,
                       lexeme_text(lex));
        } else {
            lextype_t lt;
            lt = lexeme_type(lex);
            if (lt == LEXTYPE_NAME_SW_TOGGLE_OFF || lt == LEXTYPE_NAME_SW_TOGGLE_ON) {
                struct toggle_s *t = name_extraspace(np);
                if (t->handler == 0) {
                    log_signal(parser_logctx(pctx), parser_curpos(pctx),
                               STC__SWITCHUNS, lexeme_text(lex));
                } else {
                    (*t->handler)(pctx, t->ctx, t->togindex, decltype, np);
                }
            } else {
                struct switch_special_s *s = name_extraspace(np);
                if (s->handler == 0) {
                    log_signal(parser_logctx(pctx), parser_curpos(pctx),
                               STC__SWITCHUNS, lexeme_text(lex));
                } else {
                    (*s->handler)(pctx, s->ctx, decltype, lex);
                }
            }
        }
        lexeme_free(parser_lexmemctx(pctx), lex);
        if (parser_expect(pctx, QL_NORMAL, terminator, 0, 1)) {
            return 1;
        }
        if (!parser_expect(pctx, QL_NORMAL, LEXTYPE_DELIM_COMMA, 0, 1)) {
            log_signal(parser_logctx(pctx), parser_curpos(pctx), STC__DELIMEXP, ",");
        }

    } /* while 1 */

    // If we broke out of the loop, there was some kind of error, try to recover
    parser_skip_to_delim(pctx, terminator);
    return 1;

} /* parse_switches */
