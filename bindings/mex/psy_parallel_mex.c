/* psy_parallel_mex.c - MATLAB/Octave MEX binding for psy_parallel.h
 *
 * Command-dispatch interface in the style of widmann's ppdev-mex: the first
 * argument is a command string, the second (when needed) is an opaque uint64
 * port handle returned by 'open'.
 *
 *     h = psy_parallel_mex('open')                 % platform defaults
 *     h = psy_parallel_mex('open', '/dev/parport1')% ppdev device (Linux)
 *     h = psy_parallel_mex('open', 'direct', 888)  % raw x86 I/O @ base (root)
 *     h = psy_parallel_mex('open', 'inpout', 888)  % Windows inpout @ base
 *     % optional trailing struct tunes the async-worker RT reservation (ns):
 *     h = psy_parallel_mex('open', struct('runtime_ns',5e5,'deadline_ns',2e6))
 *
 *     psy_parallel_mex('write',      h, value)     % data register (0..255)
 *     d = psy_parallel_mex('read',   h)            % read data register
 *     psy_parallel_mex('setdir',     h, isInput)   % data direction
 *     psy_parallel_mex('pulse',      h, value, usec)       % blocking
 *     psy_parallel_mex('pulseasync', h, value, usec)       % non-blocking
 *     s = psy_parallel_mex('status', h)            % status register
 *     c = psy_parallel_mex('control',h)            % read control register
 *     psy_parallel_mex('control',    h, value)     % write control register
 *     rt = psy_parallel_mex('sched', h)            % effective RT params struct
 *     psy_parallel_mex('close',      h)
 *
 * Build with build.m (MATLAB or Octave). See README.md.
 *
 * NOTE: the handle table is freed (and every open port closed) on 'clear mex'
 * via mexAtExit, so a forgotten close cannot leave an async worker thread
 * pointing at a freed port.
 */
/* clock_gettime/nanosleep/pthread_*: ensure the feature macro is set before any
 * header. Guarded because some mex toolchains (MATLAB) already define it on the
 * command line, while others (Octave mkoctfile) do not. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mex.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define PSY_PARALLEL_IMPLEMENTATION
#include "psy_parallel.h"

/* ---- handle table: tracks every open port for cleanup ------------------ */
typedef struct pp_node {
    psyp_port*      port;
    struct pp_node* next;
} pp_node;

static pp_node* g_ports = NULL;
static int      g_atexit_registered = 0;

static void pp_cleanup(void) {
    pp_node* n = g_ports;
    while (n) {
        pp_node* next = n->next;
        if (n->port) { psyp_close(n->port); free(n->port); }
        free(n);
        n = next;
    }
    g_ports = NULL;
}

static void pp_register(psyp_port* port) {
    pp_node* n = (pp_node*)malloc(sizeof(*n));
    n->port = port;
    n->next = g_ports;
    g_ports = n;
    if (!g_atexit_registered) { mexAtExit(pp_cleanup); g_atexit_registered = 1; }
}

static void pp_unregister(psyp_port* port) {
    pp_node** pp = &g_ports;
    while (*pp) {
        if ((*pp)->port == port) { pp_node* dead = *pp; *pp = dead->next; free(dead); return; }
        pp = &(*pp)->next;
    }
}

/* Resolve a handle arg to a registered port pointer; error if unknown. */
static psyp_port* pp_handle(const mxArray* a) {
    uint64_t bits;
    if (mxIsUint64(a))
        bits = *(const uint64_t*)mxGetData(a);
    else if (mxIsDouble(a) && mxGetNumberOfElements(a) == 1)
        bits = (uint64_t)mxGetScalar(a);
    else {
        mexErrMsgIdAndTxt("psy_parallel:handle", "handle must be a scalar uint64");
        return NULL;
    }
    psyp_port* port = (psyp_port*)(uintptr_t)bits;
    for (pp_node* n = g_ports; n; n = n->next)
        if (n->port == port) return port;
    mexErrMsgIdAndTxt("psy_parallel:handle", "invalid or closed port handle");
    return NULL;
}

static uint8_t pp_byte(const mxArray* a, const char* what) {
    if (!mxIsNumeric(a) || mxGetNumberOfElements(a) != 1)
        mexErrMsgIdAndTxt("psy_parallel:arg", "%s must be a numeric scalar", what);
    double d = mxGetScalar(a);
    if (d < 0 || d > 255)
        mexErrMsgIdAndTxt("psy_parallel:arg", "%s must be in 0..255", what);
    return (uint8_t)d;
}

static uint32_t pp_u32(const mxArray* a, const char* what) {
    if (!mxIsNumeric(a) || mxGetNumberOfElements(a) != 1)
        mexErrMsgIdAndTxt("psy_parallel:arg", "%s must be a numeric scalar", what);
    double d = mxGetScalar(a);
    if (d < 0) mexErrMsgIdAndTxt("psy_parallel:arg", "%s must be >= 0", what);
    return (uint32_t)d;
}

static mxArray* pp_scalar_u8(uint8_t v) {
    mxArray* a = mxCreateNumericMatrix(1, 1, mxUINT8_CLASS, mxREAL);
    *(uint8_t*)mxGetData(a) = v;
    return a;
}

static void check(psyp_port* port, int ok) {
    if (!ok) mexErrMsgIdAndTxt("psy_parallel:io", "%s", psyp_error(port));
}

/* Read an async-worker RT reservation from a struct with fields runtime_ns /
 * deadline_ns / period_ns (any subset; missing fields stay 0, which psyp_open
 * resolves -- a 0 period means "same as deadline"). */
static void pp_read_sched(const mxArray* s, psyp_desc* d) {
    const mxArray* f;
    if ((f = mxGetField(s, 0, "runtime_ns")))  d->sched.runtime_ns  = (uint64_t)mxGetScalar(f);
    if ((f = mxGetField(s, 0, "deadline_ns"))) d->sched.deadline_ns = (uint64_t)mxGetScalar(f);
    if ((f = mxGetField(s, 0, "period_ns")))   d->sched.period_ns   = (uint64_t)mxGetScalar(f);
}

/* ---- 'open' ------------------------------------------------------------ */
static void cmd_open(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[]) {
    psyp_desc desc;
    memset(&desc, 0, sizeof(desc));
    char* devbuf = NULL;

    /* Optional trailing async-worker RT reservation as a struct with fields
     * runtime_ns/deadline_ns/period_ns (matches the 'sched' query and the
     * 'list' return shape). A struct is unambiguous against the device string
     * and the scalar base address. psyp_open validates the values. */
    if (nrhs >= 2 && mxIsStruct(prhs[nrhs - 1])) {
        pp_read_sched(prhs[nrhs - 1], &desc);
        nrhs--; /* consume it before parsing device/mode/base */
    }

    if (nrhs >= 2) {
        if (!mxIsChar(prhs[1]))
            mexErrMsgIdAndTxt("psy_parallel:arg",
                "open: expected a device path, 'direct'/'inpout', or an RT-params struct");
        char* s = mxArrayToString(prhs[1]);
        if (strcmp(s, "direct") == 0) {
            desc.backend = PSYP_BACKEND_DIRECT;
            if (nrhs >= 3) desc.base_addr = (uint16_t)pp_u32(prhs[2], "base address");
        } else if (strcmp(s, "inpout") == 0) {
            desc.backend = PSYP_BACKEND_INPOUT;
            if (nrhs >= 3) desc.base_addr = (uint16_t)pp_u32(prhs[2], "base address");
        } else {
            /* treat the string as a ppdev device path */
            devbuf = s; s = NULL;
            desc.device = devbuf;
        }
        if (s) mxFree(s);
    }

    psyp_port* port = (psyp_port*)calloc(1, sizeof(*port));
    if (!port) mexErrMsgIdAndTxt("psy_parallel:mem", "out of memory");
    int ok = psyp_open(port, &desc);
    if (devbuf) mxFree(devbuf);
    if (!ok) {
        char msg[300];
        snprintf(msg, sizeof(msg), "%s", psyp_error(port));
        free(port);
        mexErrMsgIdAndTxt("psy_parallel:open", "%s", msg);
    }
    pp_register(port);

    mxArray* h = mxCreateNumericMatrix(1, 1, mxUINT64_CLASS, mxREAL);
    *(uint64_t*)mxGetData(h) = (uint64_t)(uintptr_t)port;
    plhs[0] = h;
    (void)nlhs;
}

void mexFunction(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[]) {
    if (nrhs < 1 || !mxIsChar(prhs[0]))
        mexErrMsgIdAndTxt("psy_parallel:usage", "first argument must be a command string");

    char* cmd = mxArrayToString(prhs[0]);

    if (strcmp(cmd, "open") == 0) {
        cmd_open(nlhs, plhs, nrhs, prhs);
        mxFree(cmd);
        return;
    }

    if (strcmp(cmd, "list") == 0) {
        int n = psyp_list_ports(NULL, 0);
        if (n < 0) n = 0;
        psyp_port_info* v = NULL;
        if (n > 0) {
            v = (psyp_port_info*)mxMalloc((size_t)n * sizeof(*v));
            int got = psyp_list_ports(v, n);
            if (got < n) n = got;
        }
        const char* fields[] = { "name", "backend", "base_addr" };
        mxArray* s = mxCreateStructMatrix(n ? 1 : 0, n ? (size_t)n : 0, 3, fields);
        for (int i = 0; i < n; i++) {
            mxSetField(s, i, "name", mxCreateString(v[i].name));
            mxSetField(s, i, "backend", mxCreateDoubleScalar((double)v[i].backend));
            mxSetField(s, i, "base_addr", mxCreateDoubleScalar((double)v[i].base_addr));
        }
        if (v) mxFree(v);
        plhs[0] = s;
        mxFree(cmd);
        return;
    }

    /* all other commands take a handle as the 2nd argument */
    if (nrhs < 2) mexErrMsgIdAndTxt("psy_parallel:usage", "'%s' needs a port handle", cmd);
    psyp_port* port = pp_handle(prhs[1]);

    if (strcmp(cmd, "write") == 0) {
        if (nrhs < 3) mexErrMsgIdAndTxt("psy_parallel:usage", "write needs a value");
        check(port, psyp_write_data(port, pp_byte(prhs[2], "value")));

    } else if (strcmp(cmd, "read") == 0) {
        uint8_t v = psyp_read_data(port);
        check(port, psyp_error(port)[0] == '\0');
        plhs[0] = pp_scalar_u8(v);

    } else if (strcmp(cmd, "status") == 0) {
        uint8_t v = psyp_read_status(port);
        check(port, psyp_error(port)[0] == '\0');
        plhs[0] = pp_scalar_u8(v);

    } else if (strcmp(cmd, "sched") == 0) {
        /* effective async-worker RT params as a struct (ns) */
        const char* fields[] = { "runtime_ns", "deadline_ns", "period_ns" };
        mxArray* s = mxCreateStructMatrix(1, 1, 3, fields);
        mxSetField(s, 0, "runtime_ns",  mxCreateDoubleScalar((double)port->sched.runtime_ns));
        mxSetField(s, 0, "deadline_ns", mxCreateDoubleScalar((double)port->sched.deadline_ns));
        mxSetField(s, 0, "period_ns",   mxCreateDoubleScalar((double)port->sched.period_ns));
        plhs[0] = s;

    } else if (strcmp(cmd, "control") == 0) {
        if (nrhs >= 3) {                       /* write control */
            check(port, psyp_write_control(port, pp_byte(prhs[2], "value")));
        } else {                               /* read control */
            uint8_t v = psyp_read_control(port);
            check(port, psyp_error(port)[0] == '\0');
            plhs[0] = pp_scalar_u8(v);
        }

    } else if (strcmp(cmd, "setdir") == 0) {
        if (nrhs < 3) mexErrMsgIdAndTxt("psy_parallel:usage", "setdir needs isInput");
        /* mxGetScalar yields 0/1 for logical and numeric alike. */
        check(port, psyp_set_data_dir(port, mxGetScalar(prhs[2]) != 0));

    } else if (strcmp(cmd, "pulse") == 0) {
        if (nrhs < 4) mexErrMsgIdAndTxt("psy_parallel:usage", "pulse needs value, usec");
        check(port, psyp_pulse(port, pp_byte(prhs[2], "value"), pp_u32(prhs[3], "usec")));

    } else if (strcmp(cmd, "pulseasync") == 0) {
        if (nrhs < 4) mexErrMsgIdAndTxt("psy_parallel:usage", "pulseasync needs value, usec");
        check(port, psyp_pulse_async(port, pp_byte(prhs[2], "value"), pp_u32(prhs[3], "usec")));

    } else if (strcmp(cmd, "close") == 0) {
        psyp_close(port);
        pp_unregister(port);
        free(port);

    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "unknown command '%s'", cmd);
        mxFree(cmd);
        mexErrMsgIdAndTxt("psy_parallel:usage", "%s", msg);
    }
    mxFree(cmd);
}
