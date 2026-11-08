/*
 *++
 * blissrt.c - BLISS run-time support: condition handling (LRM 17).
 *
 * A routine with an ENABLE declaration puts a frame in its stack and
 * links it into a per-thread chain. SIGNAL searches the chain from the
 * most recent frame, calling each handler with the signal, mechanism
 * and enable vectors (all counted vectors). A true value from a handler
 * continues the signaler, false resignals. SETUNWIND in a handler
 * terminates the routines down to the handler's establisher: their
 * handlers are called with SS$_UNWIND, then the establisher returns
 * (via longjmp) with the value left in the mechanism vector.
 *--
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

typedef long (*bliss_handler_t)(long *sig, long *mech, long *enbl);

struct sigctx;

// The compiler allocates LLVMGEN_K_ENBLFRAME bytes, 16-aligned
struct bliss_frame {
    jmp_buf             jb;         // first: the establisher's setjmp
    struct bliss_frame *prev;
    bliss_handler_t     handler;
    long               *enbl;
    long                retval;     // establisher's value after an unwind
    struct sigctx      *visitor;    // signal whose search called the handler
};
_Static_assert(sizeof(struct bliss_frame) <= 512, "frame too big");

// A signal being processed
struct sigctx {
    struct sigctx      *prev;
    struct bliss_frame *cur;        // frame whose handler is running
    struct bliss_frame *unwind;     // set by SETUNWIND
};

#define SS_UNWIND   2336            // VMS SS$_UNWIND, %X'920'
#define STS_SEVERE  4
__asm__(".globl \"SS$_UNWIND\"\n.set \"SS$_UNWIND\", 2336");

static __thread struct bliss_frame *frames;
static __thread struct sigctx *signals;

void
bliss_establish (struct bliss_frame *f, bliss_handler_t handler, long *enbl)
{
    f->prev = frames;
    f->handler = handler;
    f->enbl = enbl;
    f->retval = 0;
    f->visitor = 0;
    frames = f;
}

void
bliss_revert (struct bliss_frame *f)
{
    frames = f->prev;
}

long
bliss_unwound_value (struct bliss_frame *f)
{
    return f->retval;
}

long
bliss_setunwind (void)
{
    if (signals == 0 || signals->cur == 0 || signals->unwind != 0) {
        return 0;   // invalid: not in a handler, or already requested
    }
    signals->unwind = signals->cur;
    return 1;       // SS$_NORMAL
}

/*
 * unwind
 *
 * Terminates the routines down to and including the establisher 'est',
 * calling their handlers with SS$_UNWIND, and resumes after the call to
 * the establisher.
 */
static void
unwind (struct bliss_frame *est, long *mech)
{
    long usig[2] = { 1, SS_UNWIND };
    struct bliss_frame *f;
    struct sigctx *s;

    while ((f = frames) != 0) {
        if (f->handler != 0) {
            f->handler(usig, mech, f->enbl);
        }
        frames = f->prev;
        if (f == est) break;
    }
    est->retval = mech[3];

    // Signals whose processing is abandoned (their C frames are
    // deeper in the stack than the establisher's)
    while (signals != 0 && (char *) signals < (char *) est) {
        signals = signals->prev;
    }
    for (f = frames; f != 0; f = f->prev) {
        for (s = signals; s != 0 && s != f->visitor; s = s->prev);
        if (s == 0) f->visitor = 0;
    }
    longjmp(est->jb, 1);
}

/*
 * bliss_signal
 *
 * SIGNAL (stop = 0) and SIGNAL_STOP (stop = 1). 'sig' is the signal
 * vector: count, condition value, parameters.
 */
long
bliss_signal (long *sig, long stop)
{
    long mech[5] = { 4, 0, 0, 0, 0 };   // count, frame, depth, R0, R1
    struct sigctx ctx = { signals, 0, 0 };
    struct bliss_frame *f;
    long depth = 0, r;
    int handled = 0;

    signals = &ctx;
    for (f = frames; f != 0; f = f->prev, depth++) {
        if (f->handler == 0 || f->visitor != 0) {
            continue;   // already called for a signal in progress
        }
        if (stop) {
            sig[1] = (sig[1] & ~7L) | STS_SEVERE;
        }
        mech[1] = (long) f;
        mech[2] = depth;
        f->visitor = &ctx;
        ctx.cur = f;
        r = f->handler(sig, mech, f->enbl);
        ctx.cur = 0;
        if (ctx.unwind != 0) {
            unwind(ctx.unwind, mech);
        }
        if ((r & 1) != 0 && !stop) {
            handled = 1;    // continue the signaler
            break;
        }
    }
    for (f = frames; f != 0; f = f->prev) {
        if (f->visitor == &ctx) f->visitor = 0;
    }
    signals = ctx.prev;
    if (handled) {
        return mech[3];
    }

    // No handler dealt with it: like the VMS catchall handler, report
    // the condition, and stop on a severe error.
    {
        static const char sevchar[] = "WSEIF???";
        long cond = (stop ? (sig[1] & ~7L) | STS_SEVERE : sig[1]);
        long i;
        fprintf(stderr, "%%BLISS-%c-SIGNAL, condition %%X'%lX'",
                sevchar[cond & 7], (unsigned long) cond);
        for (i = 2; i <= sig[0]; i++) {
            fprintf(stderr, "%s%ld", (i == 2 ? ", parameters " : ", "), sig[i]);
        }
        fputc('\n', stderr);
        if ((cond & 7) == STS_SEVERE) {
            exit(EXIT_FAILURE);
        }
    }
    return mech[3];
}
