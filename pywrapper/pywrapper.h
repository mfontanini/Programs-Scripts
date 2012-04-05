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
 
#ifndef __PYWRAPPER_H
#define __PYWRAPPER_H

#include <string>
#include <stdexcept>
#include <utility>
#include <memory>
#include <map>
#include <vector>
#include <list>
#include <tuple>
#include <python2.7/Python.h>


namespace Python {
    // Deleter that calls Py_XDECREF on the PyObject parameter.
    struct PyObjectDeleter {
            void operator()(PyObject *obj) {
                Py_XDECREF(obj);
            } 
        };
        // unique_ptr that uses Py_XDECREF as the destructor function.
        typedef std::unique_ptr<PyObject, PyObjectDeleter> pyunique_ptr;
    
    // ------------ Conversion functions ------------
    
    // Convert a PyObject to a std::string.
    bool convert(PyObject *obj, std::string &val);
    // Convert a PyObject to a ssize_t.
    bool convert(PyObject *obj, Py_ssize_t &val);
    // Convert a PyObject to a long.
    bool convert(PyObject *obj, long &val);
    // Convert a PyObject to an float.
    bool convert(PyObject *obj, double &val);
    
    // Helper class to use when constructing tuples from PyObjects
    template<class Tuple, std::size_t N>
    struct TupleCreater {
        static bool initialize(PyObject *obj, Tuple &tup) {
            TupleCreater<Tuple, N-1>::initialize(obj, tup);
            return convert(PyTuple_GetItem(obj, N-1), std::get<N-1>(tup));
        }
    };
    template<class Tuple>
    struct TupleCreater<Tuple, 1> {
        static bool initialize(PyObject *obj, Tuple &tup) {
            return convert(PyTuple_GetItem(obj, 0), std::get<0>(tup));
        }
    };
    // Convert a PyObject to a std::tuple.
    template<class... Args>
    bool convert(PyObject *obj, std::tuple<Args...> &tup) {
        if(!PyTuple_Check(obj) || 
            PyTuple_Size(obj) != sizeof...(Args))
            return false;
        return TupleCreater<decltype(tup), sizeof...(Args)>::initialize(obj, tup);
    }
    // Convert a PyObject to a std::map
    template<class K, class V>
    bool convert(PyObject *obj, std::map<K, V> &mp) {
        if(!PyDict_Check(obj))
            return false;
        PyObject *py_key, *py_val;
        Py_ssize_t pos(0);
        while (PyDict_Next(obj, &pos, &py_key, &py_val)) {
            K key;
            if(!convert(py_key, key))
                return false;
            V val;
            if(!convert(py_val, val))
                return false;
            mp.insert(std::make_pair(key, val));
        }
        return true;
    }
    // Convert a PyObject to a generic container.
    template<class T, class C>
    bool convert_list(PyObject *obj, C &container) {
        if(!PyList_Check(obj))
            return false;
        for(Py_ssize_t i(0); i < PyList_Size(obj); ++i) {
            T val;
            if(!convert(PyList_GetItem(obj, i), val))
                return false;
            container.push_back(std::move(val));
        }
        return true;
    }
    // Convert a PyObject to a std::list.
    template<class T> bool convert(PyObject *obj, std::list<T> &lst) {
        return convert_list<T, std::list<T>>(obj, lst);
    }
    // Convert a PyObject to a std::vector.
    template<class T> bool convert(PyObject *obj, std::vector<T> &vec) {
       return convert_list<T, std::vector<T>>(obj, vec);
    }
    
    template<class T> bool generic_convert(PyObject *obj, 
        const std::function<bool(PyObject*)> &is_obj,
        const std::function<T(PyObject*)> &converter,
        T &val) {
        if(!is_obj(obj))
            return false;
        val = converter(obj);
        return true;
    }
    
    // -------------- PyObject allocators ----------------
    
    // Generic python list allocation
    template<class T> static PyObject *alloc_list(const T &container) {
        PyObject *lst(PyList_New(container.size()));
            
        Py_ssize_t i(0);
        for(auto it(container.begin()); it != container.end(); ++it)
            PyList_SetItem(lst, i++, alloc_pyobject(*it));
        
        return lst;
    }
    // Creates a PyObject from a std::string
    PyObject *alloc_pyobject(const std::string &str);
    // Creates a PyObject from an int
    PyObject *alloc_pyobject(int num);
    // Creates a PyObject from a double
    PyObject *alloc_pyobject(double num);
    // Creates a PyObject from a std::vector
    template<class T> PyObject *alloc_pyobject(const std::vector<T> &container) {
        return alloc_list(container);
    }
    // Creates a PyObject from a std::list
    template<class T> PyObject *alloc_pyobject(const std::list<T> &container) {
        return alloc_list(container);
    }
    // Creates a PyObject from a std::map
    template<class T, class K> PyObject *alloc_pyobject(
      const std::map<T, K> &container) {
        PyObject *dict(PyDict_New());
            
        for(auto it(container.begin()); it != container.end(); ++it)
            PyDict_SetItem(dict, 
                alloc_pyobject(it->first),
                alloc_pyobject(it->second)
            );
        
        return dict;
    }
    
    // This class is an abstraction of a python script.
    class Script {
    public:
        // Script constructor that takes the script path as the name.
        Script(const std::string &script_path) throw(std::runtime_error);
        
        // Calls the function named "name" using the arguments "args".
        template<typename... Args>
        pyunique_ptr call_function(const std::string &name, Args... args) 
          throw(std::runtime_error) {
            PyObject *func(load_function(name));
            // Create the tuple argument
            pyunique_ptr tup(PyTuple_New(sizeof...(args)));
            add_tuple_vars(tup, args...);
            // Call our object
            PyObject *ret(PyObject_CallObject(func, tup.get()));
            if(!ret)
                throw std::runtime_error("Failed to call function");
            return pyunique_ptr(ret);
        }
        
        pyunique_ptr call_function(const std::string &name) 
          throw(std::runtime_error) {
            PyObject *func(load_function(name));
            PyObject *ret(PyObject_CallObject(func, 0));
            if(!ret)
                throw std::runtime_error("Failed to call function");
            return pyunique_ptr(ret);
        }
    private:
        PyObject *load_function(const std::string &name) 
          throw(std::runtime_error);
    
        // Variadic template method to add items to a tuple
        template<typename First, typename... Rest> 
        void add_tuple_vars(pyunique_ptr &tup, First head, Rest... tail) {
            add_tuple_var(
                tup, 
                PyTuple_Size(tup.get()) - sizeof...(tail) - 1, 
                alloc_pyobject(head)
            );
            add_tuple_vars(tup, tail...);
        }
        
        // Base case for add_tuple_vars
        template<typename Arg> 
        void add_tuple_vars(pyunique_ptr &tup, Arg arg) {
            add_tuple_var(tup, PyTuple_Size(tup.get()) - 1, alloc_pyobject(arg));
        }
        
        // Adds a PyObject* to the tuple object
        void add_tuple_var(pyunique_ptr &tup, Py_ssize_t i, PyObject *pobj) {
            PyTuple_SetItem(tup.get(), i, pobj);
        }
        
        pyunique_ptr module;
        std::map<std::string, PyObject*> functions;
    };

};

#endif // __PYWRAPPER_H
