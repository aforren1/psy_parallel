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

See [example.py](example.py).

## API

| | |
|---|---|
| `pp.list_ports() -> list[dict]` | enumerate ports (`name`, `backend`, `base_addr`) |
| `pp.Port(device=None, backend=BACKEND_DEFAULT, base_addr=0, exclusive=False)` | open a port |
| `port.write_data(value)` / `port.read_data()` | data register |
| `port.set_data_dir(input)` | data-line direction (bidirectional ports) |
| `port.pulse(value, usec)` | blocking pulse (releases the GIL while waiting) |
| `port.pulse_async(value, usec)` | non-blocking pulse via the RT worker thread |
| `port.read_status()` / `port.get_status_bit(mask)` | status register |
| `port.read_control()` / `port.write_control(value)` / `port.set_control_bit(mask, on)` | control register |
| `port.close()`, `port.is_open`, `port.base_addr`, `port.backend` | lifecycle / info |

Errors raise `pp.Error`. Constants: `BACKEND_*`, `LPT1/2/3`, `STATUS_*`,
`CONTROL_*`. `Port` is a context manager (`with pp.Port() as port:`).

Platform setup (ppdev permissions, the Windows inpout DLL, etc.) is described
in the [top-level README](../../README.md).
