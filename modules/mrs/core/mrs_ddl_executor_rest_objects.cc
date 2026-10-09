/*
 * Copyright (c) 2026, MariaDB plc.
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

// The REST VIEW, PROCEDURE and FUNCTION statements: CREATE, ALTER, DROP,
// SHOW and SHOW CREATE. The data mapping of a statement (the GraphQL-like
// object) is turned into the metadata's object/field/reference model by the
// Mapping_builder below, using the columns, references and parameters the
// server reports for the database object.

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

#include "modules/mrs/core/mrs_ddl_executor.h"
#include "modules/mrs/core/mrs_metadata_rest_objects.h"
#include "modules/mrs/core/mrs_metadata_json.h"
#include "modules/mrs/core/mrs_strings.h"

namespace mrs {

using namespace ast;

namespace {

using metadata::Data_mapping;
using metadata::Data_mapping_field;
using metadata::Data_mapping_reference;

std::string kind_caption(Rest_object_kind kind) {
  switch (kind) {
    case Rest_object_kind::procedure:
      return "PROCEDURE";
    case Rest_object_kind::function:
      return "FUNCTION";
    case Rest_object_kind::script:
      return "SCRIPT";
    case Rest_object_kind::view:
      break;
  }
  return "VIEW";
}

// The object types a SHOW / SHOW CREATE statement of a kind covers.
std::vector<std::string> object_types_of(Rest_object_kind kind) {
  switch (kind) {
    case Rest_object_kind::procedure:
      return {"PROCEDURE"};
    case Rest_object_kind::function:
      return {"FUNCTION"};
    case Rest_object_kind::script:
      return {"SCRIPT"};
    case Rest_object_kind::view:
      break;
  }
  return {"TABLE", "VIEW"};
}

bool is_of_kind(const metadata::Rest_object &rest_object, Rest_object_kind kind) {
  const auto types = object_types_of(kind);
  return std::find(types.begin(), types.end(), rest_object.object_type) != types.end();
}

std::string format_caption(Result_format format) {
  switch (format) {
    case Result_format::item:
      return "ITEM";
    case Result_format::media:
      return "MEDIA";
    case Result_format::feed:
      break;
  }
  return "FEED";
}

std::string qualified_text(const Qualified_name &name) {
  return name.schema ? *name.schema + "." + name.name : name.name;
}

// The current REST schema, for statements without ON SCHEMA.
metadata::Schema current_schema(Db_session *session, const Executor_state &state) {
  if (state.current_schema_id) {
    if (const auto schema = metadata::get_schema(session, *state.current_schema_id)) {
      return *schema;
    }
  }
  throw std::runtime_error("No REST schema given.");
}

// The REST schema of a service, for statements with ON [SERVICE] SCHEMA.
metadata::Schema schema_of_service(Db_session *session, const Id &service_id,
                                   const Schema_selector &selector,
                                   const std::string &full_schema_path) {
  const auto schema = metadata::find_schema(session, service_id, selector.schema_path);
  if (!schema) {
    throw std::runtime_error("The REST schema `" + full_schema_path +
                             "` was not found.");
  }
  return *schema;
}

metadata::Rest_object_definition rest_object_definition(
    const Id &schema_id, std::string_view name, std::string_view request_path,
    std::string_view object_type, const Object_options &options) {
  metadata::Rest_object_definition definition;
  definition.rest_schema_id = schema_id;
  definition.name = std::string(name);
  definition.request_path = std::string(request_path);
  definition.object_type = std::string(object_type);
  if (options.enabled) definition.enabled = static_cast<int>(*options.enabled);
  definition.items_per_page = options.items_per_page;
  definition.requires_auth = options.requires_auth;
  if (options.format) definition.format = format_caption(*options.format);
  definition.comments = options.comments;
  definition.media_type = options.media_type;
  definition.auto_detect_media_type = options.media_type_autodetect;
  if (options.auth_procedure) {
    definition.auth_stored_procedure = qualified_text(*options.auth_procedure);
  }
  if (options.options) definition.options = options.options->value;
  definition.metadata = options.metadata;
  return definition;
}

metadata::Rest_object_changes rest_object_changes(
    const Object_options &options, const std::optional<std::string> &new_path) {
  metadata::Rest_object_changes changes;
  changes.request_path = new_path;
  if (options.enabled) changes.enabled = static_cast<int>(*options.enabled);
  changes.requires_auth = options.requires_auth;
  changes.items_per_page = options.items_per_page;
  if (options.format) changes.format = format_caption(*options.format);
  changes.comments = options.comments;
  changes.media_type = options.media_type;
  if (options.media_type_autodetect) changes.auto_detect_media_type = true;
  if (options.auth_procedure) {
    changes.auth_stored_procedure = qualified_text(*options.auth_procedure);
  }
  if (options.options) {
    changes.options = options.options->value;
    changes.merge_options = options.options->merge;
  }
  changes.metadata = options.metadata;
  return changes;
}

std::string mapping_options(const Crud_annotations &crud) {
  return metadata::mapping_options_json(crud.allow_insert(), crud.allow_update(),
                                        crud.allow_delete(), crud.is_no_check());
}

// Builds the objects of a rest_object from the data mapping of a statement.
// The columns, references and parameters come from the server; the mapping
// enables, renames and annotates them.
class Mapping_builder {
 public:
  Mapping_builder(Db_session *session, Id rest_object_id, std::string schema_name,
                  std::string name)
      : m_session(session),
        m_rest_object_id(std::move(rest_object_id)),
        m_schema_name(std::move(schema_name)),
        m_name(std::move(name)) {}

  // The RESULT object of a REST VIEW. Without a mapping all columns are
  // exposed.
  Data_mapping view_object(const std::optional<std::string> &class_name,
                                const Crud_annotations &crud,
                                const std::optional<Graphql_object> &mapping) {
    Data_mapping object = new_object(class_name, "RESULT", 0);
    object.options = mapping_options(crud);
    add_table_fields(&object, std::nullopt, m_schema_name, m_name, !mapping);
    if (mapping) apply_mapping(&object, *mapping, std::nullopt, false);
    return object;
  }

  // The PARAMETERS object of a routine. Without a PARAMETERS clause all
  // parameters are exposed; with FORCE, parameters the server does not
  // know are taken as given.
  Data_mapping parameters_object(
      std::string_view type, const std::optional<Named_graphql_object> &parameters,
      bool force) {
    Data_mapping object =
        new_object(parameters ? parameters->name : std::nullopt, "PARAMETERS", 0);

    for (const auto &parameter :
         metadata::get_routine_parameters(m_session, m_schema_name, m_name, type)) {
      Data_mapping_field field = new_field(object, std::nullopt);
      field.name = metadata::snake_to_camel_case(parameter.name);
      field.position = parameter.position;
      field.enabled = !parameters;

      json::Value column = json::Value::object();
      column.set("name", json::Value(parameter.name));
      column.set("in", json::Value(parameter.mode.find("IN") != std::string::npos));
      column.set("out", json::Value(parameter.mode.find("OUT") != std::string::npos));
      column.set("datatype", json::Value(parameter.datatype));
      // Routine parameters are nullable by nature
      column.set("not_null", json::Value(false));
      column.set("is_generated", json::Value(false));
      column.set("is_primary", json::Value(false));
      column.set("is_unique", json::Value(false));
      column.set("charset", optional_json(parameter.charset));
      column.set("collation", optional_json(parameter.collation));
      field.db_column = std::move(column);

      object.fields.push_back(std::move(field));
    }

    if (parameters) apply_mapping(&object, parameters->object, std::nullopt, force);
    return object;
  }

  // A RESULT object of a procedure: its columns are only known from the
  // statement.
  Data_mapping procedure_result(const Named_graphql_object &result,
                                     int position) {
    Data_mapping object = new_object(result.name, "RESULT", position);

    for (const auto &pair : result.object.fields) {
      if (!pair.nested.empty()) {
        throw std::runtime_error("The column `" + pair.source.name +
                                 "` of a RESULT cannot hold a nested object.");
      }
      Data_mapping_field field = new_field(object, std::nullopt);
      field.position = static_cast<int>(object.fields.size());
      json::Value column = json::Value::object();
      column.set("name", json::Value(pair.source.name));
      column.set("datatype", json::Value(datatype_of(pair)));
      field.db_column = std::move(column);
      apply_annotations(&object, &field, pair);
      object.fields.push_back(std::move(field));
    }
    return object;
  }

  // The RESULT object of a function: the single `result` value of the
  // return type the server reports.
  // The objects of a REST PROCEDURE or FUNCTION: the parameters, then the
  // result of a function or the result sets of a procedure.
  std::vector<Data_mapping> routine_objects(
      Rest_object_kind kind, const std::string &type,
      const std::optional<Named_graphql_object> &parameters,
      const std::vector<Named_graphql_object> &results, bool force) {
    std::vector<Data_mapping> objects{
        parameters_object(type, parameters, force)};
    if (kind == Rest_object_kind::function) {
      objects.push_back(
          function_result(results.empty() ? nullptr : &results.front()));
    } else {
      for (const auto &result : results) {
        objects.push_back(
            procedure_result(result, static_cast<int>(objects.size())));
      }
    }
    return objects;
  }

  Data_mapping function_result(const Named_graphql_object *result) {
    Data_mapping object =
        new_object(result ? result->name : std::nullopt, "RESULT", 1);

    Data_mapping_field field = new_field(object, std::nullopt);
    field.name = "result";
    field.position = 0;
    field.enabled = true;
    json::Value column = json::Value::object();
    column.set("name", json::Value("result"));
    column.set("datatype", optional_json(metadata::get_function_return_type(
                               m_session, m_schema_name, m_name)));
    column.set("not_null", json::Value(false));
    column.set("is_generated", json::Value(false));
    column.set("is_primary", json::Value(false));
    column.set("is_unique", json::Value(false));
    field.db_column = std::move(column);

    if (result) {
      for (const auto &pair : result->object.fields) {
        if (pair.source.name != "result" || !pair.nested.empty()) {
          throw_no_such_column(pair.source.name);
        }
        apply_annotations(&object, &field, pair);
      }
    }
    object.fields.push_back(std::move(field));
    return object;
  }

 private:
  static json::Value optional_json(const std::optional<std::string> &text) {
    return text ? json::Value(*text) : json::Value();
  }

  static std::string datatype_of(const Graphql_field &pair) {
    return pair.datatype ? to_lower(*pair.datatype) : "varchar(255)";
  }

  Data_mapping new_object(const std::optional<std::string> &name,
                               std::string kind, int position) {
    Data_mapping object;
    object.id = metadata::new_id(m_session);
    object.name = name.value_or("");
    object.kind = std::move(kind);
    object.position = position;
    return object;
  }

  Data_mapping_field new_field(const Data_mapping &object,
                         const std::optional<Id> &parent_reference_id) {
    Data_mapping_field field;
    field.id = metadata::new_id(m_session);
    field.data_mapping_id = object.id;
    field.parent_reference_id = parent_reference_id;
    return field;
  }

  [[noreturn]] void throw_no_such_column(const std::string &column) const {
    throw std::runtime_error("The column `" + column + "` does not exist on `" +
                             m_schema_name + "`.`" + m_name + "`.");
  }

  // Adds the columns and references of a table or view below a parent,
  // disabled unless asked otherwise; references are never enabled by
  // default.
  void add_table_fields(Data_mapping *object,
                        const std::optional<Id> &parent_reference_id,
                        const std::string &schema_name, const std::string &table,
                        bool enable_columns) {
    for (auto &column : metadata::get_table_columns_with_references(
             m_session, schema_name, table)) {
      Data_mapping_field field = new_field(*object, parent_reference_id);
      field.name = metadata::snake_to_camel_case(column.name);
      field.position = column.position;
      field.enabled = enable_columns && !column.is_reference();
      field.db_column = std::move(column.db_column);
      field.candidate_reference_mapping = std::move(column.reference_mapping);
      object->fields.push_back(std::move(field));
    }
  }

  void apply_mapping(Data_mapping *object, const Graphql_object &mapping,
                     const std::optional<Id> &parent_reference_id, bool force) {
    for (const auto &pair : mapping.fields) {
      if (pair.nested.empty()) {
        apply_column(object, pair, parent_reference_id, force);
      } else {
        apply_reference(object, pair, parent_reference_id);
      }
    }
  }

  Data_mapping_field *find_column(Data_mapping *object,
                            const std::optional<Id> &parent_reference_id,
                            const std::string &column_name) {
    for (auto &field : object->fields) {
      if (field.parent_reference_id == parent_reference_id && field.db_column &&
          field.column_name() == column_name) {
        return &field;
      }
    }
    return nullptr;
  }

  Data_mapping_field *find_reference_candidate(
      Data_mapping *object, const std::optional<Id> &parent_reference_id,
      const std::string &schema_name, const std::string &table) {
    for (auto &field : object->fields) {
      if (field.parent_reference_id != parent_reference_id ||
          !field.candidate_reference_mapping || field.reference) {
        continue;
      }
      const auto &mapping = *field.candidate_reference_mapping;
      if (mapping.get_string("referenced_schema") == schema_name &&
          mapping.get_string("referenced_table") == table) {
        return &field;
      }
    }
    return nullptr;
  }

  Data_mapping_reference *find_reference(Data_mapping *object, const Id &id) {
    for (auto &field : object->fields) {
      if (field.reference && field.reference->id == id) return &*field.reference;
    }
    return nullptr;
  }

  // `name: column @ANNOTATIONS`
  void apply_column(Data_mapping *object, const Graphql_field &pair,
                    const std::optional<Id> &parent_reference_id, bool force) {
    auto *field = find_column(object, parent_reference_id, pair.source.name);
    if (!field) {
      if (!force) throw_no_such_column(pair.source.name);
      field = &add_forced_field(object, pair, parent_reference_id);
    }
    apply_annotations(object, field, pair);
  }

  // A parameter the server does not know, taken from the statement (FORCE).
  Data_mapping_field &add_forced_field(Data_mapping *object,
                                 const Graphql_field &pair,
                                 const std::optional<Id> &parent_reference_id) {
    Data_mapping_field field = new_field(*object, parent_reference_id);
    field.position = static_cast<int>(object->fields.size());
    json::Value column = json::Value::object();
    column.set("name", json::Value(pair.source.name));
    column.set("datatype", json::Value(datatype_of(pair)));
    column.set("in", json::Value(pair.mode == Graphql_field::Mode::in ||
                                 pair.mode == Graphql_field::Mode::inout));
    column.set("out", json::Value(pair.mode == Graphql_field::Mode::out ||
                                  pair.mode == Graphql_field::Mode::inout));
    field.db_column = std::move(column);
    object->fields.push_back(std::move(field));
    return object->fields.back();
  }

  void apply_annotations(Data_mapping *object, Data_mapping_field *field,
                         const Graphql_field &pair) {
    field->name = pair.name;
    field->enabled = true;
    if (pair.no_check) field->no_check = true;
    if (pair.sortable) field->allow_sorting = true;
    if (pair.no_filtering) field->allow_filtering = false;
    if (pair.row_ownership) object->row_ownership_field_id = field->id;
    if (pair.key) field->db_column->set("is_primary", json::Value(true));
    if (pair.crud.no_update) field->no_update = true;
    if (pair.datatype) {
      field->db_column->set("datatype", json::Value(to_lower(*pair.datatype)));
    }
    if (pair.json_schema) field->json_schema = *pair.json_schema;
  }

  // `name: schema.table @ANNOTATIONS { ... }`
  void apply_reference(Data_mapping *object, const Graphql_field &pair,
                       const std::optional<Id> &parent_reference_id) {
    const std::string schema_name = pair.source.schema.value_or(m_schema_name);
    const std::string table = pair.source.name;

    auto *field = find_reference_candidate(object, parent_reference_id,
                                           schema_name, table);
    if (!field) {
      throw std::runtime_error("The table `" + schema_name + "`.`" + table +
                               "` has no reference to `" + m_schema_name +
                               "`.`" + m_name + "`.");
    }

    Data_mapping_reference reference;
    reference.id = metadata::new_id(m_session);
    reference.reference_mapping = std::move(*field->candidate_reference_mapping);
    reference.options = mapping_options(pair.crud);
    reference.unnest = pair.unnest;
    const Id reference_id = reference.id;
    const bool to_many = reference.kind() == "1:n";

    field->name = pair.name;
    field->enabled = true;
    field->candidate_reference_mapping.reset();
    field->reference = std::move(reference);
    // Adding the referenced fields moves the vector; the pointer is not
    // used below

    add_table_fields(object, reference_id, schema_name, table, false);
    apply_mapping(object, pair.nested.front(), reference_id, false);

    // Unnesting a list of documents into the parent only works by reducing
    // each document to a single value
    if (pair.unnest && to_many) reduce_to_single_column(object, reference_id);
  }

  void reduce_to_single_column(Data_mapping *object, const Id &reference_id) {
    auto *reference = find_reference(object, reference_id);

    // The columns of nested references count as well, e.g. the title of
    // an unnested n:1 film reference below an unnested 1:n film_actor one
    std::set<Id> references{reference_id};
    for (bool added = true; added;) {
      added = false;
      for (const auto &field : object->fields) {
        if (field.reference && field.parent_reference_id &&
            references.contains(*field.parent_reference_id) &&
            references.insert(field.reference->id).second) {
          added = true;
        }
      }
    }

    std::string reduce_to_name;
    for (const auto &field : object->fields) {
      if (!field.parent_reference_id ||
          !references.contains(*field.parent_reference_id) || !field.enabled ||
          field.reference) {
        continue;
      }
      if (reference->reduce_to_value_of_field_id) {
        throw std::runtime_error(
            "Only one column `" + reduce_to_name +
            "` must be defined for a N:1 unnest operation. The column `" +
            field.name + "` needs to be removed.");
      }
      reference->reduce_to_value_of_field_id = field.id;
      reduce_to_name = field.name;
    }
    if (!reference->reduce_to_value_of_field_id) {
      throw std::runtime_error(
          "At least one column must be defined for a N:1 unnest operation.");
    }
  }

  Db_session *m_session;
  Id m_rest_object_id;
  std::string m_schema_name;
  std::string m_name;
};

// Names the objects the statement left unnamed after the full path of the
// rest_object (/svc/sakila/city -> SvcSakilaCity), with a Params suffix for
// the parameters of a routine and a number for further results, made
// unique within the REST schema.
void assign_object_names(Db_session *session, const Id &schema_id,
                         const std::string &full_path, bool is_routine,
                         std::vector<Data_mapping> *objects) {
  std::vector<std::string> assigned;
  for (size_t i = 0; i < objects->size(); ++i) {
    auto &object = (*objects)[i];
    if (!object.name.empty()) continue;

    std::string name = metadata::path_to_pascal_case(full_path);
    size_t postfix = 2;
    if (is_routine) {
      if (object.kind == "PARAMETERS") name += "Params";
      if (i > 1) {
        postfix = std::max(postfix, i);
        name = metadata::path_to_pascal_case(full_path + std::to_string(postfix));
      }
    }

    while (std::find(assigned.begin(), assigned.end(), name) != assigned.end() ||
           metadata::object_name_in_use(session, schema_id, object.id, name)) {
      name = metadata::path_to_pascal_case(full_path + std::to_string(postfix));
      ++postfix;
    }
    object.name = name;
    assigned.push_back(name);
  }
}

// Runs the GRANT statements of a rest_object. With FORCE a failing grant,
// e.g. for a routine that does not exist yet, is reported as a warning.
void run_grants(Db_session *session, const std::vector<std::string> &grants,
                bool force, Statement_result *r) {
  for (const auto &grant : grants) {
    try {
      session->execute(grant);
    } catch (const Db_error &e) {
      if (!force) throw;
      r->warnings.push_back(Statement_result::Warning{"warning", e.code(), e.what()});
    }
  }
}

// Runs the grants of a rest_object that was just added. GRANT commits the
// transaction implicitly, so the object is removed again when they fail.
void run_grants_of_new_object(Db_session *session, const Id &id,
                              const std::vector<std::string> &grants, bool force,
                              Statement_result *r) {
  try {
    run_grants(session, grants, force, r);
  } catch (...) {
    try {
      metadata::delete_rest_object(session, id);
    } catch (...) {
      // The grant error is the one to report
    }
    throw;
  }
}

// The database object a CREATE statement names: `schema.name` or `name` in
// the database schema of the REST schema.
std::pair<std::string, std::string> database_object_name(
    const Qualified_name &object, const metadata::Schema &schema) {
  return {object.schema.value_or(schema.name), object.name};
}

// REVOKE and GRANT commit the transaction they run in, so the privileges of
// the data provider role are changed before the metadata transaction of an
// ALTER, and the old ones are granted again when anything after fails:
// the rest_object and its privileges then stay in step either way. Nothing is
// revoked when the statements do not change.
//
// A rest_object created with FORCE may name a routine or table that does not
// exist (yet); granting on it is then reported as a warning.
void restore_grants(Db_session *session, const metadata::Rest_object &rest_object,
                    const std::vector<std::string> &old_grants) noexcept {
  try {
    metadata::revoke_all_from_rest_object(session, rest_object.schema_name,
                                        rest_object.name, rest_object.object_type);
  } catch (...) {
    // The error of the statement is the one to report
  }
  for (const auto &grant : old_grants) {
    try {
      session->execute(grant);
    } catch (...) {
      // A grant that was a warning before fails again; the rest go on
    }
  }
}

// Returns whether the privileges changed, so the caller knows to restore
// them when its metadata change fails.
bool regrant(Db_session *session, const metadata::Rest_object &rest_object,
             const std::vector<std::string> &old_grants,
             const std::vector<std::string> &new_grants, Statement_result *r) {
  constexpr int k_no_such_routine = 1305;  // ER_SP_DOES_NOT_EXIST
  constexpr int k_no_such_table = 1146;    // ER_NO_SUCH_TABLE

  if (old_grants == new_grants) return false;
  metadata::revoke_all_from_rest_object(session, rest_object.schema_name,
                                      rest_object.name, rest_object.object_type);
  try {
    for (const auto &grant : new_grants) {
      try {
        session->execute(grant);
      } catch (const Db_error &e) {
        if (e.code() != k_no_such_routine && e.code() != k_no_such_table) throw;
        r->warnings.push_back(
            Statement_result::Warning{"warning", e.code(), e.what()});
      }
    }
  } catch (...) {
    restore_grants(session, rest_object, old_grants);
    throw;
  }
  return true;
}

// Grants the old privileges again when the metadata change after a
// regrant() fails; keep() once the change is committed.
class Grant_restorer {
 public:
  Grant_restorer(Db_session *session, const metadata::Rest_object &rest_object,
                 const std::vector<std::string> &old_grants, bool active)
      : m_session(session),
        m_rest_object(rest_object),
        m_old_grants(old_grants),
        m_active(active) {}
  ~Grant_restorer() {
    if (m_active) restore_grants(m_session, m_rest_object, m_old_grants);
  }
  Grant_restorer(const Grant_restorer &) = delete;
  Grant_restorer &operator=(const Grant_restorer &) = delete;

  void keep() { m_active = false; }

 private:
  Db_session *m_session;
  const metadata::Rest_object &m_rest_object;
  const std::vector<std::string> &m_old_grants;
  bool m_active;
};

}  // namespace

metadata::Schema Ddl_executor::rest_object_schema(
    const std::optional<Schema_selector> &given) {
  if (!given) return current_schema(m_session, *m_state);
  return schema_of_service(m_session, require_service(given), *given,
                           full_schema_path(given));
}

// -- CREATE -------------------------------------------------------------------

void Ddl_executor::do_execute(const Create_rest_view &s, Statement_result *r) {
  const auto full_path = full_schema_path(s.on, s.path);
  set_failure_context("Failed to create the REST VIEW `" + full_path + "`.");

  Db_transaction transaction(m_session);

  const auto schema = rest_object_schema(s.on);
  const auto [schema_name, name] = database_object_name(s.object, schema);

  const auto object_type = metadata::database_object_type(m_session, schema_name, name);
  if (!object_type) {
    throw std::runtime_error("The table or view `" + schema_name + "`.`" + name +
                             "` does not exist.");
  }

  if (keep_existing(
          s.flags,
          [&] { return metadata::find_rest_object(m_session, schema.id, s.path); },
          [&](const auto &existing) {
            metadata::delete_rest_object(m_session, existing.id);
          })) {
    r->message = "REST VIEW `" + full_path + "` created successfully.";
    transaction.commit();
    return;
  }

  const Id id = metadata::new_id(m_session);
  Mapping_builder builder(m_session, id, schema_name, name);
  std::vector<Data_mapping> objects{
      builder.view_object(s.class_name, s.crud, s.mapping)};
  assign_object_names(m_session, schema.id, full_path, false, &objects);

  auto definition =
      rest_object_definition(schema.id, name, s.path, *object_type, s.options);
  definition.id = id;
  metadata::add_rest_object(m_session, definition, objects);

  run_grants_of_new_object(
      m_session, id,
      metadata::grant_statements(
          m_session, schema_name, name, *object_type,
          metadata::calculate_crud_operations(*object_type, objects),
          objects, definition.options),
      false, r);

  transaction.commit();

  r->message = "REST VIEW `" + full_path + "` created successfully.";
}

void Ddl_executor::do_execute(const Create_rest_routine &s, Statement_result *r) {
  const auto type = kind_caption(s.kind);
  const auto full_path = full_schema_path(s.on, s.path);
  set_failure_context("Failed to create the REST " + type + " `" + full_path + "`.");

  Db_transaction transaction(m_session);

  const auto schema = rest_object_schema(s.on);
  const auto [schema_name, name] = database_object_name(s.object, schema);

  if (keep_existing(
          s.flags,
          [&] { return metadata::find_rest_object(m_session, schema.id, s.path); },
          [&](const auto &existing) {
            metadata::delete_rest_object(m_session, existing.id);
          })) {
    r->message = "REST " + type + " `" + full_path + "` created successfully.";
    transaction.commit();
    return;
  }

  const Id id = metadata::new_id(m_session);
  Mapping_builder builder(m_session, id, schema_name, name);
  auto objects = builder.routine_objects(s.kind, type, s.parameters,
                                         s.results, s.force);
  assign_object_names(m_session, schema.id, full_path, true, &objects);

  auto definition = rest_object_definition(schema.id, name, s.path, type, s.options);
  definition.id = id;
  metadata::add_rest_object(m_session, definition, objects);

  run_grants_of_new_object(
      m_session, id,
      metadata::grant_statements(
          m_session, schema_name, name, type,
          metadata::calculate_crud_operations(type, objects),
          objects, definition.options),
      s.force, r);

  transaction.commit();

  r->message = "REST " + type + " `" + full_path + "` created successfully.";
}

// -- ALTER --------------------------------------------------------------------

void Ddl_executor::do_execute(const Alter_rest_view &s, Statement_result *r) {
  const auto full_path = full_schema_path(s.on, s.path);
  set_failure_context("Failed to update the REST VIEW `" + full_path + "`.");

  const auto schema = rest_object_schema(s.on);
  const auto rest_object = metadata::find_rest_object(m_session, schema.id, s.path);
  if (!rest_object || !is_of_kind(*rest_object, Rest_object_kind::view)) {
    throw std::runtime_error("The given REST VIEW `" + full_path +
                             "` could not be found.");
  }

  auto changes = rest_object_changes(s.options, s.new_path);
  auto objects = metadata::get_objects(m_session, rest_object->id);
  const auto old_grants = metadata::grant_statements(
      m_session, rest_object->schema_name, rest_object->name, rest_object->object_type,
      rest_object->crud_operations, objects, rest_object->options);

  bool new_mapping = false;
  if (s.class_def) {
    if (s.class_def->mapping) {
      // A new mapping replaces the whole data mapping
      Mapping_builder builder(m_session, rest_object->id, rest_object->schema_name,
                              rest_object->name);
      objects = {builder.view_object(s.class_def->name, s.class_def->crud,
                                     s.class_def->mapping)};
      new_mapping = true;
    } else {
      // Only the name and the data mapping flags change, the fields stay
      if (objects.empty()) {
        throw std::runtime_error("The given REST object `" + full_path +
                                 "` does not have a result definition defined.");
      }
      auto &object = objects.front();
      if (metadata::object_name_in_use(m_session, schema.id, object.id,
                                       s.class_def->name)) {
        throw std::runtime_error("The object name " + s.class_def->name +
                                 " is already in use on this REST schema.");
      }
      object.name = s.class_def->name;
      object.options = mapping_options(s.class_def->crud);
    }
    changes.crud_operations =
        metadata::calculate_crud_operations(rest_object->object_type, objects);
  }

  const auto new_grants = metadata::grant_statements(
      m_session, rest_object->schema_name, rest_object->name, rest_object->object_type,
      changes.crud_operations.value_or(rest_object->crud_operations), objects,
      metadata::options_after(m_session, *rest_object, changes));
  Grant_restorer restorer(m_session, *rest_object, old_grants,
                          regrant(m_session, *rest_object, old_grants, new_grants, r));

  Db_transaction transaction(m_session);
  if (new_mapping) {
    metadata::set_objects(m_session, rest_object->id, objects);
  } else if (s.class_def) {
    const auto &object = objects.front();
    metadata::update_object(m_session, object.id, object.name, object.options);
  }
  metadata::update_rest_object(m_session, rest_object->id, changes);
  transaction.commit();
  restorer.keep();

  r->affected_items_count = 1;
}

void Ddl_executor::do_execute(const Alter_rest_routine &s, Statement_result *r) {
  const auto type = kind_caption(s.kind);
  const auto full_path = full_schema_path(s.on, s.path);
  set_failure_context("Failed to update the REST " + type + " `" + full_path + "`.");

  const auto schema = rest_object_schema(s.on);
  const auto rest_object = metadata::find_rest_object(m_session, schema.id, s.path);
  if (!rest_object || !is_of_kind(*rest_object, s.kind)) {
    throw std::runtime_error("The given REST " + type + " `" + full_path +
                             "` could not be found.");
  }

  auto changes = rest_object_changes(s.options, s.new_path);

  // The privileges of a routine (EXECUTE) do not depend on its objects
  const auto old_grants = metadata::grant_statements(
      m_session, rest_object->schema_name, rest_object->name, rest_object->object_type,
      rest_object->crud_operations, {}, rest_object->options);

  // A PARAMETERS or RESULT clause replaces the whole data mapping
  const bool new_mapping = s.parameters || !s.results.empty();
  std::vector<Data_mapping> objects;
  if (new_mapping) {
    Mapping_builder builder(m_session, rest_object->id, rest_object->schema_name,
                            rest_object->name);
    objects = builder.routine_objects(s.kind, type, s.parameters, s.results,
                                      false);
    changes.crud_operations = metadata::calculate_crud_operations(type, objects);
  }

  const auto new_grants = metadata::grant_statements(
      m_session, rest_object->schema_name, rest_object->name, rest_object->object_type,
      changes.crud_operations.value_or(rest_object->crud_operations), {},
      metadata::options_after(m_session, *rest_object, changes));
  Grant_restorer restorer(m_session, *rest_object, old_grants,
                          regrant(m_session, *rest_object, old_grants, new_grants, r));

  Db_transaction transaction(m_session);
  if (new_mapping) {
    // The old objects go first, so their names are free for the new ones
    metadata::set_objects(m_session, rest_object->id, {});
    assign_object_names(m_session, schema.id, full_path, true, &objects);
    metadata::set_objects(m_session, rest_object->id, objects);
  }
  metadata::update_rest_object(m_session, rest_object->id, changes);
  transaction.commit();
  restorer.keep();

  r->affected_items_count = 1;
}

// -- DROP ---------------------------------------------------------------------

void Ddl_executor::do_execute(const Drop_rest_object &s, Statement_result *r) {
  const auto caption = kind_caption(s.kind);
  const auto full_path = full_schema_path(s.from, s.path);
  set_failure_context("Failed to drop the REST " + caption + " `" + full_path + "`.");

  Db_transaction transaction(m_session);

  const auto schema = rest_object_schema(s.from);
  auto rest_object = metadata::find_rest_object(m_session, schema.id, s.path);
  if (rest_object && !is_of_kind(*rest_object, s.kind)) rest_object.reset();
  if (!rest_object && !s.if_exists) {
    throw std::runtime_error("The given REST " + caption + " `" + full_path +
                             "` could not be found.");
  }
  if (rest_object) {
    metadata::delete_rest_object(m_session, rest_object->id);
  }

  transaction.commit();
  r->message = "REST " + caption + " `" + full_path + "` dropped successfully.";
}

// -- SHOW ---------------------------------------------------------------------

void Ddl_executor::do_execute(const Show_rest_objects &s, Statement_result *r) {
  set_failure_context("Cannot SHOW the REST db objects.");

  const auto schema = rest_object_schema(s.on);
  const auto rest_objects =
      metadata::get_rest_objects(m_session, schema.id, object_types_of(s.kind));

  if (s.format == Output_format::json) {
    json::Value::Array docs;
    for (const auto &rest_object : rest_objects) {
      docs.push_back(metadata::rest_object_json(m_session, rest_object, false));
    }
    r->set_json("REST " + kind_caption(s.kind) + "S",
                json::Value(std::move(docs)).dump(true));
    return;
  }

  r->columns = {"REST DB Object", "enabled"};
  for (const auto &rest_object : rest_objects) {
    auto &row = r->add_row();
    row.emplace_back(rest_object.request_path);
    row.emplace_back(metadata::enabled_caption(rest_object.enabled));
  }
}

void Ddl_executor::do_execute(const Show_create_rest_object &s,
                              Statement_result *r) {
  const auto caption = kind_caption(s.kind);
  const auto full_path = full_schema_path(s.on, s.path);
  set_failure_context("Failed to get the REST " + caption + " `" + full_path + "`.");

  const auto schema = rest_object_schema(s.on);
  const auto rest_object = metadata::find_rest_object(m_session, schema.id, s.path);
  if (!rest_object) {
    throw std::runtime_error("The given REST " + caption + " `" + full_path +
                             "` could not be found.");
  }
  if (!metadata::has_objects(m_session, rest_object->id)) {
    throw std::runtime_error("The given REST object `" + full_path +
                             "` does not have a result definition defined.");
  }
  if (!is_of_kind(*rest_object, s.kind)) {
    throw std::runtime_error("The given REST object `" + full_path +
                             "` is not a REST " + caption + ".");
  }

  r->columns = {"CREATE REST " + caption};
  r->add_row().emplace_back(
      s.format == Output_format::json
          ? metadata::rest_object_json(m_session, *rest_object).dump(true)
          : metadata::rest_object_create_statement(m_session, *rest_object));
}

// -- SHOW REST COLUMNS ----------------------------------------------------

namespace {

const char *source_caption(Show_rest_columns::Source source) {
  switch (source) {
    case Show_rest_columns::Source::table:
      return "TABLE";
    case Show_rest_columns::Source::view:
      return "VIEW";
    case Show_rest_columns::Source::procedure:
      return "PROCEDURE";
    case Show_rest_columns::Source::function:
      return "FUNCTION";
    case Show_rest_columns::Source::any:
      break;
  }
  return "";
}

Db_value yes_no(const json::Value &doc, std::string_view key) {
  const auto *value = doc.get(key);
  if (!value || value->is_null()) return Db_value(nullptr);
  return Db_value(value->as_bool() ? "YES" : "NO");
}

Db_value json_text(const json::Value &doc, std::string_view key) {
  const auto *value = doc.get(key);
  if (!value || !value->is_string()) return Db_value(nullptr);
  return Db_value(value->as_string());
}

// "n:1 sakila.country (country_id = country_id)"
std::string reference_caption(const json::Value &mapping) {
  std::string columns;
  if (const auto *column_mapping = mapping.get("column_mapping");
      column_mapping && column_mapping->is_array()) {
    for (const auto &pair : column_mapping->as_array()) {
      if (!columns.empty()) columns += ", ";
      columns += pair.get_string("base") + " = " + pair.get_string("ref");
    }
  }
  return mapping.get_string("kind") + " " +
         mapping.get_string("referenced_schema") + "." +
         mapping.get_string("referenced_table") + " (" + columns + ")";
}

}  // namespace

void Ddl_executor::do_execute(const Show_rest_columns &s, Statement_result *r) {
  // The schema defaults to the database schema of the current REST schema,
  // then to the session's current database
  std::string schema_name;
  if (s.object.schema) {
    schema_name = *s.object.schema;
  } else if (m_state->current_schema_id) {
    if (const auto schema = metadata::get_schema(m_session, *m_state->current_schema_id)) {
      schema_name = schema->name;
    }
  }
  if (schema_name.empty()) {
    const auto result = m_session->query("SELECT DATABASE() AS db");
    if (!result.empty() && !result.first()["db"].is_null()) {
      schema_name = result.first()["db"].as_string();
    }
  }
  const auto target =
      (schema_name.empty() ? "" : sql::quote_identifier(schema_name) + ".") +
      sql::quote_identifier(s.object.name);
  set_failure_context("Cannot SHOW the REST COLUMNS of " + target + ".");
  if (schema_name.empty()) throw std::runtime_error("No database schema selected.");

  // The type of the database object, as given or detected
  std::string type = source_caption(s.source);
  const auto table_type =
      metadata::database_object_type(m_session, schema_name, s.object.name);
  if (type.empty()) {
    if (table_type) {
      type = *table_type;
    } else if (metadata::routine_exists(m_session, schema_name, s.object.name,
                                        "FUNCTION")) {
      type = "FUNCTION";
    } else if (metadata::routine_exists(m_session, schema_name, s.object.name,
                                        "PROCEDURE")) {
      type = "PROCEDURE";
    } else {
      throw std::runtime_error("The database object " + target +
                               " was not found.");
    }
  } else if (type == "TABLE" || type == "VIEW") {
    if (!table_type) {
      throw std::runtime_error("The " + to_lower(type) + " " + target +
                               " was not found.");
    }
    if (*table_type != type) {
      throw std::runtime_error(target + " is a " + to_lower(*table_type) +
                               ", not a " + to_lower(type) + ".");
    }
  } else if (!metadata::routine_exists(m_session, schema_name, s.object.name,
                                       type)) {
    throw std::runtime_error("The " + to_lower(type) + " " + target +
                             " was not found.");
  }

  const bool routine = type == "PROCEDURE" || type == "FUNCTION";
  std::vector<metadata::Table_column> columns;
  std::vector<metadata::Routine_parameter> parameters;
  std::optional<std::string> return_type;
  if (routine) {
    parameters = metadata::get_routine_parameters(m_session, schema_name,
                                                  s.object.name, type);
    if (type == "FUNCTION") {
      return_type = metadata::get_function_return_type(m_session, schema_name,
                                                       s.object.name);
    }
  } else {
    columns = metadata::get_table_columns_with_references(m_session, schema_name,
                                                          s.object.name);
  }

  if (s.format == Output_format::json) {
    r->columns = {"REST COLUMNS"};
    r->add_row().emplace_back(
        (routine ? metadata::routine_json(schema_name, s.object.name, type,
                                          parameters, return_type)
                 : metadata::table_columns_json(schema_name, s.object.name,
                                                type, columns))
            .dump(true));
    return;
  }

  r->columns = {"position",   "name",          "kind",     "datatype",
                "not_null",   "is_primary",    "id_generation", "reference"};
  for (const auto &column : columns) {
    auto &row = r->add_row();
    row.emplace_back(static_cast<int64_t>(column.position));
    row.emplace_back(column.name);
    if (column.reference_mapping) {
      row.emplace_back("REFERENCE");
      for (int i = 0; i < 4; ++i) row.emplace_back(nullptr);
      row.emplace_back(reference_caption(*column.reference_mapping));
    } else {
      const auto db_column =
          column.db_column ? *column.db_column : json::Value::object();
      row.emplace_back("COLUMN");
      row.push_back(json_text(db_column, "datatype"));
      row.push_back(yes_no(db_column, "not_null"));
      row.push_back(yes_no(db_column, "is_primary"));
      row.push_back(json_text(db_column, "id_generation"));
      row.emplace_back(nullptr);
    }
  }
  for (const auto &parameter : parameters) {
    auto &row = r->add_row();
    row.emplace_back(static_cast<int64_t>(parameter.position));
    row.emplace_back(parameter.name);
    row.emplace_back(parameter.mode);
    row.emplace_back(parameter.datatype);
    for (int i = 0; i < 4; ++i) row.emplace_back(nullptr);
  }
  if (return_type) {
    auto &row = r->add_row();
    row.emplace_back(static_cast<int64_t>(0));
    row.emplace_back(nullptr);
    row.emplace_back("RETURN");
    row.emplace_back(*return_type);
    for (int i = 0; i < 4; ++i) row.emplace_back(nullptr);
  }
}

}  // namespace mrs
