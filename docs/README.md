# Prakter Documentation Index

Welcome to the Prakter workflow engine documentation! This index will help you find the right documentation for your needs.

## 📚 Documentation Overview

| Document | Purpose | Audience | Read Time |
|----------|---------|----------|-----------|
| **[PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)** | Detailed structure analysis | New developers, architects | 15 min |
| **[ARCHITECTURE.md](ARCHITECTURE.md)** | System architecture & design | Architects, senior developers | 20 min |
| **[TRIGGERS.md](TRIGGERS.md)** | Trigger system guide | Feature developers | 15 min |
| **[../grammar.md](../grammar.md)** | DSL specification | All developers | 30 min |

## 🎯 Start Here

### I'm New to Prakter
1. Read **[../README.md](../README.md)** - Project overview
2. Read **[PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)** - Understand the structure
3. Try **[../examples/](../examples/)** - Run example workflows
4. Read **[../grammar.md](../grammar.md)** - Learn the DSL

### I Want to Contribute
1. Read **[../CONTRIBUTING.md](../CONTRIBUTING.md)** - Contribution guidelines
2. Read **[PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)** - Understand the codebase
3. Read **[ARCHITECTURE.md](ARCHITECTURE.md)** - Understand the design

### I Want to Add a Feature
1. Read **[PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)** - Find relevant files
2. Read **[ARCHITECTURE.md](ARCHITECTURE.md)** - Understand extension points
3. Check **[TRIGGERS.md](TRIGGERS.md)** - If adding trigger features

### I Want to Understand the Code
1. Read **[ARCHITECTURE.md](ARCHITECTURE.md)** - High-level design
2. Read **[PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)** - File organization
3. Browse **[../prakter/](../prakter/)** - Source code

## 📖 Documentation by Topic

### Project Structure
- **[PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)** - Comprehensive structure guide

### Architecture & Design
- **[ARCHITECTURE.md](ARCHITECTURE.md)** - System architecture diagrams
- **[../grammar.md](../grammar.md)** - DSL specification (v7.0)

### Features
- **[TRIGGERS.md](TRIGGERS.md)** - Complete trigger system guide

### Examples
- **[../examples/](../examples/)** - Example workflows

## 🔍 Quick Lookup

### Find Code

| What | Where | Documentation |
|------|-------|---------------|
| Workflow execution | `prakter/src/dag/workflow_executor.cpp` | [ARCHITECTURE.md](ARCHITECTURE.md) |
| CLI Subcommands | `prakter/main.cpp` | [../README.md](../README.md#cli-usage) |
| YAML parsing | `prakter/src/yml/task_parser.cpp` | [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) |
| Task executors | `prakter/src/executors/` | [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) |
| Trigger system | `prakter/src/dag/trigger_executor.cpp` | [TRIGGERS.md](TRIGGERS.md) |
| Expression eval | `prakter/src/expressions/` | [ARCHITECTURE.md](ARCHITECTURE.md) |
| Data models | `prakter/include/yml/task*.hpp` | [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) |
| GHA Exporter | `prakter/include/util/gha_exporter.hpp` | [../README.md](../README.md#cli-usage) |
| DAG Visualization | `prakter/include/util/dag_exporter.hpp` | [../README.md](../README.md#cli-usage) |

### Common Tasks

| Task | Documentation |
|------|---------------|
| Add new task type | [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md#common-tasks) |
| Add trigger action | [TRIGGERS.md](TRIGGERS.md) |
| Modify YAML syntax | [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) |
| Understand execution flow | [ARCHITECTURE.md](ARCHITECTURE.md#execution-sequence) |
| Scaffold project | [../README.md](../README.md#cli-usage) |
| Visualize workflow | [../README.md](../README.md#cli-usage) |
| Export to CI | [../README.md](../README.md#cli-usage) |

## 📋 Documentation Standards

### File Naming
- Use `UPPERCASE.md` for root-level docs
- Use `PascalCase.md` for docs/ folder
- Use descriptive names (not `doc1.md`)

### Content Structure
- Start with purpose and audience
- Include table of contents for long docs
- Use diagrams and examples
- Keep sections focused
- Link to related docs

### Maintenance
- Update docs with code changes
- Review docs quarterly
- Archive outdated docs
- Keep examples working

## 🎓 Learning Path

### Beginner (Week 1)
- [ ] Read README.md
- [ ] Read FOLDER_GUIDE.md
- [ ] Run example workflows
- [ ] Read grammar.md basics

### Intermediate (Week 2-3)
- [ ] Read PROJECT_STRUCTURE.md
- [ ] Read ARCHITECTURE.md
- [ ] Modify an example workflow
- [ ] Add a simple feature

### Advanced (Week 4+)
- [ ] Read all documentation
- [ ] Understand full architecture
- [ ] Contribute a feature
- [ ] Review refactoring plan

## 🔗 External Resources

### C++ Resources
- [C++20 Features](https://en.cppreference.com/w/cpp/20)
- [Modern C++ Best Practices](https://github.com/cpp-best-practices/cppbestpractices)

### Libraries Used
- [ryml](https://github.com/biojppm/rapidyaml) - YAML parsing
- [jsoncons](https://github.com/danielaparker/jsoncons) - JSON + JMESPath
- [quickjs-ng](https://github.com/quickjs-ng/quickjs) - JavaScript engine
- [pegtl](https://github.com/taocpp/PEGTL) - Expression parsing

### Build Tools
- [CMake](https://cmake.org/documentation/)
- [vcpkg](https://vcpkg.io/en/getting-started.html)
- [cxxopts](https://github.com/jarro2783/cxxopts) - CLI parsing library

## 📝 Contributing to Documentation

### Adding New Documentation
1. Choose appropriate location (root vs docs/)
2. Follow naming conventions
3. Include in this index
4. Link from related docs
5. Update table of contents

### Improving Existing Documentation
1. Fix errors and typos
2. Add missing information
3. Update outdated content
4. Improve clarity
5. Add examples

### Documentation Review Checklist
- [ ] Purpose clearly stated
- [ ] Audience identified
- [ ] Content accurate
- [ ] Examples work
- [ ] Links valid
- [ ] Formatting consistent
- [ ] Indexed properly

## 🆘 Getting Help

### Documentation Issues
- Check this index first
- Search existing docs
- Open an issue on GitHub
- Ask in discussions

### Code Issues
- Read relevant documentation
- Check examples
- Review architecture
- Open an issue with details

## 📊 Documentation Metrics

### Coverage
- ✅ Project structure documented
- ✅ Architecture documented
- ✅ Features documented (triggers)
- ✅ Examples provided
- ✅ Navigation guides created
- ⚠️ API reference (future)
- ⚠️ Tutorial series (future)

### Quality
- ✅ Clear and concise
- ✅ Well-organized
- ✅ Includes diagrams
- ✅ Has examples
- ✅ Cross-referenced
- ✅ Up-to-date

## 🎯 Documentation Roadmap

### Short Term
- [ ] Add API reference (Doxygen)
- [ ] Create video tutorials
- [ ] Add more examples
- [ ] Translate to other languages

### Long Term
- [ ] Interactive documentation
- [ ] Playground environment
- [ ] Community cookbook
- [ ] Best practices guide

## 📞 Contact

- **Issues:** [GitHub Issues](https://github.com/your-repo/prakter/issues)
- **Discussions:** [GitHub Discussions](https://github.com/your-repo/prakter/discussions)
- **Email:** [your-email@example.com](mailto:your-email@example.com)

---

**Last Updated:** October 2025
**Documentation Version:** 1.0
**Prakter Version:** 1.0.0
