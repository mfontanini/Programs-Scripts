/*      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 3 of the License, or
 *      (at your option) any later version.
 *      
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *      
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 *      
 *      Author: 
 *      Matias Fontanini
 * 
 */

#include <array>
#include "pywrapper.h"

using std::runtime_error;
using std::string;

Python::Script::Script(const string &script_path) 
  throw(runtime_error) {
    Py_Initialize();
    char arr[] = "path";
    //pyunique_ptr path(PySys_GetObject("path"));
    PyObject *path(PySys_GetObject(arr));
    pyunique_ptr pwd(PyString_FromString("."));
    PyList_Insert(path, 0, pwd.get());
    /* We don't need that string value anymore, so deref it */
    module.reset(PyImport_ImportModule(script_path.c_str()));
    if(!module.get())
        throw runtime_error("Failed to load script");
}

PyObject *Python::Script::load_function(const std::string &name) 
  throw(runtime_error) {
    PyObject *obj(PyObject_GetAttrString(module.get(), name.c_str()));
    if(!obj)
        throw std::runtime_error("Failed to find function");
    return obj;
}

PyObject *Python::alloc_pyobject(const std::string &str) {
    return PyString_FromString(str.c_str());
}

PyObject *Python::alloc_pyobject(const char *cstr) {
    return PyString_FromString(cstr);
}

PyObject *Python::alloc_pyobject(int num) {
    return PyInt_FromLong(num);
}

PyObject *Python::alloc_pyobject(bool value) {
    return PyBool_FromLong(value);
}

PyObject *Python::alloc_pyobject(double num) {
    return PyFloat_FromDouble(num);
}

bool is_py_int(PyObject *obj) {
    return PyInt_Check(obj);
}

bool is_py_float(PyObject *obj) {
    return PyFloat_Check(obj);
}

bool Python::convert(PyObject *obj, std::string &val) {
    if(!PyString_Check(obj))
        return false;
    val = PyString_AsString(obj);
    return true;
}

bool Python::convert(PyObject *obj, Py_ssize_t &val) {
    return generic_convert<Py_ssize_t>(obj, is_py_int, PyInt_AsSsize_t, val);
}

bool Python::convert(PyObject *obj, bool &value) {
    if(obj == Py_False)
        value = false;
    else if(obj == Py_True)
        value = true;
    else
        return false;
    return true;
}

bool Python::convert(PyObject *obj, double &val) {
    return generic_convert<double>(obj, is_py_float, PyFloat_AsDouble, val);
}

bool Python::convert(PyObject *obj, long &val) {
    return generic_convert<long>(obj, is_py_int, PyInt_AsLong, val);
}
