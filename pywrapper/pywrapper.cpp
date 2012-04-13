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

#include <algorithm>
#include "pywrapper.h"

using std::runtime_error;
using std::string;

Python::Script::Script(const string &script_path) 
  throw(runtime_error) {
    Py_Initialize();
    load_script(script_path);
}

Python::Script::Script() {
    Py_Initialize();
}

void Python::Script::load_script(const string &script_path) 
  throw(runtime_error) {
    char arr[] = "path";
    PyObject *path(PySys_GetObject(arr));
    string base_path("."), file_path;
    size_t last_slash(script_path.rfind("/"));
    if(last_slash != string::npos) {
        if(last_slash >= script_path.size() - 2)
            throw runtime_error("Invalid script path");
        base_path = script_path.substr(0, last_slash);
        file_path = script_path.substr(last_slash + 1);
    }
    else
        file_path = script_path;
    if(file_path.rfind(".py") == file_path.size() - 3)
        file_path = file_path.substr(0, file_path.size() - 3);
    pyunique_ptr pwd(PyString_FromString(base_path.c_str()));
    
    PyList_Append(path, pwd.get());
    /* We don't need that string value anymore, so deref it */
    module.reset(PyImport_ImportModule(file_path.c_str()));
    if(!module.get()) {
        print_error();
        throw runtime_error("Failed to load script"); 
    }
}

void Python::Script::clear_error() {
    PyErr_Clear();
}

void Python::Script::print_error() {
    PyErr_Print();
}

void Python::Script::print_object(PyObject *obj) {
    PyObject_Print(obj, stdout, 0);
}

PyObject *Python::Script::load_function(const std::string &name) 
  throw(runtime_error) {
    PyObject *obj(PyObject_GetAttrString(module.get(), name.c_str()));
    if(!obj)
        throw std::runtime_error("Failed to find function");
    return obj;
}

Python::pyunique_ptr Python::Script::call_function(const std::string &name) 
  throw(std::runtime_error) {
    pyunique_ptr func(load_function(name));
    PyObject *ret(PyObject_CallObject(func.get(), 0));
    if(!ret)
        throw std::runtime_error("Failed to call function");
    return pyunique_ptr(ret);
}

Python::pyunique_ptr Python::Script::get_attr(const std::string &name) 
  throw(std::runtime_error) {
    PyObject *obj(PyObject_GetAttrString(module.get(), name.c_str()));
    if(!obj)
        throw std::runtime_error("Unable to find attribute '" + name + '\'');
    return pyunique_ptr(obj);
}

// Allocation methods

PyObject *Python::alloc_pyobject(const std::string &str) {
    return PyString_FromString(str.c_str());
}

PyObject *Python::alloc_pyobject(const std::vector<char> &val, size_t sz) {
    return PyByteArray_FromStringAndSize(val.data(), sz);
}

PyObject *Python::alloc_pyobject(const std::vector<char> &val) {
    return alloc_pyobject(val, val.size());
}

PyObject *Python::alloc_pyobject(const char *cstr) {
    return PyString_FromString(cstr);
}

PyObject *Python::alloc_pyobject(size_t num) {
    return PyInt_FromLong(num);
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

bool Python::convert(PyObject *obj, std::vector<char> &val) {
    if(!PyByteArray_Check(obj))
        return false;
    if(val.size() < (size_t)PyByteArray_Size(obj))
        val.resize(PyByteArray_Size(obj));
    std::copy(PyByteArray_AsString(obj), 
      PyByteArray_AsString(obj) + PyByteArray_Size(obj), 
      val.begin());
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

bool Python::convert(PyObject *obj, size_t &val) {
    return generic_convert<size_t>(obj, is_py_int, PyInt_AsLong, val);
}
