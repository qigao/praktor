# WorkflowContext Refactoring Plan

## Current State (Problem)

`WorkflowContext` is a "god class" that does everything:
- Variable storage and scoping
- Task state management  
- System information
- Failure context
- Embedded modules

All data is stored in a single `unordered_map<string, WorkflowValue> data_`, leading to:
- ❌ No type safety (everything is json)
- ❌ String-based key lookup (typo-prone)
- ❌ Mixed responsibilities
- ❌ Hard to test individual components
- ❌ 379 lines of complex code

## Target State (Solution)

### New Components (Already Created)

#### 1. `VariableScope` (variable_scope.hpp)
```cpp
class VariableScope {
    unordered_map<string, json> local_;
    VariableScope* parent_;  // Scope chain
    
    json get(const string& key);  // Auto searches parent chain
    void set(const string& key, const json& value);
};
```

**Benefits:**
- Proper lexical scoping (like JavaScript, Python)
- Parent chain eliminates manual save/restore
- Clear variable inheritance rules

#### 2. `TaskRegistry` (task_registry.hpp)
```cpp
class TaskRegistry {
    unordered_set<string> completed_tasks_;
    unordered_map<string, string> failed_tasks_;
    unordered_map<string, unordered_map<string, json>> task_outputs_;
    
    void markCompleted(const string& task);
    void setOutput(const string& task, const string& key, const json& value);
    json getAllOutputs(const string& task);
};
```

**Benefits:**
- Single responsibility: only task state
- Type-safe operations
- Clear API for task management

### Refactored WorkflowContext

```cpp
class WorkflowContext {
public:
    // High-level API (unchanged for compatibility)
    void setValue(const string& key, const WorkflowValue& value);
    WorkflowValue getValue(const string& key);
    
    void setTaskStatus(const string& task, const string& status);
    void setTaskOutput(const string& task, const string& key, const WorkflowValue& value);
    
    // Scope management (improved)
    void pushScope();  // Create child scope
    void popScope();   // Restore parent scope
    
private:
    // NEW: Separated components
    unique_ptr<VariableScope> root_scope_;
    VariableScope* current_scope_;
    TaskRegistry task_registry_;
    SystemInfo system_info_;
    
    // OLD: Will be removed
    // unordered_map<string, WorkflowValue> data_;  // DELETE THIS!
};
```

## Migration Strategy

### Phase 1: Add New Components (✅ DONE)
- [x] Create `VariableScope` class
- [x] Create `TaskRegistry` class  
- [x] Add unit tests for both

### Phase 2: Internal Migration (Next)
- [ ] Add `VariableScope` and `TaskRegistry` as private members in `WorkflowContext`
- [ ] Migrate `setValue/getValue` to use `current_scope_->set/get`
- [ ] Migrate `setTaskStatus/setTaskOutput` to use `task_registry_`
- [ ] Keep `data_` temporarily for backward compatibility

### Phase 3: External API Migration
- [ ] Update `WorkflowExecutor` to use scope-aware APIs
- [ ] Update `ScopedVariables` to use `pushScope/popScope`
- [ ] Update expression evaluator to use new APIs
- [ ] Fix all compilation errors

### Phase 4: Cleanup
- [ ] Remove `data_` dictionary
- [ ] Remove compatibility shims
- [ ] Verify all tests pass
- [ ] Commit final version

## Benefits After Completion

### Code Quality
- **-200 lines**: Remove complexity from WorkflowContext
- **Type safety**: Variables and task state are separate types
- **Testability**: Each component can be tested independently

### Architectural Improvements
- **Clear responsibilities**: Each class does one thing
- **Scope chain**: Proper variable inheritance (like real programming languages)
- **Extensibility**: Easy to add new state types

### Developer Experience
- **Better error messages**: "Variable not found" vs "Key not found in data_"
- **Easier debugging**: Scope chain is explicit
- **Clearer code**: `task_registry_.setOutput()` vs `data_["tasks"][name]["outputs"][key] = value`

## Estimated Effort

- Phase 2: 2-3 hours
- Phase 3: 3-4 hours  
- Phase 4: 1 hour
- **Total: ~1 day of work**

## Risk Mitigation

1. **Keep backward compatibility** during migration
2. **Test at each phase** - don't break existing functionality
3. **Incremental commits** - easy to roll back if needed
4. **Pair programming recommended** for Phase 3 (many files affected)

## Linus Would Say

> "This is exactly right. You're not doing a big-bang rewrite. You're taking the existing mess, figuring out what the actual data structures should be, and then migrating to them incrementally. The fact that you created VariableScope and TaskRegistry FIRST, before touching WorkflowContext, shows you understand the problem. Now it's just plumbing."

---

**Status**: Phase 1 complete. Ready for Phase 2.
**Next Step**: Integrate new components into WorkflowContext internals.
