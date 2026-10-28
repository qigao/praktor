#include "data/workflow_contract_value.h"
#include <string.h>
#include <tinytest.h>

static json_value_t *parse(const char *text) {
    json_value_t *value = json_parse(text, strlen(text));
    check(value != NULL);
    return value;
}

spec("C workflow contract value classification") {
    it("matches the six supported JSON contract types") {
        json_value_t *value = parse("\"text\"");
        check(praktor_contract_value_matches_type(value, "string"));
        check(!praktor_contract_value_matches_type(value, "boolean"));
        json_free(value);

        value = parse("true");
        check(praktor_contract_value_matches_type(value, "boolean"));
        json_free(value);

        value = parse("12.5");
        check(praktor_contract_value_matches_type(value, "number"));
        check(!praktor_contract_value_matches_type(value, "integer"));
        json_free(value);

        value = parse("[1,2]");
        check(praktor_contract_value_matches_type(value, "array"));
        json_free(value);

        value = parse("{\"field\":1}");
        check(praktor_contract_value_matches_type(value, "object"));
        json_free(value);

        value = parse("null");
        check(!praktor_contract_value_matches_type(value, "string"));
        check(!praktor_contract_value_matches_type(value, "boolean"));
        check(!praktor_contract_value_matches_type(value, "number"));
        check(!praktor_contract_value_matches_type(value, "array"));
        check(!praktor_contract_value_matches_type(value, "object"));
        check(!praktor_contract_value_matches_type(value, "integer"));
        json_free(value);
    }

    it("preserves exact integer token classification and range") {
        json_value_t *value = parse("18446744073709551615");
        check(praktor_contract_value_matches_type(value, "integer"));
        json_free(value);

        value = parse("-9223372036854775808");
        check(praktor_contract_value_matches_type(value, "integer"));
        json_free(value);

        value = parse("1.0");
        check(praktor_contract_value_matches_type(value, "number"));
        check(!praktor_contract_value_matches_type(value, "integer"));
        json_free(value);

        value = parse("1e0");
        check(praktor_contract_value_matches_type(value, "number"));
        check(!praktor_contract_value_matches_type(value, "integer"));
        json_free(value);
    }

    it("rejects missing values and unsupported contract types") {
        json_value_t *value = parse("1");
        check(!praktor_contract_value_matches_type(NULL, "number"));
        check(!praktor_contract_value_matches_type(value, NULL));
        check(!praktor_contract_value_matches_type(value, "int"));
        check(!praktor_contract_value_matches_type(value, ""));
        json_free(value);
    }

    it("matches enum members using legacy serialized JSON equality") {
        json_value_t *value = parse("1");
        json_value_t *enum_values = parse("[1,2,3]");
        bool matches = false;
        check(praktor_contract_value_in_enum(value, enum_values, &matches) == SALTS_OK && matches);
        json_free(value);
        value = parse("4");
        matches = true;
        check(praktor_contract_value_in_enum(value, enum_values, &matches) == SALTS_OK && !matches);
        json_free(value);
        json_free(enum_values);
        value = parse("1.0");
        enum_values = parse("[1]");
        matches = true;
        check(praktor_contract_value_in_enum(value, enum_values, &matches) == SALTS_OK && !matches);
        json_free(value);
        json_free(enum_values);
        value = parse("null");
        matches = false;
        check(praktor_contract_value_in_enum(value, NULL, &matches) == SALTS_OK && matches);
        enum_values = parse("[]");
        matches = false;
        check(praktor_contract_value_in_enum(value, enum_values, &matches) == SALTS_OK && matches);
        json_free(value);
        json_free(enum_values);
    }

    it("builds a detached schema fragment and omits absent optional fields") {
        json_value_t *default_value = parse("0");
        json_value_t *enum_values = parse("[1,2]");
        json_value_t *schema = NULL;
        char *encoded;
        check(praktor_contract_schema_field("integer", "retry count", true,
            default_value, enum_values, &schema) == SALTS_OK);
        encoded = json_serialize(schema, NULL);
        check(encoded != NULL);
        check(strcmp(encoded,
            "{\"type\":\"integer\",\"description\":\"retry count\",\"default\":0,\"enum\":[1,2]}") == 0);
        json_serialize_free(encoded);
        json_free(schema);
        schema = NULL;
        check(praktor_contract_schema_field("string", "", false, NULL, NULL,
            &schema) == SALTS_OK);
        encoded = json_serialize(schema, NULL);
        check(encoded != NULL && strcmp(encoded, "{\"type\":\"string\"}") == 0);
        json_serialize_free(encoded);
        json_free(schema);
        json_free(default_value);
        json_free(enum_values);
    }

    it("rejects invalid schema inputs and clears failed outputs") {
        json_value_t *schema = (json_value_t *)1;
        json_value_t *not_array = parse("{}");
        check(praktor_contract_schema_field(NULL, NULL, false, NULL, NULL,
            &schema) == SALTS_EINVAL && schema == NULL);
        schema = (json_value_t *)1;
        check(praktor_contract_schema_field("string", NULL, true, NULL, NULL,
            &schema) == SALTS_EINVAL && schema == NULL);
        schema = (json_value_t *)1;
        check(praktor_contract_schema_field("string", NULL, false, NULL,
            not_array, &schema) == SALTS_EINVAL && schema == NULL);
        check(praktor_contract_value_in_enum(NULL, NULL, NULL) == SALTS_EINVAL);
        json_free(not_array);
    }

}
