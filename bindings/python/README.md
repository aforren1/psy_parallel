# psy_parallel — Python binding

A small CPython extension wrapping the [`psy_parallel.h`](../../psy_parallel.h)
single-header library. It uses the plain CPython C API (no nanobind/pybind/
Cython) to stay dependency-free, and is built against the **Limited API /
stable ABI** (`Py_LIMITED_API = 0x03080000`), so a single `psy_parallel.abi3.so`
works on CPython 3.8+ without recompiling per version.

## Build / install

From this directory:

```sh
pip install .                       # build + install an abi3 wheel
# or, in place for development:
python setup.py build_ext --inplace
```

The library implementation is compiled directly into the extension; on
Linux/macOS the build links `-pthread` for the async-pulse worker.

## Use

```python
import psy_parallel as pp

for info in pp.list_ports():            # enumerate without opening
    print(info)                         # {'name': '/dev/parport0', 'backend': 1, 'base_addr': 0}

# Defaults: ppdev /dev/parport0 (Linux), inpout @ LPT1 (Windows).
with pp.Port() as port:                 # or pp.Port(device="/dev/parport1")
    port.write_data(0x55)               # latch a trigger byte
    port.pulse(0x55, usec=2000)         # blocking: 0x55 for 2 ms, then 0
    port.pulse_async(0x55, usec=2000)   # returns immediately; worker drops it
    s = port.read_status()
    print(hex(s), port.get_status_bit(pp.STATUS_BUSY))
```

Other backends:

```python
pp.Port(backend=pp.BACKEND_DIRECT, base_addr=pp.LPT1)   # Linux raw I/O (root)
pp.Port(backend=pp.BACKEND_INPOUT, base_addr=0x378)     # Windows inpout
```

Tune the async-pulse worker's real-time reservation (Linux
[SCHED_DEADLINE](https://www.kernel.org/doc/html/latest/scheduler/sched-deadline.html); ns):

```python
port = pp.Port(rt_runtime_ns=500_000, rt_deadline_ns=2_000_000, rt_period_ns=2_000_000)
print(port.sched)   # {'runtime_ns': 500000, 'deadline_ns': 2000000, 'period_ns': 2000000}
```

Omit `rt_period_ns` (or leave it 0) and it defaults to the deadline. Leave all
three unset for the library defaults. Invalid combinations (must be
`0 < runtime ≤ deadline ≤ period`) raise `pp.Error` at construction.

See [example.py](example.py).

## API

| | |
|---|---|
| `pp.list_ports() -> list[dict]` | enumerate ports (`name`, `backend`, `base_addr`) |
| `pp.Port(device=None, backend=BACKEND_DEFAULT, base_addr=0, exclusive=False, rt_runtime_ns=0, rt_deadline_ns=0, rt_period_ns=0)` | open a port |
| `port.write_data(value)` / `port.read_data()` | data register |
| `port.set_data_dir(input)` | data-line direction (bidirectional ports) |
| `port.pulse(value, usec)` | blocking pulse (releases the GIL while waiting) |
| `port.pulse_async(value, usec)` | non-blocking pulse via the RT worker thread |
| `port.read_status()` / `port.get_status_bit(mask)` | status register |
| `port.read_control()` / `port.write_control(value)` / `port.set_control_bit(mask, on)` | control register |
| `port.close()`, `port.is_open`, `port.base_addr`, `port.backend`, `port.sched` | lifecycle / info |

Errors raise `pp.Error`. Constants: `BACKEND_*`, `LPT1/2/3`, `STATUS_*`,
`CONTROL_*`, `DEFAULT_RT_{RUNTIME,DEADLINE,PERIOD}_NS`. `Port` is a context
manager (`with pp.Port() as port:`).

Platform setup (ppdev permissions, the Windows inpout DLL, etc.) is described
in the [top-level README](../../README.md).
