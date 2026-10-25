#include "praktor_test.hpp"
#include "../src/yml/task_yaml_internal.hpp"
#include "yml/orch_yaml.h"
#include "yml/yaml_task_metadata.h"
#include "yml/yaml_workflow_metadata.h"
#include "yml/task_parser.hpp"
#include <algorithm>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>
#include <utility>
#include <stdexcept>
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
            "Parallel:\n  - Shell: {cmd: left}\n  - Shell: {cmd: right}\n",
            "Sequence:\n  child:\n    - Shell: {CMD: first, c_m_d: second}\n    - Sleep: 2\n",
            "Sequence:\n  children:\n    - Shell: {cmd: first}\n    - FileExists: {path: marker, fail_if_missing: TRUE}\n"
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
            "Shell: {cmd: [hi]}\n",
            "IfThenElse: {condition: ready, child: {Shell: {cmd: hi}}}\n",
            "WhileDo: {condition: ready, child: {Shell: {cmd: hi}}}\n",
            "Switch: {variable: mode, children: [{Shell: {cmd: hi}}]}\n",
            "SetVariable: {key: x, value: y}\n",
            "Timeout: {timeout_ms: 1, child: {Shell: {cmd: hi}}}\n",
            "Repeat: {num_cycles: 2, child: {Shell: {cmd: hi}}}\n",
            "Delay: {delay_ms: 1, child: {Shell: {cmd: hi}}}\n"
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
            {"set_variable","name: assign\nset_variable: result\nfrom: source\n"},
            {"timeout","name: wait\ntimeout: 25\nchild: {Shell: {cmd: run}}\n"},
            {"repeat","name: loop\nrepeat: 2\nchild: [{Shell: {cmd: one}}, {Sleep: 5}]\n"},
            {"delay","name: delayed\ndelay: 15\nchild: {Shell: {cmd: run}}\n"},
            {"keep_running_until_failure","name: loop\nkeep_running_until_failure: 7\nchild: {Shell: {cmd: run}}\n"},
            {"precondition","name: guarded\nprecondition: '{ready}'\nchild: {Shell: {cmd: run}}\n"},
            {"entry_updated","name: watch\nentry_updated: state\nchild: {Shell: {cmd: run}}\n"},
            {"consume_queue","name: consume\nconsume_queue: events\nchild: {Shell: {cmd: run}}\n"},
            {"run_once","name: once\nrun_once: ignored\nchild: {Shell: {cmd: run}}\n"}
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
    it("matches task-level named controls, decorators and leaves") {
        /* The legacy runner selector parses the selected YAML value, not
         * the metadata-bearing task map. The native parser must agree. */
        struct TaskCase { const char* type; const char* source; };
        const TaskCase cases[]={
            {"sequence","name: serial\nsequence:\n  - Shell: {cmd: one}\n  - Sleep: 1\n"},
            {"parallel","name: fork\nparallel:\n  - Shell: {cmd: a}\n  - Shell: {cmd: b}\n"},
            {"reactive_sequence","name: reactive\nreactive_sequence:\n  - FileExists: {path: ok}\n  - Shell: {cmd: go}\n"},
            {"pipeline_sequence","name: pipe\npipeline_sequence:\n  - Shell: {cmd: one}\n"},
            {"fallback","name: fallback\nfallback:\n  - Shell: {cmd: one}\n"},
            {"selector","name: choose\nselector:\n  - Shell: {cmd: one}\n"},
            {"shell","name: command\nshell: {cmd: echo, outputKey: stdout}\n"},
            {"parse_json","name: json\nparse_json: {input_key: stdout, path: '$'}\n"},
            {"parse_regex","name: regex\nparse_regex: {input_key: stdout, pattern: '(x)', capture_group: 1}\n"},
            {"parse_lines","name: lines\nparse_lines: {input_key: stdout, filter: '[a-z]'}\n"},
            {"parse_keyvalue","name: kv\nparse_keyvalue: {input_key: stdout, delimiter: '='}\n"},
            {"check_exit_code","name: status\ncheck_exit_code: {expected: 0}\n"},
            {"wait_event","name: event\nwait_event: {event: done}\n"},
            {"file_exists","name: file\nfile_exists: {path: ready, fail_if_missing: false}\n"},
            {"sleep","name: snooze\nsleep: 4\n"},
            {"subtree","name: subtree\nsubtree: {tree: worker}\n"},
            {"inverter","name: invert\ninverter: {child: {Shell: {cmd: run}}}\n"},
            {"force_success","name: success\nforce_success: {child: {Shell: {cmd: run}}}\n"},
            {"force_failure","name: failure\nforce_failure: {child: {Shell: {cmd: run}}}\n"},
            {"run_once","name: once\nrun_once: {child: {Shell: {cmd: run}}}\n"}
        };
        for(const auto& item:cases) {
            TaskYamlDetail::YamlDocument document(item.source);
            const OrchParams legacy=parse_orch_params(document.root(),item.type);
            praktor_orch_tree *native=nullptr;
            char diagnostic[256]{};
            const int status=praktor_orch_from_flat_yaml(
                document.root().native(),item.type,&native,diagnostic,sizeof diagnostic);
            check(status==SALTS_OK);
            check(native!=nullptr);
            if(native) {
                check(native_ast_equals_legacy(legacy.root,praktor_orch_tree_root(native)));
                praktor_orch_tree_destroy(native);
            }
        }
    }
    it("rejects malformed task-level named runners without partial output") {
        struct TaskCase { const char* type; const char* source; };
        const TaskCase cases[]={
            {"shell","name: bad\nshell: {cmd: hello, illegal: yes}\n"},
            {"shell","name: bad\nshell: {cmd: [bad]}\n"},
            {"inverter","name: bad\ninverter: {children: []}\n"},
            {"sequence","name: bad\nsequence: []\n"},
            {"parse_regex","name: bad\nparse_regex: {input_key: stdout, pattern: x, capture_group: two}\n"},
            {"wait_event","name: bad\nwait_event: {event: ok, timeout: wrong}\n"}
        };
        for(const auto& item:cases) {
            TaskYamlDetail::YamlDocument document(item.source);
            bool legacy_rejected=false;
            try { (void)parse_orch_params(document.root(),item.type); }
            catch(const std::exception&){legacy_rejected=true;}
            praktor_orch_tree *native=nullptr;
            char diagnostic[256]{};
            const int status=praktor_orch_from_flat_yaml(
                document.root().native(),item.type,&native,diagnostic,sizeof diagnostic);
            check(legacy_rejected);
            check(status!=SALTS_OK);
            check(native==nullptr);
            check(diagnostic[0]!='\0');
        }
    }
    it("compares the checked-in workflow examples against the legacy parser") {
        const char *files[]={
            "examples/orch_build_pipeline.yml",
            "examples/orch_parallel_testing.yml",
            "examples/simplified_parser_syntax.yml",
            "examples/orch_shortcuts_demo.yml",
            "examples/orch_version_extraction.yml",
            "examples/orch_env_vars_example.yml",
            "examples/orch_mustache_example.yml",
            "sdk/examples/ops-event-handler-example.yml"
        };
        const char *runners[]={
            "sequence","parallel","reactive_sequence","pipeline_sequence",
            "fallback","selector","if","while","switch","set_variable",
            "shell","parse_json","parse_regex","parse_lines","parse_keyvalue",
            "check_exit_code","wait_event","file_exists","sleep","subtree",
            "inverter","force_success","force_failure","timeout","repeat",
            "delay","keep_running_until_failure","precondition","entry_updated",
            "consume_queue","run_once"
        };
        std::size_t compared=0;
        for(const char *name:files) {
            const std::string path=std::string(PRAKTOR_ORCH_CORPUS_ROOT)+"/"+name;
            std::ifstream stream(path,std::ios::binary);
            if(!stream)throw std::runtime_error("Cannot read orchestration corpus: "+path);
            const std::string yaml{
                std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
            TaskYamlDetail::YamlDocument document(yaml);
            const auto tasks=document.root()["tasks"];
            for(const auto& task:tasks) {
                /* command is handled by its own native lowering path. */
                if(task.has_child("command"))continue;
                for(const char *type:runners) {
                    if(!task.has_child(type))continue;
                    const OrchParams legacy=parse_orch_params(task,type);
                    praktor_orch_tree *native=nullptr;
                    char diagnostic[256]{};
                    const int status=praktor_orch_from_flat_yaml(
                        task.native(),type,&native,diagnostic,sizeof diagnostic);
                    check(status==SALTS_OK);
                    check(native!=nullptr);
                    if(native) {
                        check(native_ast_equals_legacy(legacy.root,praktor_orch_tree_root(native)));
                        praktor_orch_tree_destroy(native);
                    }
                    ++compared;
                    break;
                }
            }
        }
        check(compared>=17);
    }

    it("preserves legacy named-map parameter error precedence and locations") {
        const char *source=
            "Sequence:\n"
            "  child:\n"
            "    Shell: {cmd: [bad]}\n"
            "  bogus: wrong\n";
        TaskYamlDetail::YamlDocument document(source);
        std::string legacy_error;
        try { (void)TaskYamlDetail::parse_orch_node(document.root()); }
        catch (const std::exception& e) { legacy_error=e.what(); }
        praktor_orch_tree *native=nullptr;
        char error[256]{};
        const int status=praktor_orch_from_named_yaml(
            document.root().native(),&native,error,sizeof error);
        check(status!=SALTS_OK);
        check(native==nullptr);
        check(legacy_error.find("Unknown orchestration parameter 'bogus'")!=std::string::npos);
        check(std::string(error).find("Unknown orchestration parameter 'bogus'")!=std::string::npos);
        check(legacy_error.find("at line")!=std::string::npos);
        check(std::string(error).find("at line")!=std::string::npos);
    }
    it("preserves legacy named-map child/children conflict precedence") {
        const char *source=
            "Sequence:\n"
            "  child: {Shell: {cmd: first}}\n"
            "  children: [{Shell: {cmd: second}}]\n";
        TaskYamlDetail::YamlDocument document(source);
        std::string legacy_error;
        try { (void)TaskYamlDetail::parse_orch_node(document.root()); }
        catch (const std::exception& e) { legacy_error=e.what(); }
        praktor_orch_tree *native=nullptr;
        char error[256]{};
        check(praktor_orch_from_named_yaml(document.root().native(),
            &native,error,sizeof error)!=SALTS_OK);
        check(native==nullptr);
        check(legacy_error.find("both 'child' and 'children'")!=std::string::npos);
        check(std::string(error).find("both 'child' and 'children'")!=std::string::npos);
    }

    it("matches legacy named shorthand and retired-type errors verbatim") {
        const char *sources[]={
            "Sequence: not_a_sequence\n",
            "Parallel: ignored\n",
            "RunOnce: only_once\n",
            "Inverter: ignored\n",
            "ParseRegex: one_parameter\n",
            "IfThenElse: {condition: ready}\n",
            "if_then_else: {condition: ready}\n",
            "WhileDo: {condition: ready}\n",
            "Switch: {variable: mode}\n",
            "SetVariable: {key: key, value: value}\n",
            "Timeout: {timeout_ms: 5}\n",
            "Repeat: {num_cycles: 2}\n",
            "Delay: {delay_ms: 3}\n"
        };
        for(const char *source:sources) {
            TaskYamlDetail::YamlDocument document(source);
            std::string legacy_error;
            try { (void)TaskYamlDetail::parse_orch_node(document.root()); }
            catch(const std::exception& e) {legacy_error=e.what();}
            praktor_orch_tree *native=nullptr;
            char diagnostic[512]{};
            const int status=praktor_orch_from_named_yaml(
                document.root().native(),&native,diagnostic,sizeof diagnostic);
            check(!legacy_error.empty());
            check(status==SALTS_EINVAL);
            check(native==nullptr);
            check(std::string(diagnostic)==legacy_error);
        }
    }
    it("matches invalid named child mappings and sequences") {
        const char *sources[]={
            "Sequence:\n  children: {Shell: {cmd: one}}\n",
            "Parallel:\n  children: {Shell: {cmd: one}}\n",
            "Shell:\n  children: [{Shell: {cmd: one}}]\n",
            "Shell:\n  child: {Sleep: 1}\n",
            "Sequence:\n  - Shell: {cmd: ok}\n  - not_a_node\n",
            "Sequence:\n  children: [42]\n",
            "Sequence:\n  child: [1]\n",
            "Sequence:\n  child: {InvalidNode: {}}\n",
            "Sequence:\n  child: {Shell: {cmd: ok}, Sleep: 1}\n",
            "A: {}\nB: {}\n",
            "- Shell: {cmd: yes}\n- Sleep: 1\n"
        };
        for(const char *source:sources) {
            TaskYamlDetail::YamlDocument document(source);
            std::string legacy_error;
            try { (void)TaskYamlDetail::parse_orch_node(document.root()); }
            catch(const std::exception& e) {legacy_error=e.what();}
            praktor_orch_tree *native=nullptr;
            char diagnostic[512]{};
            const int status=praktor_orch_from_named_yaml(
                document.root().native(),&native,diagnostic,sizeof diagnostic);
            check(!legacy_error.empty());
            check(status==SALTS_EINVAL);
            check(native==nullptr);
            if(std::string(source).find("InvalidNode")!=std::string::npos)
                check(std::string(diagnostic).find("Unknown orchestration node type: 'InvalidNode'")!=std::string::npos);
            else
                check(std::string(diagnostic)==legacy_error);
        }
    }
    it("matches required parameter and named-control child-count diagnostics") {
        const char *sources[]={
            "Shell: {output_key: out}\n",
            "ParseRegex: {input_key: raw}\n",
            "WaitEvent: {timeout: 30}\n",
            "FileExists: {output_key: exists}\n",
            "Sleep: {}\n",
            "Sequence: []\n",
            "Fallback: {children: []}\n",
            "Parallel: {children: []}\n",
            "Inverter: {}\n",
            "RunOnce: {children: []}\n",
            "PipelineSequence: {input_key: in}\n",
            "Sequence:\n  children:\n    - Shell: {output_key: missing}\n    - Shell: {cmd: later}\n"
        };
        for(const char *source:sources) {
            TaskYamlDetail::YamlDocument document(source);
            std::string legacy_error;
            try { (void)TaskYamlDetail::parse_orch_node(document.root()); }
            catch(const std::exception& e) {legacy_error=e.what();}
            praktor_orch_tree *native=nullptr;
            char diagnostic[512]{};
            const int status=praktor_orch_from_named_yaml(
                document.root().native(),&native,diagnostic,sizeof diagnostic);
            check(!legacy_error.empty());
            check(status==SALTS_EINVAL);
            check(native==nullptr);
            check(std::string(diagnostic)==legacy_error);
        }
    }

    it("matches full legacy locations and canonical names for typed scalar errors") {
        struct ScalarCase { const char *source; const char *key; const char *kind; };
        const ScalarCase cases[] = {
            {"ParseRegex: {input_key: source, pattern: ok, captureGroup: invalid}\n",
                "capture_group","an integer"},
            {"Shell: {cmd: echo, STREAM_OUTPUT: maybe}\n",
                "stream_output","a boolean"},
            {"FileExists: {path: marker, failIfMissing: 123}\n",
                "fail_if_missing","a boolean"},
            {"WaitEvent: {event: ready, timeout: 1.5}\n",
                "timeout","an integer"}
        };
        for(const auto& item:cases) {
            TaskYamlDetail::YamlDocument document(item.source);
            std::string legacy_error;
            try { (void)TaskYamlDetail::parse_orch_node(document.root()); }
            catch(const std::exception& e) { legacy_error=e.what(); }
            praktor_orch_tree *native=nullptr;
            char diagnostic[512]{};
            const int status=praktor_orch_from_named_yaml(
                document.root().native(),&native,diagnostic,sizeof diagnostic);
            check(status!=SALTS_OK);
            check(native==nullptr);
            check(legacy_error.find(item.key)!=std::string::npos);
            check(legacy_error.find(item.kind)!=std::string::npos);
            check(std::string(diagnostic)==legacy_error);
        }
    }

    it("matches exact legacy flat numeric and unknown-key diagnostics") {
        struct Case { const char *kind, *source; };
        const Case cases[]={
            {"while","name: loop\nwhile: ready\nmax_iterations: bad\ndo: {Shell: {cmd: tick}}\n"},
            {"timeout","name: t\ntimeout: 1.5\nchild: {Shell: {cmd: go}}\n"},
            {"repeat","name: r\nrepeat: invalid\nchild: {Shell: {cmd: go}}\n"},
            {"delay","name: d\ndelay: '--'\nchild: {Shell: {cmd: go}}\n"},
            {"keep_running_until_failure","name: k\nkeep_running_until_failure: x\nchild: {Shell: {cmd: go}}\n"},
            {"if","name: choice\nif: ready\nthen: {Shell: {cmd: go}}\nbogus: bad\n"},
            {"set_variable","name: assign\nset_variable: key\nvalue: ok\nunknown: bad\n"}
        };
        for(const auto &item:cases) {
            TaskYamlDetail::YamlDocument document(item.source);
            std::string legacy_error;
            try { (void)parse_orch_params(document.root(),item.kind); }
            catch(const std::exception& e) { legacy_error=e.what(); }
            praktor_orch_tree *native=nullptr;
            char diagnostic[512]{};
            const int status=praktor_orch_from_flat_yaml(
                document.root().native(),item.kind,&native,diagnostic,sizeof diagnostic);
            check(!legacy_error.empty());
            check(status==SALTS_EINVAL);
            check(native==nullptr);
            check(std::string(diagnostic)==legacy_error);
        }
    }

    it("matches legacy flat empty branch, switch and scalar shape errors") {
        struct Case { const char *kind,*source; };
        const Case cases[]={
            {"if","name: test\nif: ready\nthen: []\n"},
            {"while","name: test\nwhile: ready\ndo: []\n"},
            {"switch","name: test\nswitch: mode\ncases: []\n"},
            {"switch","name: test\nswitch: mode\ncases: {}\n"},
            {"run_once","name: test\nrun_once: ignored\nchild: []\n"},
            {"timeout","name: test\ntimeout: [invalid]\nchild: {Shell: {cmd: run}}\n"},
            {"while","name: test\nwhile: ready\nmax_iterations: [invalid]\ndo: {Shell: {cmd: run}}\n"},
            {"switch","name: test\nswitch: [bad]\n"},
            {"set_variable","name: test\nset_variable: [bad]\nvalue: okay\nfrom: source\n"},
            {"set_variable","name: test\nset_variable: key\nvalue: [invalid]\n"},
            {"set_variable","name: test\nset_variable: key\nfrom: [invalid]\n"}
        };
        for(const auto& item:cases) {
            TaskYamlDetail::YamlDocument document(item.source);
            std::string legacy_error;
            try { (void)parse_orch_params(document.root(),item.kind); }
            catch(const std::exception& e) { legacy_error=e.what(); }
            praktor_orch_tree *native=nullptr;
            char diagnostic[512]{};
            const int status=praktor_orch_from_flat_yaml(
                document.root().native(),item.kind,&native,diagnostic,sizeof diagnostic);
            check(!legacy_error.empty());
            check(status==SALTS_EINVAL);
            check(native==nullptr);
            check(std::string(diagnostic)==legacy_error);
        }
    }

    it("agrees with legacy on invalid flat forms and never publishes partial ASTs") {
        struct FlatCase { const char *kind; const char *source; };
        const FlatCase cases[] = {
            {"if","name: choice\nif: ready\nthen: []\n"},
            {"while","name: loop\nwhile: ready\ndo: []\n"},
            {"switch","name: switch\nswitch: mode\ncases: []\n"},
            {"set_variable","name: assign\nset_variable: x\nvalue: a\nfrom: b\n"},
            {"timeout","name: guard\ntimeout: 1.5\nchild: {Shell: {cmd: run}}\n"},
            {"repeat","name: retry\nrepeat: invalid\nchild: {Shell: {cmd: run}}\n"},
            {"delay","name: sleep\ndelay: '--'\nchild: {Shell: {cmd: run}}\n"},
            {"run_once","name: once\nrun_once: ok\nchild: []\n"},
            {"precondition","name: guard\nprecondition: [invalid]\nchild: {Shell: {cmd: run}}\n"}
        };
        for (const auto& item : cases) {
            TaskYamlDetail::YamlDocument document(item.source);
            bool legacy_rejected = false;
            try { (void)parse_orch_params(document.root(),item.kind); }
            catch(const std::exception&) { legacy_rejected = true; }
            praktor_orch_tree *native = nullptr;
            char diagnostic[160]{};
            const int status = praktor_orch_from_flat_yaml(
                document.root().native(),item.kind,&native,diagnostic,sizeof diagnostic);
            check(legacy_rejected);
            check(status != SALTS_OK);
            check(native == nullptr);
            check(diagnostic[0] != '\0');
        }
    }

}


/* Full-workflow qualification boundary: C11 produces borrowed-input-free task
 * metadata and orchestration trees; the unmodified TaskParser supplies the
 * production comparison. No workflow executes, and this test does not create
 * a second C++ or C scheduler. */
static std::string workflow_corpus_read(const char *path) {
    const std::string absolute=std::string(PRAKTOR_ORCH_CORPUS_ROOT)+"/"+path;
    std::ifstream stream(absolute,std::ios::binary);
    if(!stream)throw std::runtime_error("Cannot read workflow corpus: "+absolute);
    return std::string{
        std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
static void full_workflow_matches_native_projections(const std::string& yaml,
                                                     const std::string& source) {
    const Workflow legacy=TaskParser::parseText(yaml,source);
    const auto graph=TaskParser::buildGraph(legacy);
    WorkflowValue native_workflow;
    {
        TaskYamlDetail::YamlDocument document(yaml);
        json_value_t *metadata=nullptr;
        praktor_yaml_field_error error{};
        const int status=praktor_yaml_workflow_metadata_create(
            document.root().native(),&metadata,&error);
        if(status!=SALTS_OK) {
            if(error.text)info("Workflow metadata error: %s",error.text);
            check(status==SALTS_OK);
            praktor_yaml_field_error_dispose(&error);
            return;
        }
        praktor_yaml_field_error_dispose(&error);
        native_workflow=WorkflowValue::adoptJson(metadata);
    }
    if(native_workflow.contains("name"))
        check(native_workflow.at("name").as<std::string>()==legacy.name);
    if(native_workflow.contains("strict_inputs"))
        check(native_workflow.at("strict_inputs").as<bool>()==legacy.strict_inputs);

    /* Each native result must remain sound after destroying its YAML DOM. */
    std::size_t compared_asts=0;
    for(std::size_t i=0;i<legacy.tasks.size();++i) {
        WorkflowValue meta;
        std::unique_ptr<praktor_orch_tree,decltype(&praktor_orch_tree_destroy)>
            native(nullptr,&praktor_orch_tree_destroy);
        {
            TaskYamlDetail::YamlDocument document(yaml);
            const auto tasks=document.root()["tasks"];
            check(tasks.is_seq());
            check(tasks.num_children()==legacy.tasks.size());
            if(i>=tasks.num_children())return;
            const auto task=tasks.child_at(i);
            json_value_t *raw=nullptr;
            praktor_yaml_field_error error{};
            const int metadata_status=praktor_yaml_task_metadata_create(
                task.native(),&raw,&error);
            if(metadata_status!=SALTS_OK) {
                if(error.text)info("Task metadata error: %s",error.text);
                check(metadata_status==SALTS_OK);
                praktor_yaml_field_error_dispose(&error);
                return;
            }
            praktor_yaml_field_error_dispose(&error);
            meta=WorkflowValue::adoptJson(raw);
            const std::string kind=meta.contains("runner_kind")
                ?meta.at("runner_kind").as<std::string>():std::string{};
            if(kind=="orch_root"||kind=="actions") {
                praktor_orch_tree *tree=nullptr;
                char diagnostic[512]{};
                const int result=kind=="actions"?
                    praktor_orch_from_named_yaml(task["actions"].native(),
                        &tree,diagnostic,sizeof diagnostic):
                    praktor_orch_from_flat_yaml(task.native(),
                        meta.at("orch_node_type").as<std::string>().c_str(),
                        &tree,diagnostic,sizeof diagnostic);
                if(result!=SALTS_OK)
                    info("Native tree failure: %s, task: %s",
                         diagnostic,legacy.tasks[i].name.c_str());
                check(result==SALTS_OK);
                native.reset(tree);
            }
        }
        const auto& selected=legacy.tasks[i];
        check(meta.at("name").as<std::string>()==selected.name);
        const std::string runner=meta.contains("runner_kind")
            ?meta.at("runner_kind").as<std::string>():std::string{};
        const int candidates=meta.contains("runner_count")
            ?meta.at("runner_count").as<int>():0;
        check(candidates<=1);
        if(!runner.empty()) {
            check(selected.declared_runner==
                  (runner=="orch_root"?"actions":runner));
        }
        const auto dependencies=meta.contains("depends_on")
            ?meta.at("depends_on").array_range():std::vector<WorkflowValue>{};
        check(dependencies.size()==selected.depends_on.size());
        for(std::size_t j=0;j<dependencies.size()&&j<selected.depends_on.size();++j)
            check(dependencies[j].as<std::string>()==selected.depends_on[j]);
        if(meta.contains("env")) {
            check(meta.at("env").size()==selected.env.size());
            for(const auto& entry:meta.at("env").object_range()) {
                const auto found=selected.env.find(entry.key());
                check(found!=selected.env.end());
                if(found!=selected.env.end())
                    check(found->second==entry.value().as<std::string>());
            }
        }
        if(native) {
            check(selected.action==TaskAction::Orch);
            check(std::holds_alternative<OrchParams>(selected.specifics));
            if(std::holds_alternative<OrchParams>(selected.specifics))
                check(native_ast_equals_legacy(
                    std::get<OrchParams>(selected.specifics).root,
                    praktor_orch_tree_root(native.get())));
            ++compared_asts;
        }
        /* Validate the actual production dependency graph against parsed task
         * names, rather than treating a task's depends_on list as graph proof. */
        for(const auto& dep:selected.depends_on) {
            const auto predecessor=std::find_if(
                legacy.tasks.begin(),legacy.tasks.end(),
                [&](const Task& candidate){return candidate.name==dep;});
            check(predecessor!=legacy.tasks.end());
            if(predecessor!=legacy.tasks.end()) {
                const auto outgoing=graph.getEdges(*predecessor);
                check(std::any_of(outgoing.begin(),outgoing.end(),
                    [&](const Task& next){return next.name==selected.name;}));
            }
        }
    }
    check(compared_asts>0);
}
spec("Full workflow versus C11 task metadata and native orchestration") {
    it("compares five repository workflows after YAML document destruction") {
        const char *files[]={
            "examples/orch_build_pipeline.yml",
            "examples/orch_parallel_testing.yml",
            "examples/orch_shortcuts_demo.yml",
            "examples/orch_version_extraction.yml",
            "examples/orch_env_vars_example.yml"
        };
        for(const char *path:files) {
            const std::string yaml=workflow_corpus_read(path);
            full_workflow_matches_native_projections(yaml,path);
        }
    }
    it("matches a mixed command/action/flat-branch workflow") {
        const char *yaml=
            "name: full-parity\n"
            "variables: {MODE: fast}\n"
            "tasks:\n"
            "  - name: prepare\n"
            "    command: echo ready\n"
            "    parse_json: {path: '$.ready', output_key: ready}\n"
            "  - name: act\n"
            "    depends_on: [prepare]\n"
            "    env: {TEST_MODE: '1'}\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - Shell: {cmd: echo started}\n"
            "        - FileExists: {path: marker}\n"
            "  - name: decide\n"
            "    depends_on: [act]\n"
            "    if: ready\n"
            "    then: [{Shell: {cmd: yes}}, {Sleep: 1}]\n"
            "    else: {Shell: {cmd: no}}\n"
            "  - name: deliver\n"
            "    depends_on: [decide, prepare]\n"
            "    parallel:\n"
            "      - Shell: {cmd: alpha}\n"
            "      - Shell: {cmd: beta}\n";
        full_workflow_matches_native_projections(yaml,"mixed-parity-inline.yml");
    }
}
