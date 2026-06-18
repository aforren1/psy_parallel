import os
import sys
from setuptools import setup, Extension

HERE = os.path.abspath(os.path.dirname(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

# psy_parallel.h normally lives at the repo root (dev tree). For isolated
# builds (sdist / cibuildwheel, which only see this directory), CI copies the
# header next to this file; prefer that copy when present.
_local_header = os.path.join(HERE, "psy_parallel.h")
HEADER_DIR = HERE if os.path.exists(_local_header) else REPO_ROOT

extra_compile_args = []
extra_link_args = []
if sys.platform != "win32":
    # The async-pulse worker uses pthreads on Linux/macOS.
    extra_compile_args.append("-pthread")
    extra_link_args.append("-pthread")

# Target the CPython 3.8+ stable ABI (Limited API): one .abi3.so works across
# Python versions. The macro must match Py_LIMITED_API in the C source.
PY_LIMITED = 0x03080000

ext = Extension(
    name="psy_parallel",
    sources=[os.path.join(HERE, "psy_parallel_ext.c")],
    include_dirs=[HEADER_DIR],  # for "psy_parallel.h"
    define_macros=[("Py_LIMITED_API", hex(PY_LIMITED))],
    py_limited_api=True,
    extra_compile_args=extra_compile_args,
    extra_link_args=extra_link_args,
)

setup(
    name="psy_parallel",
    version="0.1.0",
    description="Parallel-port access (data/status/control, blocking and async pulses)",
    long_description="CPython binding for the psy_parallel single-header C library.",
    ext_modules=[ext],
    python_requires=">=3.8",
    # Build a cp38-abi3 wheel rather than a version-specific one.
    options={"bdist_wheel": {"py_limited_api": "cp38"}},
)
