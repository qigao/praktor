# Praktor Expression Skill

Generate expressions for Praktor workflow `when` conditions and Mustache templates for variable interpolation.

## Two-Tier System

| Context | Implementation | Capabilities |
| :--- | :--- | :--- |
| **Templating** (Vars, Env, Command) | **Mustache** | Variable substitution, Loops, Sections, Inverted Sections |
| **Logic** (`when` clause) | **PEG Evaluator** | Logical operators (`and`, `or`), Comparison, Functions |

## 1. Mustache Templating

Used in `command`, `vars`, `env`, `working_dir`, and `script.source`.

### Basic Variables
```yaml
command: "echo Hello {{ NAME }}!"
```

### Deep Object Access
Supports dot notation for nested objects:
```yaml
command: "echo Build: {{ tasks.build.outputs.data.version.major }}"
```

### Sections & Loops (`{{# ... }}`)
Renders if the value is truthy (true, non-empty list, non-empty object).
```yaml
# Iterating over a list
command: |
  {{#tasks.build.outputs.data.artifacts}}
  echo "Artifact: {{name}} ({{size}} bytes)"
  {{/tasks.build.outputs.data.artifacts}}

# Conditional block
command: |
  {{#env.DEBUG}}
  echo "DEBUG MODE ENABLED"
  {{/env.DEBUG}}
```

### Inverted Sections (`{{^ ... }}`)
Renders if the value is falsy (false, null, empty list, empty object).
```yaml
command: |
  {{^tasks.test.outputs.data.errors}}
  echo "NO ERRORS FOUND"
  {{/tasks.test.outputs.data.errors}}
```

## 2. Expression Language (for `when`)

Used exclusively in the `when` field for logical flow control.

### Syntax
Expressions are still wrapped in `{{ ... }}`.

### Variable Access
Same deep path access as Mustache: `{{ tasks.build.outputs.version }}`.

### Comparison Operators
```yaml
when: "{{ VERSION }} == '1.0.0'"
when: "{{ tasks.test.outputs.exit_code }} == 0"
when: "{{ env.BRANCH }} != 'main'"
when: "{{ tasks.count.outputs.value }} > 10"
```

### Logical Operators
```yaml
when: "{{ env.BRANCH }} == 'main' and {{ tasks.test.outputs.exit_code }} == 0"
when: "not {{ tasks.skip.outputs.should_skip }}"
when: "!{{ env.SKIP_DEPLOY }}"
```

### String & Regex Operators
```yaml
when: "{{ VERSION }} starts_with 'v'"
when: "{{ FILENAME }} ends_with '.json'"
when: "{{ EMAIL }} matches '.*@company\\.com'"
when: "{{ STATUS }} in ['ready', 'pending', 'active']"  # Check if value is in list
```

### Built-in Functions
- `len(value)`: `len({{ tasks.fetch.outputs.data }}) > 0`
- `empty(value)`: `not empty({{ tasks.fetch.outputs.data }})`
- `bool(value)`: `bool({{ env.ENABLED }})`

## 3. Switch/Case Patterns

The `when` keyword doesn't have native switch/case syntax, but you can achieve the same effect using these patterns:

### Pattern 1: Using `in` Operator (Recommended)
```yaml
tasks:
  - name: deploy_non_prod
    command: "./deploy.sh --non-prod"
    when: "{{ ENVIRONMENT }} in ['dev', 'test', 'staging']"

  - name: handle_client_errors
    command: "./handle-error.sh"
    when: "{{ tasks.api_call.outputs.status }} in [400, 401, 403, 404, 422]"
```

### Pattern 2: Multiple Tasks (Switch-like)
```yaml
tasks:
  - name: deploy_dev
    command: "./deploy.sh --env dev --debug"
    when: "{{ ENVIRONMENT }} == 'dev'"

  - name: deploy_staging
    command: "./deploy.sh --env staging --verify"
    when: "{{ ENVIRONMENT }} == 'staging'"

  - name: deploy_production
    command: "./deploy.sh --env production --strict"
    when: "{{ ENVIRONMENT }} == 'production'"
```

### Pattern 3: Using OR Operators
```yaml
tasks:
  - name: deploy
    command: "./deploy.sh"
    when: "{{ ENVIRONMENT }} == 'dev' or {{ ENVIRONMENT }} == 'staging' or {{ ENVIRONMENT }} == 'production'"
```

### Pattern 4: Script Task (Complex Logic)
```yaml
tasks:
  - name: determine_action
    script:
      source: |
        const env = context.get("ENVIRONMENT");
        let action = "";
        
        switch(env) {
          case "dev": action = "deploy-debug"; break;
          case "staging": action = "deploy-verify"; break;
          case "production": action = "deploy-strict"; break;
          default: action = "deploy-default";
        }
        
        context.set("deploy_action", action);

  - name: execute_deploy
    depends_on: [determine_action]
    command: "./deploy.sh --action {{ tasks.determine_action.outputs.deploy_action }}"
```

## 4. Variable Paths

| Path | Description |
| :--- | :--- |
| `{{ VAR_NAME }}` | Global variable from `variables:` |
| `{{ env.VAR }}` | Environment variable |
| `{{ tasks.NAME.status }}` | Task status: `pending`, `running`, `completed`, `failed` |
| `{{ tasks.NAME.outputs.stdout }}` | Raw task stdout |
| `{{ tasks.NAME.outputs.data.path }}` | Parsed JSON field (if `output_format: json`) |
| `{{ os.name }}` | OS name (`linux`, `windows`, `darwin`) |

## 5. Choice of Syntax

- Use **Mustache Loops** in blocks of text (scripts, multi-line commands).
- Use **Expressions** for simple boolean decisions in `when`.
- Use **Variables** for simple scalar substitution.
- Use **`in` operator** for checking multiple values (switch-like behavior).
- Use **Script tasks** for complex conditional logic requiring full JavaScript switch/case.
