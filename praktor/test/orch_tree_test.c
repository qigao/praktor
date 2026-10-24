#include "yml/orch_tree.h"
#include <fmt.h>
#include <limits.h>
#include <salts/error_codes.h>
#include <salts/thread.h>
#include <string.h>
#include <tinytest.h>
#include <tstr.h>

static vstr view(const char *text) { return vstr_from_cstr(text); }
static bool is_text(vstr text, const char *expected) { return vstr_eq(text, view(expected)) != 0; }
static praktor_orch_tree *build(const praktor_orch_command *input) {
    praktor_orch_tree *tree = NULL;
    char error[256] = {0};
    int status = praktor_orch_from_command(input, &tree, error, sizeof error);
    info("status %d: %s", status, error);
    check(status == SALTS_OK && tree && !error[0]); return tree;
}
static const praktor_orch_node *child(const praktor_orch_node *node, size_t index, praktor_orch_type type) {
    const praktor_orch_node *out = NULL;
    check(praktor_orch_child_at(node, index, &out) == SALTS_OK && praktor_orch_node_type(out) == type);
    return out;
}
static void param_is(const praktor_orch_node *node, size_t index, const char *key, const char *value) {
    praktor_orch_param param = {0};
    check(praktor_orch_param_at(node, index, &param) == SALTS_OK && is_text(param.key, key) && is_text(param.value, value));
}
static void refuses(praktor_orch_command input, const char *message) {
    praktor_orch_command saved = {0};
    praktor_orch_tree *previous, *out;
    char error[256] = {0};
    saved.command = view("saved"); previous = build(&saved); out = previous;
    check(praktor_orch_from_command(&input, &out, error, sizeof error) == SALTS_EINVAL && out == previous);
    check(strstr(error, message));
    param_is(child(praktor_orch_tree_root(previous), 0, PRAKTOR_ORCH_SHELL), 0, "cmd", "saved");
    praktor_orch_tree_destroy(previous);
}
static void independent(void *argument) {
    bool *passed = (bool *)argument;
    unsigned index;
    *passed = true;
    for (index = 0; index < 50 && *passed; ++index) {
        praktor_orch_command input = {0};
        praktor_orch_tree *tree = NULL;
        const praktor_orch_node *node = NULL;
        praktor_orch_param param = {0};
        input.command = view("echo"); input.regex.present = true; input.regex.pattern = view("x"); input.regex.capture_group = -7; input.regex.output_key = view("match");
        *passed = praktor_orch_from_command(&input, &tree, NULL, 0) == SALTS_OK &&
            praktor_orch_child_at(praktor_orch_tree_root(tree), 1, &node) == SALTS_OK &&
            praktor_orch_param_at(node, 2, &param) == SALTS_OK && is_text(param.value, "-7");
        praktor_orch_tree_destroy(tree);
    }
}
spec("Native orchestration catalog and command lowering") {
    it("resolves every immutable catalog entry and underscore/case variants") {
        size_t index;
        for (index = 0; index < PRAKTOR_ORCH_TYPE_COUNT; ++index) {
            const praktor_orch_spec *specification = praktor_orch_spec_for((praktor_orch_type)index);
            praktor_orch_type type = PRAKTOR_ORCH_TYPE_COUNT;
            check(specification && praktor_orch_find_type(view(specification->name), &type) == SALTS_OK && type == (praktor_orch_type)index);
        }
        {
            praktor_orch_type type = PRAKTOR_ORCH_SHELL;
            check(praktor_orch_find_type(view("__kEeP_RuNnInG__uNtIl_FaIlUrE_"), &type) == SALTS_OK && type == PRAKTOR_ORCH_KEEP_RUNNING_UNTIL_FAILURE);
            check(praktor_orch_find_type(view("unknown"), &type) == SALTS_ENOENT && type == PRAKTOR_ORCH_KEEP_RUNNING_UNTIL_FAILURE);
            check(praktor_orch_find_type(vstr_from_buf("Shell\0suffix", 12), &type) == SALTS_ENOENT);
            check(praktor_orch_find_type((vstr){NULL, 1}, &type) == SALTS_EINVAL && type == PRAKTOR_ORCH_KEEP_RUNNING_UNTIL_FAILURE);
            check(!praktor_orch_spec_for(PRAKTOR_ORCH_TYPE_COUNT) && !praktor_orch_spec_for((praktor_orch_type)-1));
            check(!praktor_orch_spec_for(PRAKTOR_ORCH_SUB_TREE)->control && !praktor_orch_spec_for(PRAKTOR_ORCH_SUB_TREE)->allows_children);
            check(strcmp(praktor_orch_spec_for(PRAKTOR_ORCH_SHELL)->optional[5], "stream_output") == 0);
        }
    }
    it("owns scalar and binary command text beyond input destruction and pointer transfer") {
        praktor_orch_command input = {0};
        tstr source = tstr_from_v(vstr_from_buf("echo\0tail", 9));
        praktor_orch_tree *tree, *moved;
        praktor_orch_param param = {0};
        const praktor_orch_node *shell;
        check(source); input.command = tstr_to_v(source); tree = build(&input); moved = tree; tree = NULL; tstr_free(source);
        check(praktor_orch_tree_size(moved) == 2 && praktor_orch_node_type(praktor_orch_tree_root(moved)) == PRAKTOR_ORCH_SEQUENCE);
        shell = child(praktor_orch_tree_root(moved), 0, PRAKTOR_ORCH_SHELL);
        check(praktor_orch_param_at(shell, 0, &param) == SALTS_OK && param.value.len == 9 && memcmp(param.value.data, "echo\0tail", 9) == 0);
        param_is(shell, 1, "output_key", "stdout"); praktor_orch_tree_destroy(moved); praktor_orch_tree_destroy(tree);
    }
    it("preserves command order and assigns stdout only to the last Shell") {
        praktor_orch_command input = {0};
        vstr commands[] = {view("one"), view(""), view("three")};
        praktor_orch_tree *tree;
        const praktor_orch_node *root;
        input.sequence = true; input.commands = commands; input.command_count = 3; tree = build(&input); root = praktor_orch_tree_root(tree);
        check(praktor_orch_child_count(root) == 3 && praktor_orch_tree_size(tree) == 4);
        param_is(child(root, 0, PRAKTOR_ORCH_SHELL), 0, "cmd", "one");
        check(praktor_orch_param_count(child(root, 0, PRAKTOR_ORCH_SHELL)) == 1);
        param_is(child(root, 1, PRAKTOR_ORCH_SHELL), 0, "cmd", "");
        param_is(child(root, 2, PRAKTOR_ORCH_SHELL), 1, "output_key", "stdout"); praktor_orch_tree_destroy(tree);
    }
    it("retains empty scalar and empty list manual-input behavior") {
        praktor_orch_command input = {0};
        praktor_orch_tree *tree = build(&input);
        param_is(child(praktor_orch_tree_root(tree), 0, PRAKTOR_ORCH_SHELL), 0, "cmd", ""); praktor_orch_tree_destroy(tree);
        input.sequence = true; tree = build(&input); check(praktor_orch_tree_size(tree) == 1 && !praktor_orch_child_count(praktor_orch_tree_root(tree))); praktor_orch_tree_destroy(tree);
        input.output_format = PRAKTOR_YAML_OUTPUT_JSON; tree = build(&input);
        check(praktor_orch_child_count(praktor_orch_tree_root(tree)) == 1);
        param_is(child(praktor_orch_tree_root(tree), 0, PRAKTOR_ORCH_PARSE_JSON), 1, "path", "$"); praktor_orch_tree_destroy(tree);
    }
    it("retains regex configuration integer range and original parameter order") {
        praktor_orch_command input = {0};
        praktor_orch_tree *tree;
        const praktor_orch_node *node;
        input.command = view("echo"); input.regex.present = true; input.regex.pattern = view("(x)"); input.regex.output_key = view(""); input.regex.capture_group = INT_MIN;
        tree = build(&input); node = child(praktor_orch_tree_root(tree), 1, PRAKTOR_ORCH_PARSE_REGEX);
        param_is(node, 0, "input_key", "stdout"); param_is(node, 1, "pattern", "(x)"); param_is(node, 2, "capture_group", "-2147483648"); param_is(node, 3, "output_key", ""); praktor_orch_tree_destroy(tree);
    }
    it("chooses the original postprocessor priority and JSON format fallback") {
        praktor_orch_command input = {0};
        praktor_orch_tree *tree;
        input.regex.present = input.json.present = input.lines.present = input.keyvalue.present = true; input.output_format = PRAKTOR_YAML_OUTPUT_JSON;
        input.json.path = (vstr){NULL, 1}; /* Inactive lower-priority data is not read. */
        tree = build(&input); child(praktor_orch_tree_root(tree), 1, PRAKTOR_ORCH_PARSE_REGEX); praktor_orch_tree_destroy(tree);
        input.json.path = view(""); input.regex.present = false; tree = build(&input); child(praktor_orch_tree_root(tree), 1, PRAKTOR_ORCH_PARSE_JSON); praktor_orch_tree_destroy(tree);
        input.json.present = false; tree = build(&input); child(praktor_orch_tree_root(tree), 1, PRAKTOR_ORCH_PARSE_LINES); praktor_orch_tree_destroy(tree);
        input.lines.present = false; tree = build(&input); child(praktor_orch_tree_root(tree), 1, PRAKTOR_ORCH_PARSE_KEY_VALUE); praktor_orch_tree_destroy(tree);
        input.keyvalue.present = false; tree = build(&input); param_is(child(praktor_orch_tree_root(tree), 1, PRAKTOR_ORCH_PARSE_JSON), 2, "output_key", "data"); praktor_orch_tree_destroy(tree);
    }
    it("omits only an empty Lines filter and preserves explicit empty KV fields") {
        praktor_orch_command input = {0};
        praktor_orch_tree *tree;
        const praktor_orch_node *node;
        input.lines.present = true; input.lines.output_key = view("lines"); tree = build(&input); node = child(praktor_orch_tree_root(tree), 1, PRAKTOR_ORCH_PARSE_LINES);
        check(praktor_orch_param_count(node) == 2); param_is(node, 1, "output_key", "lines"); praktor_orch_tree_destroy(tree);
        input.lines.filter = view("x"); tree = build(&input); param_is(child(praktor_orch_tree_root(tree), 1, PRAKTOR_ORCH_PARSE_LINES), 1, "filter", "x"); praktor_orch_tree_destroy(tree);
        input.lines.present = false; input.keyvalue.present = true; tree = build(&input); node = child(praktor_orch_tree_root(tree), 1, PRAKTOR_ORCH_PARSE_KEY_VALUE);
        param_is(node, 1, "delimiter", ""); param_is(node, 2, "line_separator", ""); param_is(node, 3, "output_key", ""); praktor_orch_tree_destroy(tree);
    }
    it("admits exact command and node boundaries then refuses excess without output mutation") {
        praktor_orch_command input = {0};
        vstr commands[PRAKTOR_ORCH_MAX_COMMANDS];
        praktor_orch_tree *tree;
        size_t index;
        for (index = 0; index < PRAKTOR_ORCH_MAX_COMMANDS; ++index) commands[index] = view("x");
        input.sequence = true; input.commands = commands; input.command_count = PRAKTOR_ORCH_MAX_COMMANDS; input.output_format = PRAKTOR_YAML_OUTPUT_JSON;
        tree = build(&input); check(praktor_orch_tree_size(tree) == PRAKTOR_ORCH_MAX_NODES);
        check(praktor_orch_child_count(praktor_orch_tree_root(tree)) == PRAKTOR_ORCH_MAX_COMMANDS + 1u); praktor_orch_tree_destroy(tree);
        ++input.command_count; refuses(input, "maximum of 4096");
    }
    it("bounds all retained text including generated and default parameters") {
        praktor_orch_command input = {0};
        tstr text = tstr_new_len(NULL, PRAKTOR_ORCH_MAX_BYTES - 8u);
        praktor_orch_tree *tree;
        check(text); memset(text, 'x', tstr_len(text)); input.command = tstr_to_v(text);
        tree = build(&input); praktor_orch_tree_destroy(tree); tstr_free(text);
        text = tstr_new_len(NULL, PRAKTOR_ORCH_MAX_BYTES - 7u); check(text); memset(text, 'x', tstr_len(text)); input.command = tstr_to_v(text);
        refuses(input, "retained text"); tstr_free(text);
        text = tstr_new_len(NULL, PRAKTOR_ORCH_MAX_BYTES / 2u); check(text); memset(text, 'x', tstr_len(text));
        input.command = tstr_to_v(text); input.regex.present = true; input.regex.pattern = tstr_to_v(text);
        refuses(input, "retained text"); tstr_free(text);
        input.command = view("x"); input.regex.present = true; input.regex.pattern = (vstr){NULL, 1}; refuses(input, "parameter view");
        input.regex.present = false; input.lines.present = true; input.lines.filter = (vstr){NULL, 1}; refuses(input, "parameter view");
    }
    it("keeps diagnostic and accessor outputs bounded and unchanged on refusal") {
        praktor_orch_command input = {0};
        praktor_orch_tree *tree = build(&input), *out = tree;
        const praktor_orch_node *root = praktor_orch_tree_root(tree), *node = root;
        praktor_orch_param param = {view("saved"), view("value")};
        char error[1] = {'x'};
        check(praktor_orch_from_command(NULL, &out, error, sizeof error) == SALTS_EINVAL && out == tree && !error[0]);
        check(praktor_orch_from_command(&input, &out, NULL, 1) == SALTS_EINVAL && out == tree);
        check(praktor_orch_child_at(root, 1, &node) == SALTS_ENOENT && node == root);
        check(praktor_orch_param_at(root, 0, &param) == SALTS_ENOENT && is_text(param.key, "saved") && is_text(param.value, "value"));
        check(praktor_orch_child_at(NULL, 0, &node) == SALTS_EINVAL && node == root);
        check(!praktor_orch_tree_root(NULL) && !praktor_orch_tree_size(NULL) && praktor_orch_node_type(NULL) == PRAKTOR_ORCH_TYPE_COUNT);
        praktor_orch_tree_destroy(tree);
    }
    it("constructs and destroys independent owners concurrently") {
        cmeta_thread_t threads[4] = {0}; bool passed[4] = {false}; size_t index;
        for (index = 0; index < 4; ++index) check(cmeta_thread_create(&threads[index], independent, &passed[index]) == SALTS_OK);
        for (index = 0; index < 4; ++index) check(cmeta_thread_join(&threads[index]) == SALTS_OK && passed[index]);
    }
}


spec("Flat C11 orchestration AST using Salts CSTL") {
    it("retains ordered nested children and owns canonical param text") {
        tstr text=tstr_dup("echo hi");
        praktor_orch_input_param shell[]={{view("CMD"),tstr_to_v(text)}};
        praktor_orch_input_param sleep[]={{view("duration"),view("25")}};
        praktor_orch_flat_node desc[]={
            {PRAKTOR_ORCH_SEQUENCE,PRAKTOR_ORCH_ROOT_PARENT,NULL,0},
            {PRAKTOR_ORCH_INVERTER,0,NULL,0},
            {PRAKTOR_ORCH_SHELL,1,shell,1},
            {PRAKTOR_ORCH_SEQUENCE,0,NULL,0},
            {PRAKTOR_ORCH_SLEEP,3,sleep,1},
            {PRAKTOR_ORCH_SHELL,3,shell,1}
        };
        praktor_orch_tree *tree=NULL;
        const praktor_orch_node *root,*nested;
        char error[128]={0};
        check(text!=NULL);
        check(praktor_orch_from_flat_nodes(desc,6,&tree,error,sizeof error)==SALTS_OK);
        check(tree&&praktor_orch_tree_size(tree)==6&&!error[0]);
        tstr_free(text);
        root=praktor_orch_tree_root(tree);
        check(praktor_orch_child_count(root)==2);
        param_is(child(child(root,0,PRAKTOR_ORCH_INVERTER),0,PRAKTOR_ORCH_SHELL),0,"cmd","echo hi");
        nested=child(root,1,PRAKTOR_ORCH_SEQUENCE);
        param_is(child(nested,0,PRAKTOR_ORCH_SLEEP),0,"duration","25");
        param_is(child(nested,1,PRAKTOR_ORCH_SHELL),0,"cmd","echo hi");
        praktor_orch_tree_destroy(tree);
    }
    it("rejects invalid indices, missing required fields and child cardinality") {
        praktor_orch_tree *result=NULL;
        char error[128]={0};
        praktor_orch_flat_node bad_parent[]={
            {PRAKTOR_ORCH_SEQUENCE,PRAKTOR_ORCH_ROOT_PARENT,NULL,0},
            {PRAKTOR_ORCH_SHELL,1,NULL,0}
        };
        praktor_orch_flat_node missing[]={{PRAKTOR_ORCH_SHELL,PRAKTOR_ORCH_ROOT_PARENT,NULL,0}};
        praktor_orch_flat_node decorator[]={{PRAKTOR_ORCH_INVERTER,PRAKTOR_ORCH_ROOT_PARENT,NULL,0}};
        check(praktor_orch_from_flat_nodes(bad_parent,2,&result,error,sizeof error)==SALTS_EINVAL&&!result);
        check(strstr(error,"parent")!=NULL);
        check(praktor_orch_from_flat_nodes(missing,1,&result,error,sizeof error)==SALTS_EINVAL&&!result);
        check(strstr(error,"required")!=NULL);
        check(praktor_orch_from_flat_nodes(decorator,1,&result,error,sizeof error)==SALTS_EINVAL&&!result);
        check(strstr(error,"child count")!=NULL);
    }
    it("rejects leaf parenting and duplicate or unknown params atomically") {
        praktor_orch_input_param cmd[]={{view("cmd"),view("echo")}};
        praktor_orch_input_param duplicate[]={
            {view("output_key"),view("first")},{view("outputKey"),view("second")},{view("cmd"),view("echo")}
        };
        praktor_orch_input_param wrong[]={{view("unknown"),view("x")}};
        praktor_orch_flat_node leaf_parent[]={{PRAKTOR_ORCH_SHELL,PRAKTOR_ORCH_ROOT_PARENT,cmd,1},
            {PRAKTOR_ORCH_SHELL,0,cmd,1}};
        praktor_orch_flat_node dup[]={{PRAKTOR_ORCH_SHELL,PRAKTOR_ORCH_ROOT_PARENT,duplicate,3}};
        praktor_orch_flat_node unk[]={{PRAKTOR_ORCH_SHELL,PRAKTOR_ORCH_ROOT_PARENT,wrong,1}};
        praktor_orch_tree *tree=NULL;
        char error[128]={0};
        check(praktor_orch_from_flat_nodes(leaf_parent,2,&tree,error,sizeof error)==SALTS_EINVAL&&!tree);
        check(strstr(error,"Leaf")!=NULL);
        check(praktor_orch_from_flat_nodes(dup,1,&tree,error,sizeof error)==SALTS_EINVAL&&!tree);
        check(strstr(error,"Duplicate")!=NULL);
        check(praktor_orch_from_flat_nodes(unk,1,&tree,error,sizeof error)==SALTS_EINVAL&&!tree);
        check(strstr(error,"Unknown")!=NULL);
    }
    it("owns binary values and preserves existing output on invalid input") {
        praktor_orch_input_param params[]={{view("cmd"),vstr_from_buf("a\0b",3)}};
        praktor_orch_flat_node desc[]={{PRAKTOR_ORCH_SHELL,PRAKTOR_ORCH_ROOT_PARENT,params,1}};
        praktor_orch_tree *tree=NULL,*out;
        praktor_orch_param param={0};
        char error[8]={'x'};
        check(praktor_orch_from_flat_nodes(desc,1,&tree,error,sizeof error)==SALTS_OK&&tree&&!error[0]);
        check(praktor_orch_param_at(praktor_orch_tree_root(tree),0,&param)==SALTS_OK);
        check(param.value.len==3&&memcmp(param.value.data,"a\0b",3)==0);
        out=tree;
        check(praktor_orch_from_flat_nodes(NULL,1,&out,error,sizeof error)==SALTS_EINVAL&&out==tree);
        check(praktor_orch_from_flat_nodes(desc,0,&out,error,sizeof error)==SALTS_EINVAL&&out==tree);
        check(praktor_orch_from_flat_nodes(desc,1,&out,NULL,1)==SALTS_EINVAL&&out==tree);
        praktor_orch_tree_destroy(tree);
    }
}
