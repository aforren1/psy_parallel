/* psy_parallel.h - v0.1 - public domain single-header parallel-port library
 *
 *   A small, dependency-free C library for reading and writing the PC
 *   parallel port (LPT). Built for low-latency 8-bit trigger output of the
 *   kind used in psychophysics / EEG / electrophysiology rigs, where a byte
 *   written to the data lines is latched onto a recording system.
 *
 *   Inspired by:
 *     - pyparallel    (https://github.com/pyparallel/pyparallel)
 *     - ppdev-mex     (https://github.com/widmann/ppdev-mex)
 *   and written in the single-header style of the stb / sokol libraries.
 *
 *   Targets Windows and Linux.
 *
 *   ---------------------------------------------------------------------
 *   USAGE
 *   ---------------------------------------------------------------------
 *   Do this:
 *
 *       #define PSY_PARALLEL_IMPLEMENTATION
 *
 *   in *one* C or C++ file before including this header to create the
 *   implementation. Every other file just includes the header normally.
 *
 *       #define PSY_PARALLEL_IMPLEMENTATION
 *       #include "psy_parallel.h"
 *
 *       psyp_port pp;
 *       if (!psyp_open(&pp, &(psyp_desc){0})) {       // sensible defaults
 *           fprintf(stderr, "open failed: %s\n", psyp_error(&pp));
 *           return 1;
 *       }
 *       psyp_write_data(&pp, 0x55);                   // raise a trigger
 *       psyp_pulse(&pp, 0x55, 2000);                  // 0x55 for 2 ms, then 0
 *       psyp_close(&pp);
 *
 *   ---------------------------------------------------------------------
 *   BACKENDS
 *   ---------------------------------------------------------------------
 *   Linux:
 *     PSYP_BACKEND_PPDEV   - the kernel ppdev character device
 *                            (/dev/parport0). The portable, recommended
 *                            path. Needs read/write on the device (add your
 *                            user to the `lp` group, or set a udev rule).
 *                            The kernel `lp` printer driver must not hold
 *                            the port (`sudo rmmod lp`, or blacklist it).
 *     PSYP_BACKEND_DIRECT  - raw x86 port I/O via ioperm()+outb/inb. Lowest
 *                            latency, but requires root (CAP_SYS_RAWIO) and
 *                            x86/x86_64. You must know the I/O base address.
 *
 *   Windows:
 *     PSYP_BACKEND_INPOUT  - loads inpout32.dll / inpoutx64.dll at runtime
 *                            (https://www.highrez.co.uk/downloads/inpout32/)
 *                            and calls its Out32/Inp32 entry points. Modern
 *                            Windows forbids user-mode port I/O, so a kernel
 *                            helper driver such as InpOut is required. The
 *                            DLL self-installs its driver on first use and
 *                            needs administrator rights to do so.
 *
 *   PSYP_BACKEND_DEFAULT picks ppdev on Linux and inpout on Windows.
 *
 *   ---------------------------------------------------------------------
 *   REGISTER MODEL
 *   ---------------------------------------------------------------------
 *   A standard (SPP) parallel port exposes three byte-wide registers at
 *   consecutive I/O addresses, base+0/+1/+2:
 *
 *     Data    (base+0, R/W) 8 output bits -> pins 2..9. The trigger byte.
 *     Status  (base+1, R)   5 input bits  -> pins 15,13,12,10,11.
 *     Control (base+2, R/W) 4 output bits -> pins 1,14,16,17 + dir bit.
 *
 *   Some pins are inverted in hardware relative to their register bit
 *   (Busy, nStrobe, nAutoFeed, nSelectIn). This library returns/accepts the
 *   raw register bits, matching pyparallel; use the PSYP_STATUS_* and
 *   PSYP_CONTROL_* masks and mind the hardware inversion noted below.
 *
 *   ---------------------------------------------------------------------
 *   LICENSE: public domain / MIT-0, see end of file.
 */
#ifndef PSY_PARALLEL_H_INCLUDED
#define PSY_PARALLEL_H_INCLUDED

/* Feature-test macro for nanosleep() in the Linux implementation. Defined
 * here, before the first system header, so it takes effect when this header
 * is the implementation translation unit's first include (the usual case).
 * If you include other system headers before this one in your implementation
 * file, define _POSIX_C_SOURCE>=199309L (or _DEFAULT_SOURCE) yourself. */
#if defined(PSY_PARALLEL_IMPLEMENTATION) && defined(__linux__) && \
    !defined(_POSIX_C_SOURCE) && !defined(_GNU_SOURCE) && !defined(_DEFAULT_SOURCE)
    #define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PSYP_API
#define PSYP_API extern
#endif

/* Default I/O base addresses of the legacy ISA LPT ports. Add-in PCI/PCIe
 * cards usually live elsewhere; check your system (Linux: /proc/ioports;
 * Windows: Device Manager -> port -> Resources). */
#define PSYP_LPT1 0x378
#define PSYP_LPT2 0x278
#define PSYP_LPT3 0x3BC

/* Status register (base+1) bit masks. Read-only.
 * NOTE: Busy (bit 7) is inverted in hardware: the bit reads 0 while the
 * physical Busy line is asserted high. */
#define PSYP_STATUS_BUSY    0x80  /* pin 11, hardware-inverted */
#define PSYP_STATUS_ACK     0x40  /* pin 10, nAck             */
#define PSYP_STATUS_PAPER   0x20  /* pin 12, paper-out        */
#define PSYP_STATUS_SELECT  0x10  /* pin 13, select-in        */
#define PSYP_STATUS_ERROR   0x08  /* pin 15, nError/nFault    */

/* Control register (base+2) bit masks. Read/write.
 * NOTE: Strobe, AutoFeed and SelectIn are inverted in hardware relative to
 * their register bit; Init is not. PSYP_CONTROL_DIR=1 switches an enhanced
 * (PS/2/bidirectional) port's data lines to input. */
#define PSYP_CONTROL_STROBE   0x01  /* pin 1,  nStrobe,   hardware-inverted */
#define PSYP_CONTROL_AUTOFEED 0x02  /* pin 14, nAutoFeed, hardware-inverted */
#define PSYP_CONTROL_INIT     0x04  /* pin 16, nInit                        */
#define PSYP_CONTROL_SELECT   0x08  /* pin 17, nSelectIn, hardware-inverted */
#define PSYP_CONTROL_DIR      0x20  /* data direction: 1 = input/reverse    */

typedef enum psyp_backend {
    PSYP_BACKEND_DEFAULT = 0, /* ppdev on Linux, inpout on Windows */
    PSYP_BACKEND_PPDEV,       /* Linux kernel ppdev character device */
    PSYP_BACKEND_DIRECT,      /* Linux raw x86 port I/O (ioperm)     */
    PSYP_BACKEND_INPOUT       /* Windows inpout32/inpoutx64 DLL      */
} psyp_backend;

/* Open description. Zero-initialize it and only set what you need; missing
 * fields fall back to sensible defaults inside psyp_open(). */
typedef struct psyp_desc {
    psyp_backend backend;   /* default: PSYP_BACKEND_DEFAULT          */
    const char*  device;    /* ppdev path, default "/dev/parport0"    */
    uint16_t     base_addr; /* direct/inpout base, default PSYP_LPT1   */
    bool         exclusive; /* ppdev: claim the port exclusively (PPEXCL) */
} psyp_desc;

/* Port handle. Allocate it (stack/heap), pass to psyp_open(), and treat the
 * fields as opaque. */
typedef struct psyp_port {
    psyp_backend backend;
    uint16_t     base_addr;
    bool         is_open;
    char         error[256];
#if defined(_WIN32)
    void* dll;     /* HMODULE of the inpout DLL */
    void* out_fn;  /* Out32 entry point         */
    void* inp_fn;  /* Inp32 entry point         */
#else
    int  fd;       /* ppdev file descriptor (-1 if direct/closed) */
    bool claimed;  /* ppdev port claimed                          */
    bool direct;   /* using ioperm()+outb/inb                     */
#endif
    void* async;   /* lazily-created async-pulse worker, or NULL  */
} psyp_port;

/* One detected parallel port, as returned by psyp_list_ports(). */
typedef struct psyp_port_info {
    char         name[64];  /* openable identifier: ppdev path (Linux) or
                             * "LPTn" (Windows). Pass to psyp_desc.device on
                             * Linux; on Windows it is informational. */
    psyp_backend backend;   /* backend this entry was discovered for */
    uint16_t     base_addr; /* I/O base if known, else 0 */
} psyp_port_info;

/* --- discovery ---------------------------------------------------------- */

/* Enumerate parallel ports the system exposes, without opening any. Writes up
 * to `max` entries into `out` (pass out=NULL/max=0 to just count) and returns
 * the total number found, which may exceed `max`.
 *
 *   Linux:   scans /dev/parport0../dev/parport15 (the ppdev nodes); entries
 *            carry the device path and PSYP_BACKEND_PPDEV. For raw DIRECT
 *            access, find base addresses in /proc/ioports instead.
 *   Windows: lists LPT* DOS devices via QueryDosDevice; entries carry the name
 *            and PSYP_BACKEND_INPOUT with base_addr 0 (supply the real base
 *            address yourself via psyp_desc.base_addr).
 *
 * Typical use:
 *   int n = psyp_list_ports(NULL, 0);
 *   psyp_port_info* v = malloc(n * sizeof *v);
 *   psyp_list_ports(v, n);
 */
PSYP_API int psyp_list_ports(psyp_port_info* out, int max);

/* --- lifecycle ---------------------------------------------------------- */

/* Open a parallel port per `desc` (NULL = all defaults). Returns true on
 * success; on failure returns false and leaves a message in psyp_error(). */
PSYP_API bool psyp_open(psyp_port* p, const psyp_desc* desc);

/* Release and close the port. Safe to call on a zeroed or already-closed
 * handle. */
PSYP_API void psyp_close(psyp_port* p);

/* Last error message for this handle ("" if none). */
PSYP_API const char* psyp_error(const psyp_port* p);

/* True if the handle currently owns an open port. */
PSYP_API bool psyp_is_open(const psyp_port* p);

/* --- data register (base+0) -------------------------------------------- */

/* Write the 8 data bits. This is the trigger-output workhorse. */
PSYP_API bool psyp_write_data(psyp_port* p, uint8_t value);

/* Read the data register back. On a forward (output) port this returns the
 * last value latched; on a bidirectional port in input mode it reads the
 * external lines. */
PSYP_API uint8_t psyp_read_data(psyp_port* p);

/* Set data-line direction on a bidirectional port: input=true switches the
 * data pins to read mode, input=false (default) drives them as outputs. */
PSYP_API bool psyp_set_data_dir(psyp_port* p, bool input);

/* Write `value` to the data lines, hold for `usec` microseconds, then write
 * 0. The classic fixed-width trigger pulse. Timing is best-effort and bounded
 * by OS scheduling; expect microsecond-to-millisecond jitter on a non-RT
 * kernel. */
PSYP_API bool psyp_pulse(psyp_port* p, uint8_t value, uint32_t usec);

/* Non-blocking pulse. Writes `value` to the data lines immediately on the
 * calling thread (so the trigger ONSET is not delayed by thread wake-up),
 * then returns at once; a dedicated real-time worker thread writes the
 * trailing 0 after `usec` microseconds.
 *
 * The worker is created on first use; on that first call the onset is written
 * before the worker is created, so even the first trigger's onset is not
 * delayed by thread spin-up. The worker runs at elevated scheduling priority
 * for low timing jitter: SCHED_DEADLINE (falling back to SCHED_FIFO, then
 * normal) on Linux, THREAD_PRIORITY_TIME_CRITICAL with a high-resolution
 * waitable timer on Windows. Acquiring a real-time policy needs privileges
 * (CAP_SYS_NICE / running elevated); psyp_pulse_async still works without
 * them, just with more jitter.
 *
 * If a new async pulse is issued while one is still pending, the most
 * recently requested off-time governs the single trailing edge (the data
 * register is shared, so overlapping triggers cannot carry distinct values
 * simultaneously); the worker re-evaluates immediately, whether the new
 * deadline falls earlier or later than the old one.
 *
 * Returns false if the worker could not be started (e.g. built with
 * PSYP_NO_THREADS) or the port is not open; see psyp_error(). Compile with
 * -DPSYP_NO_THREADS to drop threading support and this function entirely. */
PSYP_API bool psyp_pulse_async(psyp_port* p, uint8_t value, uint32_t usec);

/* --- status register (base+1, read-only) ------------------------------- */

/* Read the raw status byte. Combine with PSYP_STATUS_* masks. */
PSYP_API uint8_t psyp_read_status(psyp_port* p);

/* Convenience: test a single status line (raw register bit). */
PSYP_API bool psyp_get_status_bit(psyp_port* p, uint8_t mask);

/* --- control register (base+2) ----------------------------------------- */

/* Read / write the raw control byte. Combine with PSYP_CONTROL_* masks. */
PSYP_API uint8_t psyp_read_control(psyp_port* p);
PSYP_API bool    psyp_write_control(psyp_port* p, uint8_t value);

/* Set or clear individual control bits without disturbing the others. */
PSYP_API bool psyp_set_control_bit(psyp_port* p, uint8_t mask, bool on);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PSY_PARALLEL_H_INCLUDED */

/* ======================================================================= *
 *                            IMPLEMENTATION                               *
 * ======================================================================= */
#ifdef PSY_PARALLEL_IMPLEMENTATION
#ifndef PSY_PARALLEL_IMPLEMENTATION_GUARD
#define PSY_PARALLEL_IMPLEMENTATION_GUARD

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

/* Pick the concrete platform once. */
#if defined(_WIN32)
    #define PSYP__WINDOWS 1
#elif defined(__linux__)
    #define PSYP__LINUX 1
    /* Raw port I/O (outb/inb) only exists on x86. */
    #if defined(__i386__) || defined(__x86_64__)
        #define PSYP__HAVE_IOPORT 1
    #endif
#else
    #error "psy_parallel: unsupported platform (need Windows or Linux)"
#endif

/* --- shared helpers ----------------------------------------------------- */

static void psyp__set_error(psyp_port* p, const char* fmt, ...) {
    if (!p) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->error, sizeof(p->error), fmt, ap);
    va_end(ap);
}

static void psyp__clear_error(psyp_port* p) {
    if (p) p->error[0] = '\0';
}

const char* psyp_error(const psyp_port* p) {
    return p ? p->error : "null port handle";
}

bool psyp_is_open(const psyp_port* p) {
    return p && p->is_open;
}

#ifndef PSYP_NO_THREADS
/* defined in the async-pulse section below; called from psyp_close() */
static void psyp__async_stop(psyp_port* p);
#endif

/* ======================================================================= *
 *  LINUX
 * ======================================================================= */
#if defined(PSYP__LINUX)

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/ppdev.h>
#include <linux/parport.h>
#if defined(PSYP__HAVE_IOPORT)
    #include <sys/io.h>
#endif

static bool psyp__nsleep(uint32_t usec) {
    struct timespec ts;
    ts.tv_sec  = usec / 1000000u;
    ts.tv_nsec = (long)(usec % 1000000u) * 1000L;
    /* loop to absorb signal interruptions */
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
    return true;
}

bool psyp_open(psyp_port* p, const psyp_desc* desc) {
    if (!p) return false;
    psyp_desc d;
    memset(&d, 0, sizeof(d));
    if (desc) d = *desc;
    memset(p, 0, sizeof(*p));
    p->fd = -1;

    p->backend = d.backend;
    if (p->backend == PSYP_BACKEND_DEFAULT) p->backend = PSYP_BACKEND_PPDEV;
    if (p->backend == PSYP_BACKEND_INPOUT) {
        psyp__set_error(p, "inpout backend is Windows-only");
        return false;
    }
    p->base_addr = d.base_addr ? d.base_addr : PSYP_LPT1;

    if (p->backend == PSYP_BACKEND_PPDEV) {
        const char* path = d.device ? d.device : "/dev/parport0";
        int fd = open(path, O_RDWR);
        if (fd < 0) {
            psyp__set_error(p, "open(%s): %s", path, strerror(errno));
            return false;
        }
        /* Claim the port from the kernel before any read/write. */
        if (d.exclusive) {
            /* PPEXCL must precede PPCLAIM. Failure here isn't fatal on its
             * own; PPCLAIM below is the real gate. */
            ioctl(fd, PPEXCL);
        }
        if (ioctl(fd, PPCLAIM) != 0) {
            psyp__set_error(p, "PPCLAIM(%s): %s (is the `lp` module loaded "
                               "or another process holding the port?)",
                            path, strerror(errno));
            close(fd);
            return false;
        }
        p->fd = fd;
        p->claimed = true;
        p->direct = false;
        p->is_open = true;
        return true;
    }

    if (p->backend == PSYP_BACKEND_DIRECT) {
#if defined(PSYP__HAVE_IOPORT)
        /* Request access to the 3 SPP registers at base. Needs root. */
        if (ioperm(p->base_addr, 3, 1) != 0) {
            psyp__set_error(p, "ioperm(0x%X): %s (need root/CAP_SYS_RAWIO)",
                            p->base_addr, strerror(errno));
            return false;
        }
        p->direct = true;
        p->is_open = true;
        return true;
#else
        psyp__set_error(p, "direct port I/O requires x86/x86_64");
        return false;
#endif
    }

    psyp__set_error(p, "unknown backend %d", (int)p->backend);
    return false;
}

void psyp_close(psyp_port* p) {
    if (!p || !p->is_open) return;
#ifndef PSYP_NO_THREADS
    psyp__async_stop(p);
#endif
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        ioperm(p->base_addr, 3, 0);
#endif
    } else if (p->fd >= 0) {
        if (p->claimed) ioctl(p->fd, PPRELEASE);
        close(p->fd);
    }
    p->fd = -1;
    p->claimed = false;
    p->is_open = false;
}

bool psyp_write_data(psyp_port* p, uint8_t value) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    psyp__clear_error(p);
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        outb(value, p->base_addr);
        return true;
#endif
    }
    if (ioctl(p->fd, PPWDATA, &value) != 0) {
        psyp__set_error(p, "PPWDATA: %s", strerror(errno));
        return false;
    }
    return true;
}

#ifndef PSYP_NO_THREADS
/* Hardware data write that does NOT touch the shared p->error buffer, so the
 * async worker thread can drop the trailing edge without racing main-thread
 * API calls (which also read/write p->error). */
static bool psyp__write_data_raw(psyp_port* p, uint8_t value) {
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        outb(value, p->base_addr);
        return true;
#endif
    }
    return ioctl(p->fd, PPWDATA, &value) == 0;
}
#endif

uint8_t psyp_read_data(psyp_port* p) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return 0; }
    psyp__clear_error(p);
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        return inb(p->base_addr);
#endif
    }
    unsigned char v = 0;
    if (ioctl(p->fd, PPRDATA, &v) != 0)
        psyp__set_error(p, "PPRDATA: %s", strerror(errno));
    return (uint8_t)v;
}

bool psyp_set_data_dir(psyp_port* p, bool input) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    psyp__clear_error(p);
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        uint8_t c = inb((uint16_t)(p->base_addr + 2));
        c = input ? (c | PSYP_CONTROL_DIR) : (c & (uint8_t)~PSYP_CONTROL_DIR);
        outb(c, (uint16_t)(p->base_addr + 2));
        return true;
#endif
    }
    int dir = input ? 1 : 0; /* PPDATADIR: 0 = forward/out, 1 = reverse/in */
    if (ioctl(p->fd, PPDATADIR, &dir) != 0) {
        psyp__set_error(p, "PPDATADIR: %s", strerror(errno));
        return false;
    }
    return true;
}

uint8_t psyp_read_status(psyp_port* p) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return 0; }
    psyp__clear_error(p);
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        return inb((uint16_t)(p->base_addr + 1));
#endif
    }
    unsigned char v = 0;
    if (ioctl(p->fd, PPRSTATUS, &v) != 0)
        psyp__set_error(p, "PPRSTATUS: %s", strerror(errno));
    return (uint8_t)v;
}

uint8_t psyp_read_control(psyp_port* p) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return 0; }
    psyp__clear_error(p);
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        return inb((uint16_t)(p->base_addr + 2));
#endif
    }
    unsigned char v = 0;
    if (ioctl(p->fd, PPRCONTROL, &v) != 0)
        psyp__set_error(p, "PPRCONTROL: %s", strerror(errno));
    return (uint8_t)v;
}

bool psyp_write_control(psyp_port* p, uint8_t value) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    psyp__clear_error(p);
    if (p->direct) {
#if defined(PSYP__HAVE_IOPORT)
        outb(value, (uint16_t)(p->base_addr + 2));
        return true;
#endif
    }
    if (ioctl(p->fd, PPWCONTROL, &value) != 0) {
        psyp__set_error(p, "PPWCONTROL: %s", strerror(errno));
        return false;
    }
    return true;
}

int psyp_list_ports(psyp_port_info* out, int max) {
    int count = 0;
    for (int i = 0; i < 16; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/parport%d", i);
        if (access(path, F_OK) != 0) continue;
        if (out && count < max) {
            psyp_port_info* e = &out[count];
            memset(e, 0, sizeof(*e));
            snprintf(e->name, sizeof(e->name), "%s", path);
            e->backend = PSYP_BACKEND_PPDEV;
            e->base_addr = 0;
        }
        count++;
    }
    return count;
}

#endif /* PSYP__LINUX */

/* ======================================================================= *
 *  WINDOWS  (inpout32 / inpoutx64)
 * ======================================================================= */
#if defined(PSYP__WINDOWS)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef void  (__stdcall *psyp__out32_t)(short, short);
typedef short (__stdcall *psyp__inp32_t)(short);

static bool psyp__nsleep(uint32_t usec) {
    /* Sub-millisecond busy-wait on the high-res performance counter; the
     * Win32 scheduler can't reliably sleep finer than ~1 ms. */
    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    double target = (double)usec * (double)freq.QuadPart / 1.0e6;
    do {
        QueryPerformanceCounter(&now);
    } while ((double)(now.QuadPart - start.QuadPart) < target);
    return true;
}

bool psyp_open(psyp_port* p, const psyp_desc* desc) {
    if (!p) return false;
    psyp_desc d;
    memset(&d, 0, sizeof(d));
    if (desc) d = *desc;
    memset(p, 0, sizeof(*p));

    p->backend = d.backend;
    if (p->backend == PSYP_BACKEND_DEFAULT) p->backend = PSYP_BACKEND_INPOUT;
    if (p->backend != PSYP_BACKEND_INPOUT) {
        psyp__set_error(p, "only the inpout backend is available on Windows");
        return false;
    }
    p->base_addr = d.base_addr ? d.base_addr : PSYP_LPT1;

    /* Prefer the 64-bit DLL when running as a 64-bit process. The caller can
     * place either DLL alongside the executable or in the search path. */
    HMODULE dll = NULL;
#if defined(_WIN64)
    dll = LoadLibraryA("inpoutx64.dll");
    if (!dll) dll = LoadLibraryA("inpout32.dll");
#else
    dll = LoadLibraryA("inpout32.dll");
#endif
    if (!dll) {
        psyp__set_error(p, "LoadLibrary inpout DLL failed (err %lu); install "
                           "InpOut from highrez.co.uk and ship the DLL",
                        (unsigned long)GetLastError());
        return false;
    }

    psyp__out32_t out_fn = (psyp__out32_t)(void*)GetProcAddress(dll, "Out32");
    psyp__inp32_t inp_fn = (psyp__inp32_t)(void*)GetProcAddress(dll, "Inp32");
    if (!out_fn || !inp_fn) {
        psyp__set_error(p, "inpout DLL missing Out32/Inp32 entry points");
        FreeLibrary(dll);
        return false;
    }

    p->dll = (void*)dll;
    p->out_fn = (void*)out_fn;
    p->inp_fn = (void*)inp_fn;
    p->is_open = true;
    return true;
}

void psyp_close(psyp_port* p) {
    if (!p || !p->is_open) return;
#ifndef PSYP_NO_THREADS
    psyp__async_stop(p);
#endif
    if (p->dll) FreeLibrary((HMODULE)p->dll);
    p->dll = NULL;
    p->out_fn = NULL;
    p->inp_fn = NULL;
    p->is_open = false;
}

static void psyp__out(psyp_port* p, uint16_t addr, uint8_t val) {
    ((psyp__out32_t)p->out_fn)((short)addr, (short)val);
}
static uint8_t psyp__in(psyp_port* p, uint16_t addr) {
    return (uint8_t)(((psyp__inp32_t)p->inp_fn)((short)addr) & 0xFF);
}

bool psyp_write_data(psyp_port* p, uint8_t value) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    psyp__clear_error(p);
    psyp__out(p, p->base_addr, value);
    return true;
}

#ifndef PSYP_NO_THREADS
/* Hardware data write that does NOT touch the shared p->error buffer (see the
 * Linux note); used by the async worker for the trailing edge. */
static bool psyp__write_data_raw(psyp_port* p, uint8_t value) {
    psyp__out(p, p->base_addr, value);
    return true;
}
#endif

uint8_t psyp_read_data(psyp_port* p) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return 0; }
    psyp__clear_error(p);
    return psyp__in(p, p->base_addr);
}

bool psyp_set_data_dir(psyp_port* p, bool input) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    psyp__clear_error(p);
    uint8_t c = psyp__in(p, (uint16_t)(p->base_addr + 2));
    c = input ? (c | PSYP_CONTROL_DIR) : (c & (uint8_t)~PSYP_CONTROL_DIR);
    psyp__out(p, (uint16_t)(p->base_addr + 2), c);
    return true;
}

uint8_t psyp_read_status(psyp_port* p) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return 0; }
    psyp__clear_error(p);
    return psyp__in(p, (uint16_t)(p->base_addr + 1));
}

uint8_t psyp_read_control(psyp_port* p) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return 0; }
    psyp__clear_error(p);
    return psyp__in(p, (uint16_t)(p->base_addr + 2));
}

bool psyp_write_control(psyp_port* p, uint8_t value) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    psyp__clear_error(p);
    psyp__out(p, (uint16_t)(p->base_addr + 2), value);
    return true;
}

int psyp_list_ports(psyp_port_info* out, int max) {
    int count = 0;
    char buf[8192];
    /* QueryDosDevice(NULL, ...) returns a double-NUL-terminated list of every
     * DOS device name; filter the LPT* entries. (kernel32, no extra import.) */
    DWORD n = QueryDosDeviceA(NULL, buf, (DWORD)sizeof(buf));
    if (n == 0) return 0;
    for (char* s = buf; *s; s += strlen(s) + 1) {
        if (strncmp(s, "LPT", 3) != 0) continue;
        if (out && count < max) {
            psyp_port_info* e = &out[count];
            memset(e, 0, sizeof(*e));
            snprintf(e->name, sizeof(e->name), "%s", s);
            e->backend = PSYP_BACKEND_INPOUT;
            e->base_addr = 0; /* unknown; supply via psyp_desc.base_addr */
        }
        count++;
    }
    return count;
}

#endif /* PSYP__WINDOWS */

/* ======================================================================= *
 *  ASYNC PULSE WORKER
 *
 *  A single long-lived worker thread per port handles the trailing edge of
 *  non-blocking pulses. The leading edge is written by the caller (in
 *  psyp_pulse_async) for minimal onset latency; the worker only waits until
 *  an absolute target time and then writes 0.
 *
 *  The worker keeps just one pending off-deadline. A pulse issued while one
 *  is pending updates the deadline, so the trailing 0 lands once, after the
 *  most recent request.
 * ======================================================================= */
#ifndef PSYP_NO_THREADS

#if defined(PSYP__LINUX)
/* ---- Linux: pthreads + SCHED_DEADLINE / SCHED_FIFO -------------------- */
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>

#ifndef SCHED_DEADLINE
#define SCHED_DEADLINE 6
#endif

/* syscall()'s prototype lives behind _DEFAULT_SOURCE in <unistd.h>; declare
 * it directly so we don't force a feature-test macro onto the user's TU. */
extern long syscall(long, ...);

/* glibc has no wrapper or type for sched_setattr; declare both ourselves. */
struct psyp__sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t  sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};

static int psyp__setattr(struct psyp__sched_attr* a) {
#if defined(SYS_sched_setattr)
    return (int)syscall(SYS_sched_setattr, 0, a, 0u);
#elif defined(__x86_64__)
    return (int)syscall(314 /* __NR_sched_setattr */, 0, a, 0u);
#else
    (void)a; errno = ENOSYS; return -1;
#endif
}

/* Raise the calling (worker) thread to a real-time policy. SCHED_DEADLINE
 * first: a sporadic task with a 1 ms budget inside a 10 ms period and a
 * matching 10 ms deadline -- generous enough for short trigger pulses while
 * staying admissible by the kernel's EDF acceptance test. Falls back to
 * SCHED_FIFO, then leaves the thread at its default policy. */
static void psyp__worker_realtime(void) {
    struct psyp__sched_attr a;
    memset(&a, 0, sizeof(a));
    a.size           = (uint32_t)sizeof(a);
    a.sched_policy   = SCHED_DEADLINE;
    a.sched_runtime  =  1000000ull; /*  1 ms */
    a.sched_deadline = 10000000ull; /* 10 ms */
    a.sched_period   = 10000000ull; /* 10 ms */
    if (psyp__setattr(&a) == 0) return;

    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (sp.sched_priority > 0)
        (void)pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    /* No privilege for either policy: run as a normal thread. */
}

typedef struct psyp__async {
    pthread_t       thread;
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    int             started; /* thread successfully created */
    int             quit;
    int             has_job;
    struct timespec off_at;  /* absolute CLOCK_MONOTONIC target */
    psyp_port*      port;
} psyp__async;

static int psyp__ts_before(const struct timespec* a, const struct timespec* b) {
    if (a->tv_sec != b->tv_sec) return a->tv_sec < b->tv_sec;
    return a->tv_nsec < b->tv_nsec;
}

static void* psyp__worker_main(void* arg) {
    psyp__async* w = (psyp__async*)arg;
    psyp__worker_realtime();

    /* The mutex is held across the whole loop except while blocked in
     * pthread_cond_*wait (which atomically releases it). Holding it around
     * every data write serializes the trailing edge with the caller's
     * leading edge in psyp__async_submit, so neither can stomp the other. */
    pthread_mutex_lock(&w->mtx);
    while (!w->quit) {
        if (!w->has_job) {
            pthread_cond_wait(&w->cv, &w->mtx);
            continue;
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (!psyp__ts_before(&now, &w->off_at)) {
            /* Deadline reached: drop the trailing edge. */
            psyp__write_data_raw(w->port, 0);
            w->has_job = 0;
            continue;
        }
        /* Wait until the deadline OR until a new submit / quit wakes us. The
         * cond uses CLOCK_MONOTONIC (set in psyp__async_start) to match
         * off_at. Either way we re-evaluate from the top, so a re-trigger
         * that moves the deadline earlier or later takes effect immediately. */
        pthread_cond_timedwait(&w->cv, &w->mtx, &w->off_at);
    }
    /* On quit, flush any pending trailing edge right away so the data lines
     * are never left asserted -- without waiting out the remaining width. */
    if (w->has_job) {
        psyp__write_data_raw(w->port, 0);
        w->has_job = 0;
    }
    pthread_mutex_unlock(&w->mtx);
    return NULL;
}

static bool psyp__async_start(psyp_port* p) {
    if (p->async) return true;
    psyp__async* w = (psyp__async*)calloc(1, sizeof(*w));
    if (!w) { psyp__set_error(p, "async: out of memory"); return false; }
    w->port = p;
    if (pthread_mutex_init(&w->mtx, NULL) != 0) { free(w); psyp__set_error(p, "async: mutex init"); return false; }
    /* The condvar must time out against CLOCK_MONOTONIC to match off_at. */
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    int cv_rc = pthread_cond_init(&w->cv, &ca);
    pthread_condattr_destroy(&ca);
    if (cv_rc != 0) { pthread_mutex_destroy(&w->mtx); free(w); psyp__set_error(p, "async: cond init"); return false; }
    if (pthread_create(&w->thread, NULL, psyp__worker_main, w) != 0) {
        pthread_cond_destroy(&w->cv); pthread_mutex_destroy(&w->mtx); free(w);
        psyp__set_error(p, "async: thread create: %s", strerror(errno));
        return false;
    }
    w->started = 1;
    p->async = w;
    return true;
}

static void psyp__async_stop(psyp_port* p) {
    psyp__async* w = (psyp__async*)p->async;
    if (!w) return;
    pthread_mutex_lock(&w->mtx);
    w->quit = 1;
    pthread_cond_signal(&w->cv);
    pthread_mutex_unlock(&w->mtx);
    if (w->started) pthread_join(w->thread, NULL);
    pthread_cond_destroy(&w->cv);
    pthread_mutex_destroy(&w->mtx);
    free(w);
    p->async = NULL;
}

static bool psyp__async_submit(psyp_port* p, uint8_t value, uint32_t usec) {
    psyp__async* w = (psyp__async*)p->async;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t ns = now.tv_nsec + (uint64_t)(usec % 1000000u) * 1000ull;
    time_t   s  = now.tv_sec + (time_t)(usec / 1000000u) + (time_t)(ns / 1000000000ull);
    long     n  = (long)(ns % 1000000000ull);
    pthread_mutex_lock(&w->mtx);
    /* Leading edge under the lock so it can't be stomped by the worker's
     * trailing-edge write of an in-flight pulse. */
    bool ok = psyp_write_data(p, value);
    w->off_at.tv_sec = s;
    w->off_at.tv_nsec = n;
    w->has_job = 1;
    pthread_cond_signal(&w->cv);
    pthread_mutex_unlock(&w->mtx);
    return ok;
}

#elif defined(PSYP__WINDOWS)
/* ---- Windows: Win32 thread + waitable timer + TIME_CRITICAL ----------- */

typedef struct psyp__async {
    HANDLE             thread;
    HANDLE             timer;     /* high-resolution waitable timer */
    HANDLE            wakeup;     /* auto-reset event: a job is pending */
    CRITICAL_SECTION   cs;
    volatile LONG      quit;
    volatile LONG      has_job;
    LARGE_INTEGER      off_at;    /* absolute QPC target               */
    LARGE_INTEGER      qpc_freq;
    psyp_port*         port;
} psyp__async;

static DWORD WINAPI psyp__worker_main(LPVOID arg) {
    psyp__async* w = (psyp__async*)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    /* State (quit/has_job/off_at) and every data write are guarded by the
     * critical section, so the trailing edge can't stomp the caller's leading
     * edge in psyp__async_submit. The section is released only while blocked
     * in a wait. */
    EnterCriticalSection(&w->cs);
    while (!w->quit) {
        if (!w->has_job) {
            LeaveCriticalSection(&w->cs);
            WaitForSingleObject(w->wakeup, INFINITE);
            EnterCriticalSection(&w->cs);
            continue;
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        LONGLONG rem = w->off_at.QuadPart - now.QuadPart;
        if (rem <= 0) {
            psyp__write_data_raw(w->port, 0);
            w->has_job = 0;
            continue;
        }
        /* leave ~1.14 ms of slack for a QPC busy-wait tail */
        LONGLONG slack = (w->qpc_freq.QuadPart / 1000) + (w->qpc_freq.QuadPart / 7000);
        LONGLONG target = w->off_at.QuadPart;
        LeaveCriticalSection(&w->cs);
        if (rem > slack) {
            /* Coarse wait on the high-res timer, but also wake on the event so
             * a new submit / quit re-evaluates the deadline immediately. */
            LARGE_INTEGER due; /* negative = relative, 100ns units */
            due.QuadPart = -((rem - slack) * 10000000LL / w->qpc_freq.QuadPart);
            HANDLE handles[2] = { w->timer, w->wakeup };
            if (SetWaitableTimer(w->timer, &due, 0, NULL, NULL, FALSE))
                WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        } else {
            /* Sub-millisecond remainder: busy-wait on QPC. */
            do { QueryPerformanceCounter(&now); }
            while (now.QuadPart < target);
        }
        EnterCriticalSection(&w->cs);
    }
    /* On quit, flush any pending trailing edge right away (don't wait out the
     * remaining width) so the data lines are never left asserted. */
    if (w->has_job) {
        psyp__write_data_raw(w->port, 0);
        w->has_job = 0;
    }
    LeaveCriticalSection(&w->cs);
    return 0;
}

static bool psyp__async_start(psyp_port* p) {
    if (p->async) return true;
    psyp__async* w = (psyp__async*)calloc(1, sizeof(*w));
    if (!w) { psyp__set_error(p, "async: out of memory"); return false; }
    w->port = p;
    QueryPerformanceFrequency(&w->qpc_freq);
    InitializeCriticalSection(&w->cs);
    w->wakeup = CreateEventA(NULL, FALSE, FALSE, NULL);
    /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION needs Win10 1803+; fall back. */
    w->timer = CreateWaitableTimerExW(NULL, NULL,
                   0x00000002 /*HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
    if (!w->timer) w->timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    if (!w->wakeup || !w->timer) {
        if (w->wakeup) CloseHandle(w->wakeup);
        if (w->timer) CloseHandle(w->timer);
        DeleteCriticalSection(&w->cs); free(w);
        psyp__set_error(p, "async: timer/event create failed");
        return false;
    }
    w->thread = CreateThread(NULL, 0, psyp__worker_main, w, 0, NULL);
    if (!w->thread) {
        CloseHandle(w->wakeup); CloseHandle(w->timer);
        DeleteCriticalSection(&w->cs); free(w);
        psyp__set_error(p, "async: thread create failed");
        return false;
    }
    p->async = w;
    return true;
}

static void psyp__async_stop(psyp_port* p) {
    psyp__async* w = (psyp__async*)p->async;
    if (!w) return;
    EnterCriticalSection(&w->cs);
    w->quit = 1;
    LeaveCriticalSection(&w->cs);
    SetEvent(w->wakeup); /* wake an idle/timer wait so it observes quit */
    WaitForSingleObject(w->thread, INFINITE);
    CloseHandle(w->thread);
    CloseHandle(w->timer);
    CloseHandle(w->wakeup);
    DeleteCriticalSection(&w->cs);
    free(w);
    p->async = NULL;
}

static bool psyp__async_submit(psyp_port* p, uint8_t value, uint32_t usec) {
    psyp__async* w = (psyp__async*)p->async;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LONGLONG ticks = (LONGLONG)usec * w->qpc_freq.QuadPart / 1000000LL;
    EnterCriticalSection(&w->cs);
    /* Leading edge under the lock so it can't be stomped by the worker's
     * trailing-edge write of an in-flight pulse. */
    bool ok = psyp_write_data(p, value);
    w->off_at.QuadPart = now.QuadPart + ticks;
    w->has_job = 1;
    LeaveCriticalSection(&w->cs);
    SetEvent(w->wakeup);
    return ok;
}

#endif /* platform */

bool psyp_pulse_async(psyp_port* p, uint8_t value, uint32_t usec) {
    if (!psyp_is_open(p)) { psyp__set_error(p, "port not open"); return false; }
    if (!p->async) {
        /* First use: write the onset NOW, before paying the worker-thread
         * creation cost, so the very first trigger's onset is not delayed by
         * thread spin-up. No worker exists yet, so nothing can race this write
         * or stomp it with a trailing edge. */
        if (!psyp_write_data(p, value)) return false;
        if (!psyp__async_start(p)) {
            /* Don't leave the data lines asserted with no worker to clear
             * them; use the raw write so psyp__async_start's error survives. */
            psyp__write_data_raw(p, 0);
            return false;
        }
    }
    /* Steady state: the leading edge is (re)written inside submit on the
     * caller's thread under the worker lock, so onset is not delayed by the
     * worker waking up and the write cannot race the worker's trailing edge.
     * On the first-use path above this re-asserts the same value (no edge). */
    return psyp__async_submit(p, value, usec);
}

#else /* PSYP_NO_THREADS */

bool psyp_pulse_async(psyp_port* p, uint8_t value, uint32_t usec) {
    (void)value; (void)usec;
    psyp__set_error(p, "psyp_pulse_async unavailable: built with PSYP_NO_THREADS");
    return false;
}

#endif /* PSYP_NO_THREADS */

/* ======================================================================= *
 *  PLATFORM-INDEPENDENT helpers (built on the primitives above)
 * ======================================================================= */

bool psyp_get_status_bit(psyp_port* p, uint8_t mask) {
    return (psyp_read_status(p) & mask) != 0;
}

bool psyp_set_control_bit(psyp_port* p, uint8_t mask, bool on) {
    uint8_t c = psyp_read_control(p);
    if (!psyp_is_open(p)) return false;
    c = on ? (uint8_t)(c | mask) : (uint8_t)(c & (uint8_t)~mask);
    return psyp_write_control(p, c);
}

bool psyp_pulse(psyp_port* p, uint8_t value, uint32_t usec) {
    if (!psyp_write_data(p, value)) return false;
    if (usec) psyp__nsleep(usec);
    return psyp_write_data(p, 0);
}

#endif /* PSY_PARALLEL_IMPLEMENTATION_GUARD */
#endif /* PSY_PARALLEL_IMPLEMENTATION */

/* ------------------------------------------------------------------------
 * This software is available under the MIT-0 (MIT No Attribution) license.
 *
 * Copyright (c) 2026 psy_parallel contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY.
 * ------------------------------------------------------------------------ */
