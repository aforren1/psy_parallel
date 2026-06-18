# psy_parallel

A single-header C library for reading and writing the PC parallel port (LPT),
in the style of [stb](https://github.com/nothings/stb) /
[sokol](https://github.com/floooh/sokol). Built for the low-latency 8-bit
trigger output used in psychophysics, EEG, and electrophysiology rigs, where a
byte written to the data lines is latched onto a recording system.

Inspired by [pyparallel](https://github.com/pyparallel/pyparallel) and
[ppdev-mex](https://github.com/widmann/ppdev-mex). Targets **Windows** and
**Linux**.

## Use

Drop `psy_parallel.h` into your project. In exactly **one** `.c`/`.cpp` file,
define the implementation macro before including it:

```c
#define PSY_PARALLEL_IMPLEMENTATION
#include "psy_parallel.h"
```

Every other file just `#include "psy_parallel.h"`.

```c
psyp_port pp;
if (!psyp_open(&pp, &(psyp_desc){0})) {     /* NULL/zeroed desc = defaults */
    fprintf(stderr, "%s\n", psyp_error(&pp));
    return 1;
}
psyp_write_data(&pp, 0x55);                 /* latch a trigger byte        */
psyp_pulse(&pp, 0x55, 2000);                /* 0x55 for 2 ms, then back to 0 */
psyp_close(&pp);
```

See [example.c](example.c) for a runnable demo.

```
cc -O2 -pthread -o ppdemo example.c   # Linux (link pthreads for async pulse)
cl /O2 example.c                      # Windows (MSVC)
```

Link with `-pthread` on Linux for the async-pulse worker (Windows needs no
extra link). Build with `-DPSYP_NO_THREADS` to drop threading and the
`psyp_pulse_async` function entirely.

## Backends

| Backend                | Platform | Notes |
|------------------------|----------|-------|
| `PSYP_BACKEND_PPDEV`   | Linux    | Kernel `ppdev` char device (`/dev/parport0`). Portable, recommended. |
| `PSYP_BACKEND_DIRECT`  | Linux    | Raw x86 `ioperm`+`outb`/`inb`. Lowest latency, needs root, x86 only. |
| `PSYP_BACKEND_INPOUT`  | Windows  | Loads `inpout32.dll`/`inpoutx64.dll` at runtime and calls `Out32`/`Inp32`. |
| `PSYP_BACKEND_DEFAULT` | both     | `ppdev` on Linux, `inpout` on Windows. |

### Linux permissions (ppdev)

The kernel printer driver must not be holding the port, and your user needs
access to the device:

```sh
sudo rmmod lp                          # release the port (or blacklist `lp`)
sudo usermod -aG lp $USER && newgrp lp # one-time: grant device access
```

`PSYP_BACKEND_DIRECT` instead needs `CAP_SYS_RAWIO` (run as root or
`setcap cap_sys_rawio+ep`) and the I/O base address (`/proc/ioports`).

### Windows (inpout)

User-mode port I/O is blocked on modern Windows, so a kernel helper driver is
required. Install [InpOut](https://www.highrez.co.uk/downloads/inpout32/) and
ship `inpout32.dll` (32-bit) or `inpoutx64.dll` (64-bit) next to your
executable. The driver self-installs on first use and needs administrator
rights to do so. Find the port's I/O base address in Device Manager → the LPT
port → Resources, and pass it as `desc.base_addr`.

> **Caveat:** InpOut is an unmaintained, legacy-signed kernel driver that grants
> ring-0 port I/O. On hardened systems — Secure Boot with Memory Integrity
> (HVCI) enabled, or Windows' vulnerable-driver blocklist — it may be refused
> regardless of how it is installed; there is no maintained drop-in
> replacement. The data lines are output-only on a standard (SPP) port, so for
> trigger use a real PCIe/PCI parallel-port card (or onboard LPT) is required —
> USB-to-parallel *printer* adapters do not expose a register-level I/O port.

## Register model

A standard (SPP) port has three byte registers at `base+0/+1/+2`:

| Register | Addr   | Dir | Pins | Use |
|----------|--------|-----|------|-----|
| Data     | base+0 | R/W | 2–9  | The 8 trigger bits. |
| Status   | base+1 | R   | 10–13,15 | Input lines (`PSYP_STATUS_*`). |
| Control  | base+2 | R/W | 1,14,16,17 | Output lines + direction (`PSYP_CONTROL_*`). |

The library exposes the **raw register bits** (matching pyparallel). Several
pins are hardware-inverted relative to their register bit — Busy on status, and
Strobe/AutoFeed/SelectIn on control — as noted in the header. Common ISA base
addresses are provided as `PSYP_LPT1` (0x378), `PSYP_LPT2` (0x278), `PSYP_LPT3`
(0x3BC).

## API

```c
int         psyp_list_ports(psyp_port_info* out, int max);  /* enumerate; opens nothing */

bool        psyp_open(psyp_port* p, const psyp_desc* desc);
void        psyp_close(psyp_port* p);
const char* psyp_error(const psyp_port* p);
bool        psyp_is_open(const psyp_port* p);

bool        psyp_write_data(psyp_port* p, uint8_t value);   /* base+0 */
uint8_t     psyp_read_data(psyp_port* p);
bool        psyp_set_data_dir(psyp_port* p, bool input);    /* bidirectional ports */
bool        psyp_pulse(psyp_port* p, uint8_t value, uint32_t usec);        /* blocking   */
bool        psyp_pulse_async(psyp_port* p, uint8_t value, uint32_t usec);  /* non-blocking */

uint8_t     psyp_read_status(psyp_port* p);                 /* base+1 */
bool        psyp_get_status_bit(psyp_port* p, uint8_t mask);

uint8_t     psyp_read_control(psyp_port* p);                /* base+2 */
bool        psyp_write_control(psyp_port* p, uint8_t value);
bool        psyp_set_control_bit(psyp_port* p, uint8_t mask, bool on);
```

### Pulses

`psyp_pulse(p, value, usec)` writes `value`, busy/sleeps for `usec`, then
writes 0 — simple but **blocking**.

`psyp_pulse_async(p, value, usec)` returns immediately. It writes the leading
edge (`value`) on the calling thread — so the trigger **onset** is not delayed
by thread scheduling — then a dedicated real-time worker thread writes the
trailing 0 after `usec`. On the very first call the onset is written *before*
the worker thread is created, so even the first trigger's onset is not delayed
by thread spin-up. The worker runs at elevated priority for low jitter:

- **Linux**: `SCHED_DEADLINE` via the raw `sched_setattr` syscall (1 ms budget
  / 10 ms period), falling back to `SCHED_FIFO`, then a normal thread. The
  trailing edge is timed against an absolute `CLOCK_MONOTONIC` deadline with
  `pthread_cond_timedwait`, so it doesn't accumulate wake-latency drift and can
  be re-evaluated instantly. A real-time policy needs `CAP_SYS_NICE` (run
  elevated or `setcap cap_sys_nice+ep`); without it the call still works, just
  with more jitter. On glibc older than 2.17, also link `-lrt` for
  `clock_gettime`.
- **Windows**: a `THREAD_PRIORITY_TIME_CRITICAL` worker using a
  high-resolution waitable timer (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`,
  Win10 1803+) plus a short `QueryPerformanceCounter` busy-wait for the
  sub-millisecond tail.

The leading and trailing data writes are serialized under the worker's lock, so
a new pulse's onset is never overwritten by a previous pulse's trailing edge.
Issuing a new async pulse while one is pending makes the most recently
requested off-time govern the single trailing edge — the worker re-evaluates
immediately whether that deadline is earlier or later than the old one.

The worker is created on first use (after that first onset has already been
written) and cleanly stopped/joined by `psyp_close`, which flushes any pending
trailing edge **immediately** (so the data lines are never left asserted)
rather than waiting out the remaining pulse width.

Timing is still best-effort and bounded by OS scheduling — for the tightest
trigger timing prefer `PSYP_BACKEND_DIRECT` on a real-time-tuned kernel and an
isolated CPU.

`psyp_list_ports` enumerates ports without opening any — on Linux it scans the
`/dev/parport*` ppdev nodes; on Windows it lists `LPT*` devices. Pass
`out=NULL, max=0` to just get the count.

## Bindings

Language bindings live under [bindings/](bindings/), each with its own README:

- **[Python](bindings/python/)** — a dependency-free CPython extension built
  against the Limited API (one `psy_parallel.abi3.so` for CPython 3.8+).
  `pip install bindings/python`, then `import psy_parallel`.
- **[MATLAB / Octave](bindings/mex/)** — a MEX function with ppdev-mex-style
  command dispatch. Build with `bindings/mex/build.m`.

## License

MIT-0 (public domain equivalent). See the end of `psy_parallel.h`.
