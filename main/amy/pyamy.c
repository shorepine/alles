
#include <Python.h>
#include <math.h>
#include "amy.h"

static PyObject * send_wrapper(PyObject *self, PyObject *args) {
    char *arg1;
    if (! PyArg_ParseTuple(args, "s", &arg1)) {
        return NULL;
    }
    parse_message(arg1);
    return Py_None;
}

static PyObject * render_wrapper(PyObject *self, PyObject *args) {
    int16_t * result;
    result = fill_audio_buffer_task();
    // Create a python list of ints (they are signed shorts that come back)
    PyObject* ret = PyList_New(BLOCK_SIZE); 
    for (int i = 0; i < BLOCK_SIZE; ++i) {
        PyObject* python_int = Py_BuildValue("i", result[i]);
        PyList_SetItem(ret, i, python_int);
    }
    return ret;
}


static PyMethodDef AMYMethods[] = {
    {"render", render_wrapper, METH_VARARGS, "Render audio"},
    {"send", send_wrapper, METH_VARARGS, "Send a message"},
    { NULL, NULL, 0, NULL }
};

static struct PyModuleDef amyDef =
{
    PyModuleDef_HEAD_INIT,
    "amy", /* name of module */
    "",          /* module documentation, may be NULL */
    -1,          /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
    AMYMethods
};

PyMODINIT_FUNC PyInit_amy(void)
{
    start_amy();
    return PyModule_Create(&amyDef);

}

