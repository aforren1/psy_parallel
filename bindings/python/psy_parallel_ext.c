/* psy_parallel_ext.c - CPython extension wrapping psy_parallel.h
 *
 * A thin, dependency-free binding (no nanobind/pybind/Cython): it needs only
 * Python.h, matching the single-header library's zero-dependency style. The
 * library implementation is compiled directly into this module.
 *
 * Built against the stable ABI / Limited API (Py_LIMITED_API), so one compiled
 * .abi3.so works across CPython >= 3.8 without recompiling per version. That
 * rules out the static PyTypeObject layout, so the Port type is a heap type
 * created with PyType_FromSpec.
 *
 *     import psy_parallel as pp
 *     for info in pp.list_ports():
 *         print(info)                    # {'name':..., 'backend':..., 'base_addr':...}
 *     with pp.Port() as port:            # defaults per platform
 *         port.write_data(0x55)
 *         port.pulse(0x55, usec=2000)    # blocking
 *         port.pulse_async(0x55, 2000)   # returns immediately
 *         print(hex(port.read_status()))
 */
#ifndef Py_LIMITED_API
#define Py_LIMITED_API 0x03080000   /* target the CPython 3.8+ stable ABI */
#endif
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#define PSY_PARALLEL_IMPLEMENTATION
#include "psy_parallel.h"

typedef struct {
    PyObject_HEAD
    psyp_port port;
} PortObject;

static PyObject* PpError; /* module-level exception type */

#define AS_PORT(self) (&((PortObject*)(self))->port)

/* Raise psy_parallel.Error carrying the library's last error string. */
static PyObject* pp_raise(psyp_port* port) {
    PyErr_SetString(PpError, psyp_error(port));
    return NULL;
}

static int Port_init(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "device", "backend", "base_addr", "exclusive", NULL };
    const char* device = NULL;
    int backend = PSYP_BACKEND_DEFAULT;
    unsigned int base_addr = 0;
    int exclusive = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ziIp", kw,
                                     &device, &backend, &base_addr, &exclusive))
        return -1;
    if (base_addr > 0xFFFFu) {
        PyErr_SetString(PyExc_ValueError, "base_addr must fit in 16 bits");
        return -1;
    }

    psyp_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.backend = (psyp_backend)backend;
    desc.device = device;
    desc.base_addr = (uint16_t)base_addr;
    desc.exclusive = exclusive ? true : false;

    if (!psyp_open(AS_PORT(self), &desc)) {
        PyErr_SetString(PpError, psyp_error(AS_PORT(self)));
        return -1;
    }
    return 0;
}

static void Port_dealloc(PyObject* self) {
    psyp_close(AS_PORT(self));
    /* Heap type: fetch tp_free via the stable ABI and release the type ref the
     * instance holds (PyType_GenericAlloc incref's the type since 3.8). */
    PyTypeObject* tp = Py_TYPE(self);
    freefunc tp_free = (freefunc)PyType_GetSlot(tp, Py_tp_free);
    tp_free(self);
    Py_DECREF(tp);
}

/* --- data register ------------------------------------------------------ */

static PyObject* Port_write_data(PyObject* self, PyObject* arg) {
    unsigned long v = PyLong_AsUnsignedLong(arg);
    if (v == (unsigned long)-1 && PyErr_Occurred()) return NULL;
    if (v > 0xFF) { PyErr_SetString(PyExc_ValueError, "value must be 0..255"); return NULL; }
    if (!psyp_write_data(AS_PORT(self), (uint8_t)v)) return pp_raise(AS_PORT(self));
    Py_RETURN_NONE;
}

static PyObject* Port_read_data(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    uint8_t v = psyp_read_data(AS_PORT(self));
    if (psyp_error(AS_PORT(self))[0]) return pp_raise(AS_PORT(self));
    return PyLong_FromUnsignedLong(v);
}

static PyObject* Port_set_data_dir(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "input", NULL };
    int input = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "p", kw, &input)) return NULL;
    if (!psyp_set_data_dir(AS_PORT(self), input ? true : false)) return pp_raise(AS_PORT(self));
    Py_RETURN_NONE;
}

static PyObject* Port_pulse(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "value", "usec", NULL };
    unsigned int value, usec;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "II", kw, &value, &usec)) return NULL;
    if (value > 0xFF) { PyErr_SetString(PyExc_ValueError, "value must be 0..255"); return NULL; }
    bool ok;
    /* Release the GIL: psyp_pulse blocks for `usec`. */
    Py_BEGIN_ALLOW_THREADS
    ok = psyp_pulse(AS_PORT(self), (uint8_t)value, usec);
    Py_END_ALLOW_THREADS
    if (!ok) return pp_raise(AS_PORT(self));
    Py_RETURN_NONE;
}

static PyObject* Port_pulse_async(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "value", "usec", NULL };
    unsigned int value, usec;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "II", kw, &value, &usec)) return NULL;
    if (value > 0xFF) { PyErr_SetString(PyExc_ValueError, "value must be 0..255"); return NULL; }
    if (!psyp_pulse_async(AS_PORT(self), (uint8_t)value, usec)) return pp_raise(AS_PORT(self));
    Py_RETURN_NONE;
}

/* --- status / control --------------------------------------------------- */

static PyObject* Port_read_status(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    uint8_t v = psyp_read_status(AS_PORT(self));
    if (psyp_error(AS_PORT(self))[0]) return pp_raise(AS_PORT(self));
    return PyLong_FromUnsignedLong(v);
}

static PyObject* Port_get_status_bit(PyObject* self, PyObject* arg) {
    unsigned long mask = PyLong_AsUnsignedLong(arg);
    if (mask == (unsigned long)-1 && PyErr_Occurred()) return NULL;
    int set = psyp_get_status_bit(AS_PORT(self), (uint8_t)mask);
    if (psyp_error(AS_PORT(self))[0]) return pp_raise(AS_PORT(self));
    return PyBool_FromLong(set);
}

static PyObject* Port_read_control(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    uint8_t v = psyp_read_control(AS_PORT(self));
    if (psyp_error(AS_PORT(self))[0]) return pp_raise(AS_PORT(self));
    return PyLong_FromUnsignedLong(v);
}

static PyObject* Port_write_control(PyObject* self, PyObject* arg) {
    unsigned long v = PyLong_AsUnsignedLong(arg);
    if (v == (unsigned long)-1 && PyErr_Occurred()) return NULL;
    if (v > 0xFF) { PyErr_SetString(PyExc_ValueError, "value must be 0..255"); return NULL; }
    if (!psyp_write_control(AS_PORT(self), (uint8_t)v)) return pp_raise(AS_PORT(self));
    Py_RETURN_NONE;
}

static PyObject* Port_set_control_bit(PyObject* self, PyObject* args, PyObject* kwds) {
    static char* kw[] = { "mask", "on", NULL };
    unsigned int mask;
    int on;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Ip", kw, &mask, &on)) return NULL;
    if (!psyp_set_control_bit(AS_PORT(self), (uint8_t)mask, on ? true : false))
        return pp_raise(AS_PORT(self));
    Py_RETURN_NONE;
}

/* --- lifecycle / context manager --------------------------------------- */

static PyObject* Port_close(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    psyp_close(AS_PORT(self));
    Py_RETURN_NONE;
}

static PyObject* Port_enter(PyObject* self, PyObject* Py_UNUSED(ignored)) {
    Py_INCREF(self);
    return self;
}

static PyObject* Port_exit(PyObject* self, PyObject* Py_UNUSED(args)) {
    psyp_close(AS_PORT(self));
    Py_RETURN_FALSE; /* don't suppress exceptions */
}

static PyObject* Port_get_is_open(PyObject* self, void* Py_UNUSED(closure)) {
    return PyBool_FromLong(psyp_is_open(AS_PORT(self)));
}

static PyObject* Port_get_base_addr(PyObject* self, void* Py_UNUSED(closure)) {
    return PyLong_FromUnsignedLong(AS_PORT(self)->base_addr);
}

static PyObject* Port_get_backend(PyObject* self, void* Py_UNUSED(closure)) {
    return PyLong_FromLong((long)AS_PORT(self)->backend);
}

static PyGetSetDef Port_getset[] = {
    { "is_open",   Port_get_is_open,   NULL, "True while the port is open.", NULL },
    { "base_addr", Port_get_base_addr, NULL, "Resolved I/O base address.",   NULL },
    { "backend",   Port_get_backend,   NULL, "Active backend (BACKEND_*).",  NULL },
    { NULL }
};

static PyMethodDef Port_methods[] = {
    { "write_data",     Port_write_data,     METH_O,
      "write_data(value): drive the 8 data bits (0..255)." },
    { "read_data",      Port_read_data,      METH_NOARGS,
      "read_data() -> int: read the data register." },
    { "set_data_dir",   (PyCFunction)Port_set_data_dir,   METH_VARARGS | METH_KEYWORDS,
      "set_data_dir(input): set data-line direction on a bidirectional port." },
    { "pulse",          (PyCFunction)Port_pulse,          METH_VARARGS | METH_KEYWORDS,
      "pulse(value, usec): write value, block usec microseconds, then write 0." },
    { "pulse_async",    (PyCFunction)Port_pulse_async,    METH_VARARGS | METH_KEYWORDS,
      "pulse_async(value, usec): write value now, return immediately; a worker "
      "thread writes 0 after usec." },
    { "read_status",    Port_read_status,    METH_NOARGS,
      "read_status() -> int: read the status register (STATUS_* bits)." },
    { "get_status_bit", Port_get_status_bit, METH_O,
      "get_status_bit(mask) -> bool: test one status bit." },
    { "read_control",   Port_read_control,   METH_NOARGS,
      "read_control() -> int: read the control register (CONTROL_* bits)." },
    { "write_control",  Port_write_control,  METH_O,
      "write_control(value): write the control register." },
    { "set_control_bit",(PyCFunction)Port_set_control_bit,METH_VARARGS | METH_KEYWORDS,
      "set_control_bit(mask, on): set/clear one control bit." },
    { "close",          Port_close,          METH_NOARGS,
      "close(): release the port (idempotent)." },
    { "__enter__",      Port_enter,          METH_NOARGS, NULL },
    { "__exit__",       Port_exit,           METH_VARARGS, NULL },
    { NULL }
};

static PyType_Slot Port_slots[] = {
    { Py_tp_doc,     (void*)"Parallel port handle. Port(device=None, "
                            "backend=BACKEND_DEFAULT, base_addr=0, exclusive=False)." },
    { Py_tp_new,     (void*)PyType_GenericNew },
    { Py_tp_init,    (void*)Port_init },
    { Py_tp_dealloc, (void*)Port_dealloc },
    { Py_tp_methods, Port_methods },
    { Py_tp_getset,  Port_getset },
    { 0, NULL }
};

static PyType_Spec Port_spec = {
    .name = "psy_parallel.Port",
    .basicsize = sizeof(PortObject),
    .itemsize = 0,
    .flags = Py_TPFLAGS_DEFAULT,
    .slots = Port_slots,
};

/* --- module-level functions -------------------------------------------- */

static PyObject* mod_list_ports(PyObject* Py_UNUSED(self), PyObject* Py_UNUSED(args)) {
    int n = psyp_list_ports(NULL, 0);
    PyObject* list = PyList_New(n < 0 ? 0 : n);
    if (!list) return NULL;
    if (n <= 0) return list;

    psyp_port_info* v = (psyp_port_info*)PyMem_Malloc((size_t)n * sizeof(*v));
    if (!v) { Py_DECREF(list); return PyErr_NoMemory(); }
    int got = psyp_list_ports(v, n);
    if (got < n) n = got; /* shrank between calls; trim */

    for (int i = 0; i < n; i++) {
        PyObject* d = PyDict_New();
        PyObject* name = PyUnicode_FromString(v[i].name);
        PyObject* be = PyLong_FromLong((long)v[i].backend);
        PyObject* base = PyLong_FromUnsignedLong(v[i].base_addr);
        if (!d || !name || !be || !base) {
            Py_XDECREF(d); Py_XDECREF(name); Py_XDECREF(be); Py_XDECREF(base);
            Py_DECREF(list); PyMem_Free(v); return NULL;
        }
        PyDict_SetItemString(d, "name", name);
        PyDict_SetItemString(d, "backend", be);
        PyDict_SetItemString(d, "base_addr", base);
        Py_DECREF(name); Py_DECREF(be); Py_DECREF(base);
        PyList_SetItem(list, i, d); /* steals ref to d */
    }
    PyMem_Free(v);
    return list;
}

static PyMethodDef module_methods[] = {
    { "list_ports", mod_list_ports, METH_NOARGS,
      "list_ports() -> list[dict]: enumerate available parallel ports without "
      "opening them. Each dict has 'name', 'backend', 'base_addr'." },
    { NULL }
};

static PyModuleDef psy_parallel_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "psy_parallel",
    .m_doc = "Parallel-port access (data/status/control, blocking and async pulses).",
    .m_size = -1,
    .m_methods = module_methods,
};

PyMODINIT_FUNC PyInit_psy_parallel(void) {
    PyObject* m = PyModule_Create(&psy_parallel_module);
    if (!m) return NULL;

    PyObject* type = PyType_FromSpec(&Port_spec);
    if (!type) { Py_DECREF(m); return NULL; }
    if (PyModule_AddObject(m, "Port", type) < 0) {
        Py_DECREF(type); Py_DECREF(m); return NULL;
    }

    PpError = PyErr_NewException("psy_parallel.Error", NULL, NULL);
    if (!PpError) { Py_DECREF(m); return NULL; }
    Py_INCREF(PpError);
    if (PyModule_AddObject(m, "Error", PpError) < 0) {
        Py_DECREF(PpError); Py_DECREF(PpError); Py_DECREF(m); return NULL;
    }

    /* backends */
    PyModule_AddIntConstant(m, "BACKEND_DEFAULT", PSYP_BACKEND_DEFAULT);
    PyModule_AddIntConstant(m, "BACKEND_PPDEV",   PSYP_BACKEND_PPDEV);
    PyModule_AddIntConstant(m, "BACKEND_DIRECT",  PSYP_BACKEND_DIRECT);
    PyModule_AddIntConstant(m, "BACKEND_INPOUT",  PSYP_BACKEND_INPOUT);
    /* default base addresses */
    PyModule_AddIntConstant(m, "LPT1", PSYP_LPT1);
    PyModule_AddIntConstant(m, "LPT2", PSYP_LPT2);
    PyModule_AddIntConstant(m, "LPT3", PSYP_LPT3);
    /* status bits */
    PyModule_AddIntConstant(m, "STATUS_BUSY",   PSYP_STATUS_BUSY);
    PyModule_AddIntConstant(m, "STATUS_ACK",    PSYP_STATUS_ACK);
    PyModule_AddIntConstant(m, "STATUS_PAPER",  PSYP_STATUS_PAPER);
    PyModule_AddIntConstant(m, "STATUS_SELECT", PSYP_STATUS_SELECT);
    PyModule_AddIntConstant(m, "STATUS_ERROR",  PSYP_STATUS_ERROR);
    /* control bits */
    PyModule_AddIntConstant(m, "CONTROL_STROBE",   PSYP_CONTROL_STROBE);
    PyModule_AddIntConstant(m, "CONTROL_AUTOFEED", PSYP_CONTROL_AUTOFEED);
    PyModule_AddIntConstant(m, "CONTROL_INIT",     PSYP_CONTROL_INIT);
    PyModule_AddIntConstant(m, "CONTROL_SELECT",   PSYP_CONTROL_SELECT);
    PyModule_AddIntConstant(m, "CONTROL_DIR",      PSYP_CONTROL_DIR);

    return m;
}
