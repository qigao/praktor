#include "projection.h"
#include <idl_contract.h>
#include <schema_cmeta.h>
#include <salts/error_codes.h>
#include <fmt.h>
#include <string.h>
#include <tstr.h>

enum { PROJECTION_MAX_DEPTH = 128 };
typedef struct projection { int status; char *error; size_t capacity; } projection;
static int fail(projection *p, int status, const char *message, const char *detail) {
    if (p->status == SALTS_OK) {
        p->status = status;
        if (p->error && p->capacity) (void)fmt(p->error, p->capacity, "{}{}", message, detail ? detail : "");
    }
    return status;
}
/* Both helpers consume an owned value even on failure. JSON's checked APIs
 * retain ownership on failure; centralizing that rule prevents OOM leaks. */
static int put(projection *p, json_value_t *object, const char *key, json_value_t *value) {
    if (!value || !json_object_add_checked(object, key, value)) {
        json_free(value); return fail(p, SALTS_ENOMEM, "Cannot allocate schema field: ", key);
    }
    return SALTS_OK;
}
static int push(projection *p, json_value_t *array, json_value_t *value) {
    if (!value || !json_array_add_checked(array, value)) {
        json_free(value); return fail(p, SALTS_ENOMEM, "Cannot allocate schema array", NULL);
    }
    return SALTS_OK;
}
static int text(projection *p, json_value_t *object, const char *key, const char *value) {
    return put(p, object, key, value ? json_create_string(value) : NULL);
}
static int literal(projection *p, json_value_t *object, const char *key, const char *value) {
    json_value_t *parsed = value ? json_parse(value, strlen(value)) : NULL;
    if (!parsed) return fail(p, SALTS_EINVAL, "Invalid IDL JSON literal: ", key);
    return put(p, object, key, parsed);
}
static int merge(projection *p, json_value_t *destination, const json_value_t *source, size_t depth) {
    size_t i, j, k;
    if (depth > PROJECTION_MAX_DEPTH) return fail(p, SALTS_EINVAL, "Schema overlay nesting limit exceeded", NULL);
    for (i = 0; i < json_object_size(source); ++i) {
        vstr key = json_object_key_v(source, i);
        const json_value_t *value = json_object_value(source, i);
        json_value_t *existing = json_object_get_v(destination, key);
        if (!existing) {
            json_value_t *copy = json_clone(value);
            if (!copy || !json_object_add_n(destination, key.data, key.len, copy)) {
                json_free(copy); return fail(p, SALTS_ENOMEM, "Cannot clone schema overlay", NULL);
            }
        } else if (key.len == 8 && memcmp(key.data, "required", 8) == 0 && json_type(existing) == JSON_ARRAY) {
            if (json_type(value) != JSON_ARRAY) return fail(p, SALTS_EINVAL, "Required fields must be an array", NULL);
            for (j = 0; j < json_array_size(value); ++j) {
                const json_value_t *item = json_array_get(value, j);
                if (json_type(item) != JSON_STRING) return fail(p, SALTS_EINVAL, "Required field must be a string", NULL);
                for (k = 0; k < json_array_size(existing); ++k) {
                    const json_value_t *prior = json_array_get(existing, k);
                    if (json_type(prior) != JSON_STRING) return fail(p, SALTS_EINVAL, "Required field must be a string", NULL);
                    if (json_string_len(prior) == json_string_len(item) &&
                        memcmp(json_string(prior), json_string(item), json_string_len(item)) == 0)
                        return fail(p, SALTS_EINVAL, "Duplicate IDL/editor required field: ", json_string(item));
                }
                if (push(p, existing, json_clone(item)) != SALTS_OK) return p->status;
            }
        } else if (json_type(existing) == JSON_OBJECT && json_type(value) == JSON_OBJECT) {
            if (merge(p, existing, value, depth + 1) != SALTS_OK) return p->status;
        } else return fail(p, SALTS_EINVAL, "Editor overlay duplicates IDL-owned keyword: ", json_object_key(source, i));
    }
    return SALTS_OK;
}
static json_value_t *field_shape(projection *p, const IdlContract *contract, const IdlField *field, size_t depth) {
    schema_cmeta_field_type resolved = {0};
    json_value_t *shape;
    size_t i;
    if (depth > PROJECTION_MAX_DEPTH) { fail(p, SALTS_EINVAL, "IDL shape nesting limit exceeded", NULL); return NULL; }
    if (!schema_cmeta_field_resolve(contract, field, &resolved)) {
        fail(p, SALTS_EINVAL, "Unresolved IDL field: ", field->name); return NULL;
    }
    shape = json_create_object();
    if (!shape) { fail(p, SALTS_ENOMEM, "Cannot allocate schema shape", NULL); return NULL; }
    switch (resolved.kind) {
    case CMETA_DATA_BOOL: text(p, shape, "type", "boolean"); break;
    case CMETA_DATA_SINT:
    case CMETA_DATA_UINT: text(p, shape, "type", "integer"); break;
    case CMETA_DATA_FLOAT: text(p, shape, "type", "number"); break;
    case CMETA_DATA_STRING: text(p, shape, "type", "string"); break;
    case CMETA_DATA_STRUCT:
        if (!idl_contract_find_data(contract, field->type_name)) { fail(p, SALTS_EINVAL, "Unknown IDL record reference", NULL); break; }
        if (strcmp(field->type_name, "Workflow") == 0) text(p, shape, "$ref", "#");
        else {
            tstr ref = tstr_new_len("#/$defs/", 8), grown;
            if (!ref) { fail(p, SALTS_ENOMEM, "Cannot allocate IDL reference", NULL); break; }
            grown = tstr_cat(ref, field->type_name);
            if (!grown) { tstr_free(ref); fail(p, SALTS_ENOMEM, "Cannot allocate IDL reference", NULL); break; }
            ref = grown; text(p, shape, "$ref", ref); tstr_free(ref);
        }
        break;
    case CMETA_DATA_SEQUENCE: {
        IdlField element = {0};
        if (field->collection_kind != IDL_COLLECTION_LIST || !field->inner_type) {
            fail(p, SALTS_EINVAL, "Only IDL lists are supported by the editor projection", NULL); break;
        }
        element.name = field->name; element.type_name = field->inner_type;
        if (text(p, shape, "type", "array") == SALTS_OK) {
            json_value_t *items = field_shape(p, contract, &element, depth + 1);
            if (items) put(p, shape, "items", items);
        }
        break;
    }
    default: fail(p, SALTS_EINVAL, "Unsupported IDL shape in editor projection: ", field->name); break;
    }
    if (field->nullable) fail(p, SALTS_EINVAL, "Nullable IDL fields need an explicit editor projection", NULL);
    for (i = 0; p->status == SALTS_OK && i < field->constraint_count; ++i) {
        const IdlConstraint *constraint = &field->constraints[i];
        const char *kind = constraint->kind;
        if (strcmp(kind, "min") == 0 || strcmp(kind, "max") == 0)
            literal(p, shape, strcmp(kind, "min") == 0 ? "minimum" : "maximum", constraint->value);
        else if (strcmp(kind, "size") == 0) {
            int is_string = resolved.kind == CMETA_DATA_STRING;
            if (!is_string && resolved.kind != CMETA_DATA_SEQUENCE) {
                fail(p, SALTS_EINVAL, "Size constraint requires a string or list", NULL); break;
            }
            if (constraint->minimum) literal(p, shape, is_string ? "minLength" : "minItems", constraint->minimum);
            if (p->status == SALTS_OK && constraint->maximum) literal(p, shape, is_string ? "maxLength" : "maxItems", constraint->maximum);
        } else if (strcmp(kind, "pattern") == 0) text(p, shape, "pattern", constraint->pattern);
        else fail(p, SALTS_EINVAL, "Unsupported IDL constraint: ", kind);
    }
    if (p->status == SALTS_OK && field->default_value) {
        if (resolved.kind == CMETA_DATA_STRING) text(p, shape, "default", field->default_value);
        else literal(p, shape, "default", field->default_value);
    }
    if (p->status != SALTS_OK) { json_free(shape); return NULL; }
    return shape;
}
static json_value_t *message_shape(projection *p, const IdlContract *contract, const IdlDataDecl *decl) {
    json_value_t *schema = json_create_object(), *properties = json_create_object(), *required = json_create_array();
    size_t j, k;
    if (!schema || !properties || !required) { fail(p, SALTS_ENOMEM, "Cannot allocate IDL message", NULL); goto failed; }
    for (j = 0; j < decl->field_count && p->status == SALTS_OK; ++j) {
        const IdlField *field = &decl->fields[j];
        const char *name = field->name;
        json_value_t *shape;
        if (idl_annotation_count(field->annotations, field->annotation_count, "name") > 1) {
            fail(p, SALTS_EINVAL, "An IDL field may have only one projected name", NULL); break;
        }
        for (k = 0; k < field->annotation_count; ++k) {
            const IdlAnnotation *annotation = &field->annotations[k];
            if (strcmp(annotation->name, "name") != 0 || annotation->argument_count != 1) {
                fail(p, SALTS_EINVAL, "Unsupported IDL field annotation", NULL); break;
            }
            name = idl_annotation_argument(annotation, 0);
        }
        if (p->status != SALTS_OK) break;
        if (!name || !*name || json_object_get(properties, name)) {
            fail(p, SALTS_EINVAL, "Duplicate or empty projected IDL field name", NULL); break;
        }
        shape = field_shape(p, contract, field, 0);
        if (!shape || put(p, properties, name, shape) != SALTS_OK) break;
        if (!field->optional) push(p, required, json_create_string(name));
    }
    if (p->status != SALTS_OK) goto failed;
    if (text(p, schema, "type", "object") != SALTS_OK) goto failed;
    { json_value_t *owned = properties; properties = NULL; if (put(p, schema, "properties", owned) != SALTS_OK) goto failed; }
    if (json_array_size(required)) {
        json_value_t *owned = required; required = NULL;
        if (put(p, schema, "required", owned) != SALTS_OK) goto failed;
    }
    json_free(required); return schema;
failed:
    json_free(schema); json_free(properties); json_free(required); return NULL;
}
int praktor_schema_project(const char *idl, size_t idl_size,
    const json_value_t *editor, json_value_t **out, char *error, size_t capacity) {
    projection p = {SALTS_OK, error, capacity};
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    IdlContract *contract = NULL;
    json_value_t *result = NULL, *definitions;
    size_t i;
    if (error && capacity) error[0] = '\0';
    if (!out) return fail(&p, SALTS_EINVAL, "Schema output is required", NULL);
    *out = NULL;
    if (!idl || (!error && capacity) || json_type(editor) != JSON_OBJECT ||
        json_type(json_object_get(editor, "$defs")) != JSON_OBJECT)
        return fail(&p, SALTS_EINVAL, "Workflow IDL and editor definitions are required", NULL);
    if (!idl_contract_parse(idl, idl_size, &contract, &diagnostic)) {
        idl_contract_destroy(contract);
        return fail(&p, diagnostic.status == IDL_NO_MEMORY ? SALTS_ENOMEM : SALTS_EINVAL, "Invalid workflow IDL: ", diagnostic.message);
    }
    if (!contract || !idl_contract_find_data(contract, "Workflow")) { fail(&p, SALTS_EINVAL, "Workflow IDL and editor definitions are required", NULL); goto finished; }
    if (contract->service_count || contract->channel_count || contract->component_count) { fail(&p, SALTS_EINVAL, "Editor projection accepts data contracts only", NULL); goto finished; }
    result = json_clone(editor);
    if (!result) { fail(&p, SALTS_ENOMEM, "Cannot clone editor policy", NULL); goto finished; }
    definitions = json_object_get(result, "$defs");
    for (i = 0; i < contract->data_count && p.status == SALTS_OK; ++i) {
        const IdlDataDecl *decl = &contract->data[i];
        json_value_t *shape, *destination;
        if (decl->kind != IDL_DATA_MESSAGE || decl->annotation_count) { fail(&p, SALTS_EINVAL, "Editor projection requires unannotated IDL messages", NULL); break; }
        shape = message_shape(&p, contract, decl);
        if (!shape) break;
        destination = strcmp(decl->name, "Workflow") == 0 ? result : json_object_get(definitions, decl->name);
        if (json_type(destination) != JSON_OBJECT) fail(&p, SALTS_EINVAL, "IDL message has no editor policy: ", decl->name);
        else merge(&p, destination, shape, 0);
        json_free(shape);
    }
finished:
    idl_contract_destroy(contract);
    if (p.status == SALTS_OK) *out = result;
    else json_free(result);
    return p.status;
}
