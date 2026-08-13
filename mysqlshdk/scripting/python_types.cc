/*
 * Copyright (c) 2016, 2026, Oracle and/or its affiliates.
 * Copyright (c) 2026, MariaDB plc.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms,
 * as designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 *
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "scripting/python_types.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "scripting/object_factory.h"
#include "scripting/python_context.h"
#include "scripting/python_type_conversion.h"

namespace shcore {

namespace {

py::Release convert(const Argument_list &args,
                    Python_context *context = nullptr,
                    std::optional<std::size_t> argc = {}) {
  if (!context) {
    context = Python_context::get();
  }

  if (!argc.has_value()) {
    argc = args.size();
  }

  py::Release argv{PyTuple_New(*argc)};

  for (size_t index = 0; index < *argc; ++index) {
    PyTuple_SetItem(argv.get(), index, context->convert(args[index]).release());
  }

  return argv;
}

[[noreturn]] void throw_python_error(const char *fallback,
                                     Python_context *context) {
  // converts mysqlsh.Error to shcore::Error and throws the exception, if the
  // Python exception is something else, then just returns
  context->throw_if_mysqlsh_error();

  std::string traceback;
  std::string type;
  auto error = Python_context::fetch_and_clear_exception(&traceback, &type);

  if (error.empty()) {
    error = fallback;
  }

  throw Exception::scripting_error(type, error, traceback);
}

[[noreturn]] void throw_python_error(const char *fallback) {
  throw_python_error(fallback, Python_context::get());
}

py::Release call_with_error_translation(PyObject *callable, PyObject *args,
                                        PyObject *kwargs,
                                        Python_context *context) {
  py::Release ret_val{PyObject_Call(callable, args, kwargs)};

  if (!ret_val) {
    throw_python_error("User-defined function threw an exception", context);
  }

  return ret_val;
}

Value invoke_python_callable(PyObject *callable, const Argument_list &args,
                             Python_context *context) {
  const auto argv = convert(args, context);
  const auto ret_val =
      call_with_error_translation(callable, argv.get(), nullptr, context);
  return context->convert(ret_val.get());
}

}  // namespace

Python_object::Python_object(PyObject *obj) : m_object(obj) {
  py::Release class_obj{PyObject_GetAttrString(object(), "__class__")};
  py::Release class_name{PyObject_GetAttrString(class_obj.get(), "__name__")};
  Python_context::pystring_to_string(class_name.get(), &m_class);
}

Python_object::~Python_object() {
  WillEnterPython lock;
  m_object.reset();
}

std::string Python_object::class_name() const {
  return m_class.empty() ? "PythonObject" : m_class;
}

std::string &Python_object::append_repr(std::string &s_out) const {
  return append_descr(s_out, 0, '"');
}

std::string &Python_object::append_descr(std::string &s_out, int, int) const {
  WillEnterPython lock;
  py::Release obj_repr{PyObject_Repr(object())};
  std::string s;

  if (!Python_context::pystring_to_string(obj_repr, &s)) {
    return s_out.append("<" + class_name() + ">");
  }

  return s_out.append(s);
}

std::vector<std::string> Python_object::get_members() const {
  std::vector<std::pair<bool, std::string>> members;

  {
    WillEnterPython lock;
    shcore::Python_context::get_members_of(object(), &members);
  }

  std::vector<std::string> ret;
  ret.reserve(members.size());

  for (auto &member : members) {
    ret.emplace_back(std::move(member.second));
  }

  return ret;
}

bool Python_object::operator==(const Object_bridge &other) const {
  return this == &other;
}

Value Python_object::get_member(const std::string &prop) const {
  WillEnterPython lock;
  py::Release member{PyObject_GetAttrString(object(), prop.c_str())};

  if (!member) {
    if (PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_AttributeError)) {
      throw Exception::attrib_error("Invalid object member " + prop);
    }

    throw_python_error("Python attribute access threw an exception");
  }

  return py::convert(member.get());
}

bool Python_object::has_member(const std::string &prop) const {
  std::vector<std::pair<bool, std::string>> members;

  {
    WillEnterPython lock;
    shcore::Python_context::get_members_of(object(), &members);
  }

  return std::find_if(members.begin(), members.end(), [&prop](const auto &m) {
           return m.second == prop;
         }) != members.end();
}

void Python_object::set_member(const std::string &prop, Value value) {
  if (has_method(prop)) {
    throw Exception::attrib_error("Can't set object member " + prop +
                                  ", it is a method");
  }

  WillEnterPython lock;
  const auto v = py::convert(value);

  if (PyObject_SetAttrString(object(), prop.c_str(), v.get()) != 0) {
    throw_python_error("Python attribute assignment threw an exception");
  }
}

bool Python_object::is_indexed() const { return false; }

Value Python_object::get_member(size_t) const {
  throw Exception::attrib_error("Can't access object members using an index");
}

void Python_object::set_member(size_t, Value) {
  throw Exception::attrib_error("Can't set object member using an index");
}

size_t Python_object::length() const {
  throw Exception::attrib_error("Can't get count of indexed members");
}

bool Python_object::has_method(const std::string &name) const {
  std::vector<std::pair<bool, std::string>> members;

  {
    WillEnterPython lock;
    shcore::Python_context::get_members_of(object(), &members);
  }

  for (const auto &member : members) {
    if (member.second == name) {
      return member.first;
    }
  }

  return false;
}

Value Python_object::call(const std::string &name, const Argument_list &args) {
  if (!has_method(name)) {
    throw Exception::attrib_error("Invalid object method " + name);
  }

  WillEnterPython lock;
  py::Release callable{PyObject_GetAttrString(object(), name.c_str())};

  if (!callable) {
    throw_python_error("Python method access threw an exception");
  }

  if (PyMethod_Check(callable.get())) {
    Python_bound_method bound_method{Python_context::get(), callable.get()};
    return bound_method.invoke(args);
  }

  if (PyFunction_Check(callable.get())) {
    Python_function function{Python_context::get(), callable.get()};
    return function.invoke(args);
  }

  if (PyCallable_Check(callable.get())) {
    // Some Python callables, like built-in bound methods and callable
    // instances, do not convert into shell Function values.
    return invoke_python_callable(callable.get(), args, Python_context::get());
  }

  throw Exception::attrib_error("Invalid object method " + name);
}

Python_callable::Python_callable(Python_context *context, PyObject *callable,
                                 PyObject *introspection_target,
                                 uint64_t implicit_positional_args)
    : _py(context), m_callable(callable) {
  initialize_metadata(introspection_target, implicit_positional_args);
}

void Python_callable::initialize_metadata(PyObject *introspection_target,
                                          uint64_t implicit_positional_args) {
  py::Release name{PyObject_GetAttrString(introspection_target, "__name__")};
  Python_context::pystring_to_string(name.get(), &m_name);

  // Gets the number of arguments on the python function definition
  // NOTE: **kwargs is not accounted on co_argcount
  // This will be used to determine when the function should be called using
  // kwargs or not
  auto fcode = PyFunction_GetCode(introspection_target);
  py::Release arg_count{PyObject_GetAttrString(fcode, "co_argcount")};
  auto varg_count = _py->convert(arg_count.get());
  m_arg_count = varg_count.as_uint();

  if (m_arg_count >= implicit_positional_args) {
    m_arg_count -= implicit_positional_args;
  } else {
    m_arg_count = 0;
  }
}

Python_callable::~Python_callable() {
  WillEnterPython lock;
  m_callable.reset();
}

Python_function::Python_function(Python_context *context, PyObject *function)
    : Python_callable(context, function, function) {}

Python_bound_method::Python_bound_method(Python_context *context,
                                         PyObject *method)
    : Python_callable(context, method, PyMethod_GET_FUNCTION(method), 1) {}

py::Release Python_callable::call_with_error_translation(PyObject *args,
                                                         PyObject *kwargs) {
  return shcore::call_with_error_translation(callable(), args, kwargs, _py);
}

bool Python_callable::matches_kwargs_arity(size_t argc) const {
  return argc == (m_arg_count + 1);
}

const std::vector<std::pair<std::string, Value_type>> &
Python_callable::signature() const {
  // TODO:
  static std::vector<std::pair<std::string, Value_type>> tmp;
  return tmp;
}

Value_type Python_callable::return_type() const {
  // TODO:
  return Undefined;
}

bool Python_callable::operator==(
    [[maybe_unused]] const Function_base &other) const {
  // TODO:
  return false;
}

bool Python_callable::operator!=(
    [[maybe_unused]] const Function_base &other) const {
  // TODO:
  return false;
}

PyObject *Python_callable::invoke(PyObject *args) {
  // If the function caller provides more parameters than the ones defined in
  // the function, the last parameter should be handled as kwargs
  const auto argc = static_cast<size_t>(PyTuple_Size(args));
  PyObject *kwargs = nullptr;
  py::Release args_copy;
  PyObject *call_args = args;

  if (matches_kwargs_arity(argc) &&
      PyDict_Check(PyTuple_GetItem(args, argc - 1))) {
    kwargs = PyTuple_GetItem(args, argc - 1);
    args_copy = py::Release{PyTuple_GetSlice(args, 0, argc - 1)};
    call_args = args_copy.get();
  }

  return call_with_error_translation(call_args, kwargs).release();
}

Value Python_callable::invoke(const Argument_list &args) {
  WillEnterPython lock;

  auto argc = args.size();

  // If the function caller provides more parameters than the ones defined in
  // the function, the last parameter should be handled as follows:
  // - If Dictionary, it's data is passed as kwargs
  // - If Undefined, then kwards is empty
  // - Any other case will fall into passing it as normal parameter
  py::Release kw_args;

  if (matches_kwargs_arity(argc) &&
      (args[argc - 1].get_type() == shcore::Value_type::Map ||
       args[argc - 1].get_type() == shcore::Value_type::Undefined)) {
    // We remove the last parameter from the parameter list
    argc--;

    // Sets the kwargs from the dictionary if any
    if (args[argc].get_type() == shcore::Value_type::Map) {
      kw_args = py::Release{PyDict_New()};

      for (auto item : *args[argc].as_map()) {
        auto conv = _py->convert(item.second);
        PyDict_SetItemString(kw_args.get(), item.first.c_str(), conv.get());
      }
    }
  }

  const auto argv = convert(args, _py, argc);
  const auto ret_val = call_with_error_translation(argv.get(), kw_args.get());
  return _py->convert(ret_val.get());
}

}  // namespace shcore
