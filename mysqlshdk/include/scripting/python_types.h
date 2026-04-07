/*
 * Copyright (c) 2016, 2026, Oracle and/or its affiliates.
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

#ifndef MYSQLSHDK_INCLUDE_SCRIPTING_PYTHON_TYPES_H_
#define MYSQLSHDK_INCLUDE_SCRIPTING_PYTHON_TYPES_H_

// python_context.h has to be included first

#include "mysqlshdk/include/scripting/python_context.h"

#include <memory>
#include <string>
#include <vector>

#include "mysqlshdk/include/scripting/types.h"

namespace shcore {

class SHCORE_PUBLIC Python_object : public Object_bridge {
 public:
  explicit Python_object(PyObject *object);
  ~Python_object() override;

  bool operator==(const Object_bridge &other) const override;

  std::string class_name() const override;

  std::string &append_repr(std::string &s_out) const override;

  std::string &append_descr(std::string &s_out, int indent = -1,
                            int quote_strings = 0) const override;

  std::vector<std::string> get_members() const override;

  Value get_member(const std::string &prop) const override;

  bool has_member(const std::string &prop) const override;

  void set_member(const std::string &prop, Value value) override;

  bool is_indexed() const override;

  Value get_member(size_t index) const override;

  void set_member(size_t index, Value value) override;

  size_t length() const override;

  bool has_method(const std::string &name) const override;

  Value call(const std::string &name, const Argument_list &args) override;

  inline PyObject *object() const { return m_object.get(); }

 private:
  py::Store m_object;
  std::string m_class;
};

class SHCORE_PUBLIC Python_callable : public Function_base {
 public:
  ~Python_callable() override;

  const std::string &name() const override { return m_name; }

  const std::vector<std::pair<std::string, Value_type>> &signature()
      const override;

  Value_type return_type() const override;

  bool operator==(const Function_base &other) const override;

  bool operator!=(const Function_base &other) const;

  Value invoke(const Argument_list &args) override;

  PyObject *invoke(PyObject *args);

 protected:
  Python_callable(Python_context *context, PyObject *callable,
                  PyObject *introspection_target,
                  uint64_t implicit_positional_args = 0);

  inline PyObject *callable() const { return m_callable.get(); }

 private:
  void initialize_metadata(PyObject *introspection_target,
                           uint64_t implicit_positional_args);

  bool matches_kwargs_arity(size_t argc) const;

  py::Release call_with_error_translation(PyObject *args, PyObject *kwargs);

  Python_context *_py{nullptr};
  py::Store m_callable;
  std::string m_name;
  uint64_t m_arg_count{0};
};

class SHCORE_PUBLIC Python_function final : public Python_callable {
 public:
  Python_function(Python_context *context, PyObject *function);
};

class SHCORE_PUBLIC Python_bound_method final : public Python_callable {
 public:
  Python_bound_method(Python_context *context, PyObject *method);
};

}  // namespace shcore

#endif  // MYSQLSHDK_INCLUDE_SCRIPTING_PYTHON_TYPES_H_
