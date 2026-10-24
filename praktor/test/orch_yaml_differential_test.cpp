#include "praktor_test.hpp"
#include "../src/yml/task_yaml_internal.hpp"
#include "yml/orch_yaml.h"
#include <exception>
#include <string>

/* Differential guard while the production parser still uses C++ OrchNode.
 * The source document is destroyed before the native tree is compared. */
static bool native_ast_equals_legacy(const OrchNode& legacy,const praktor_orch_node *native) {
    if(!native)return false;
    const auto *spec=praktor_orch_spec_for(praktor_orch_node_type(native));
    if(!spec||legacy.type!=spec->name||
       legacy.params.size()!=praktor_orch_param_count(native)||
       legacy.children.size()!=praktor_orch_child_count(native))return false;
    for(std::size_t i=0;i<praktor_orch_param_count(native);++i) {
        praktor_orch_param parameter{};
        if(praktor_orch_param_at(native,i,&parameter)!=SALTS_OK)return false;
        std::string key(parameter.key.data,parameter.key.len);
        auto found=legacy.params.find(key);
        if(found==legacy.params.end()||
           found->second!=std::string(parameter.value.data,parameter.value.len))return false;
    }
    for(std::size_t i=0;i<praktor_orch_child_count(native);++i) {
        const praktor_orch_node *child=nullptr;
        if(praktor_orch_child_at(native,i,&child)!=SALTS_OK||
           !native_ast_equals_legacy(legacy.children[i],child))return false;
    }
    return true;
}

spec("C11 named orchestration CSTL AST parity with legacy parser") {
    it("agrees on nested controls, decorators and scalar shorthand") {
        const char *cases[]={
            "Sequence:\n  - Shell: {cmd: echo first}\n  - Inverter:\n      child:\n        Shell: {cmd: echo second}\n",
            "Sequence:\n  children:\n    - Sleep: 25\n    - Shell: {cmd: echo, output_key: out}\n",
            "Parallel:\n  - Shell: {cmd: left}\n  - Shell: {cmd: right}\n"
        };
        for(const char *source:cases) {
            TaskYamlDetail::YamlDocument document(source);
            OrchNode legacy=TaskYamlDetail::parse_orch_node(document.root());
            praktor_orch_tree *native=nullptr;
            char diagnostic[160]{};
            const int status=praktor_orch_from_named_yaml(
                document.root().native(),&native,diagnostic,sizeof diagnostic);
            check(status==SALTS_OK);
            check(native!=nullptr);
            if(native) {
                check(native_ast_equals_legacy(legacy,praktor_orch_tree_root(native)));
                praktor_orch_tree_destroy(native);
            }
        }
    }
    it("rejects the same malformed named shapes without publishing partial trees") {
        const char *invalid[]={
            "Sequence: []\n",
            "Shell: {cmd: hi, child: {Sleep: 1}}\n",
            "Shell: {cmd: [hi]}\n"
        };
        for(const char *source:invalid) {
            TaskYamlDetail::YamlDocument document(source);
            bool legacy_rejected=false;
            try { (void)TaskYamlDetail::parse_orch_node(document.root()); }
            catch(const std::exception&) {legacy_rejected=true;}
            praktor_orch_tree *native=nullptr;
            char diagnostic[160]{};
            const int status=praktor_orch_from_named_yaml(
                document.root().native(),&native,diagnostic,sizeof diagnostic);
            check(legacy_rejected);
            check(status!=SALTS_OK);
            check(native==nullptr);
        }
    }

    it("matches legacy flat if, while, switch and set_variable projections") {
        struct FlatCase { const char *kind; const char *source; };
        const FlatCase cases[] = {
            {"if","name: choose\nif: ready\nthen: {Shell: {cmd: run}}\nelse: {Shell: {cmd: skip}}\n"},
            {"if","name: choose\nif: {FileExists: {path: marker}}\nthen: [{Shell: {cmd: run}}, {Sleep: 2}]\n"},
            {"while","name: loop\nwhile: ready\nmax_iterations: 12\ndo: {Shell: {cmd: tick}}\n"},
            {"while","name: loop\nwhile: {FileExists: {path: marker}}\ndo: {Shell: {cmd: tick}}\n"},
            {"switch","name: select\nswitch: mode\ncases:\n  - Shell: {cmd: one}\n  - Shell: {cmd: two}\n"},
            {"set_variable","name: assign\nset_variable: result\nvalue: ready\n"},
            {"set_variable","name: assign\nset_variable: result\nfrom: source\n"}
        };
        for (const auto& item : cases) {
            TaskYamlDetail::YamlDocument document(item.source);
            const OrchParams legacy = parse_orch_params(document.root(), item.kind);
            praktor_orch_tree *native = nullptr;
            char diagnostic[160]{};
            const int status = praktor_orch_from_flat_yaml(
                document.root().native(), item.kind, &native, diagnostic, sizeof diagnostic);
            check(status == SALTS_OK);
            check(native != nullptr);
            if (native) {
                check(native_ast_equals_legacy(legacy.root, praktor_orch_tree_root(native)));
                praktor_orch_tree_destroy(native);
            }
        }
    }

}
