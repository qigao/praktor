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
}
