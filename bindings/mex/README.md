# psy_parallel — MATLAB / Octave (MEX) binding

A MEX wrapper around the [`psy_parallel.h`](../../psy_parallel.h) single-header
library, in the command-dispatch style of widmann's
[ppdev-mex](https://github.com/widmann/ppdev-mex): the first argument is a
command string, and `open` returns an opaque `uint64` handle you pass back.

## Build

In MATLAB or Octave, from this directory:

```matlab
run build.m        % produces psy_parallel_mex.<mexext>
```

- **Octave** needs the dev package providing `mex`/`mkoctfile`
  (e.g. `sudo apt install liboctave-dev`).
- **MATLAB** needs a configured compiler (`mex -setup`).

The build adds `-I` to the repo root (for `psy_parallel.h`) and links
`-lpthread` on Linux/macOS for the async-pulse worker (Windows uses Win32
threads, no extra lib).

## Use

```matlab
ports = psy_parallel_mex('list');                 % struct array: name/backend/base_addr

h = psy_parallel_mex('open');                      % platform defaults
% h = psy_parallel_mex('open', '/dev/parport1');   % ppdev device (Linux)
% h = psy_parallel_mex('open', 'direct', 888);     % raw x86 I/O @ base (root)
% h = psy_parallel_mex('open', 'inpout', 888);     % Windows inpout @ base

psy_parallel_mex('write',      h, 85);             % data register (0..255)
d = psy_parallel_mex('read',   h);                 % read data register (uint8)
psy_parallel_mex('setdir',     h, true);           % data direction: input
psy_parallel_mex('pulse',      h, 85, 2000);       % blocking 2 ms pulse
psy_parallel_mex('pulseasync', h, 85, 2000);       % non-blocking pulse
s = psy_parallel_mex('status', h);                 % status register
c = psy_parallel_mex('control', h);                % read control register
psy_parallel_mex('control',    h, 4);              % write control register
psy_parallel_mex('close',      h);
```

See [example.m](example.m).

### Commands

| Command | Returns | Notes |
|---|---|---|
| `'list'` | struct array | `name`, `backend`, `base_addr`; opens nothing |
| `'open' [, dev|'direct'|'inpout' [, base]]` | `uint64` handle | defaults per platform |
| `'write', h, value` | — | data register, 0..255 |
| `'read', h` | `uint8` | data register |
| `'setdir', h, isInput` | — | bidirectional-port direction |
| `'pulse', h, value, usec` | — | blocking |
| `'pulseasync', h, value, usec` | — | non-blocking (RT worker) |
| `'status', h` | `uint8` | status register (read-only) |
| `'control', h [, value]` | `uint8` / — | read or write control register |
| `'close', h` | — | release the port |

Errors are raised via `error()` with id `psy_parallel:*`. Every open port is
closed automatically on `clear psy_parallel_mex` / interpreter exit (a
`mexAtExit` hook), so a forgotten `close` cannot leave the async worker thread
running against a freed port — but calling `close` yourself is still good form.

Platform setup (ppdev permissions, the Windows inpout DLL, etc.) is described
in the [top-level README](../../README.md).
