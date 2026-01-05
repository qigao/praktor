# Contributing to Praktor

**Welcome to the Praktor community!** 🎉 We're excited you're interested in contributing to the future of workflow orchestration.

## 🚀 Quick Start for Contributors

### 1. Choose Your Adventure

**New to Praktor?** Start here:
- [ ] ⭐ **Star the repository** to show support
- [ ] 📖 **Read the [README](README.md)** to understand Praktor
- [ ] 🎮 **Try the examples**: `bin/praktor -f examples/cpp-pipeline.yml`
- [ ] 💬 **Join our community** discussions

**Ready to code?** Pick an area:
- [ ] 🐛 **Fix bugs** - Check [good first issue](https://github.com/praktor-workflow/praktor/labels/good%20first%20issue) label
- [ ] ✨ **Add features** - Pick from our [roadmap checkboxes](README.md#whats-next---community-roadmap)
- [ ] 📚 **Build examples** - Create workflows for new technology stacks
- [ ] 📝 **Improve docs** - Help others understand and use Praktor

### 2. Development Setup

```bash
# 1. Fork and clone the repository
git clone https://github.com/YOUR-USERNAME/praktor.git
cd praktor

# 2. Install dependencies (vcpkg handles this)
cmake --preset=default

# 3. Build the project
cmake --build build/Ninja/Msvc

# 4. Run tests to ensure everything works
ctest --preset=default

# 5. Try a workflow
bin/praktor -f examples/hello-world.yml
```

**Development Requirements:**
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- CMake 3.15+
- vcpkg (for dependency management)

## 🎯 Contribution Areas

### 🐛 Bug Fixes
**Perfect for**: First-time contributors, getting familiar with codebase

**Current Priority Issues:**
- Variable substitution edge cases
- Cross-platform compatibility issues
- Memory leaks in long-running workflows
- Error message improvements

**How to contribute:**
1. Find an issue labeled `bug` or `good first issue`
2. Comment that you're working on it
3. Create a branch: `git checkout -b fix/issue-description`
4. Fix the bug and add tests
5. Submit a PR with clear description

### ✨ New Features
**Perfect for**: Experienced developers, those wanting to make big impact

**High-Priority Features** (from roadmap):
- [ ] VS Code extension with syntax highlighting
- [x] `praktor validate` command for workflow validation
- [ ] Kubernetes operator for distributed execution
- [ ] Workflow result caching system

**How to contribute:**
1. Pick a checkbox from the [roadmap](README.md#whats-next---community-roadmap)
2. Open an issue to discuss implementation approach
3. Get feedback from maintainers
4. Implement with tests and documentation
5. Submit PR for review

### 📚 Examples Development
**Perfect for**: Domain experts, those who want immediate user impact

**Missing Examples** (high demand):
- [ ] **Web Development**: React, Vue, Angular build pipelines
- [ ] **Machine Learning**: MLOps, model training, data processing
- [ ] **Mobile Development**: React Native, Flutter workflows
- [ ] **Security**: Vulnerability scanning, compliance automation
- [ ] **Data Engineering**: Spark, ETL, data warehouse integration

**How to contribute:**
1. Choose a technology stack you know well
2. Create `examples/TECH-NAME/` directory
3. Build workflows following our standards
4. Test with real projects
5. Document with a README in the directory

**Example Template:**
```
examples/your-tech/
├── README.md                    # Overview and usage guide
├── workflow.yml                 # The main workflow file
└── sub-workflows/               # (Optional) nested workflows
```

### 📝 Documentation
**Perfect for**: Technical writers, those who love helping others

**Documentation Needs:**
- [ ] **Tutorial videos** for getting started
- [ ] **Advanced guides** for enterprise features
- [ ] **Collection authoring guide** with best practices
- [ ] **Troubleshooting guides** for common issues
- [ ] **API documentation** for C++ embedding

### 🔧 Core Engine Development
**Perfect for**: C++ experts, systems programmers, performance enthusiasts

**Core Areas:**
- [ ] **Parser improvements** - Better error messages, validation
- [ ] **Execution engine** - Performance optimization, memory usage
- [ ] **Import system** - Circular dependency detection, caching
- [ ] **Expression evaluator** - More operators, better performance
- [ ] **Thread pool** - Better load balancing, resource management

## 📋 Development Workflow

### Setting Up Your Branch
```bash
# Always start from main
git checkout main
git pull origin main

# Create feature branch
git checkout -b feature/your-feature-name
# or
git checkout -b fix/bug-description
# or
git checkout -b collection/tech-name
```

### Code Style Guidelines

**C++ Code:**
- Follow existing code style (we use `.clang-format`)
- Use modern C++20 features when appropriate
- Add unit tests for new functionality
- Document public APIs with doxygen comments
- No raw pointers (use smart pointers)

**YAML Workflows:**
- Use clear, descriptive task names
- Add comments explaining complex workflows
- Follow variable naming conventions (UPPER_CASE for globals)
- Include usage examples in documentation

**Documentation:**
- Use clear, concise language
- Include code examples
- Test all examples before submitting
- Follow existing markdown style

### Testing Your Changes

```bash
# Run all tests
ctest --preset=default

# Test specific functionality
bin/praktor.exe your-test-workflow.yml

# Test collections
bin/praktor.exe praktor/collections/your-collection/full-pipeline.yml

# Performance testing
bin/praktor -f your-workflow.yml --benchmark
```

### Submitting Your PR

**PR Title Format:**
- `feat: add VS Code syntax highlighting`
- `fix: resolve variable substitution in parallel tasks`
- `docs: improve getting started guide`
- `collection: add React build workflows`

**PR Description Template:**
```markdown
## What This PR Does
Brief description of the change

## Why This Change Is Needed
Problem you're solving or feature you're adding

## How to Test
Step-by-step instructions for reviewers

## Checklist
- [ ] Tests pass locally
- [ ] Added unit tests for new functionality
- [ ] Updated documentation
- [ ] Tested with real workflows
- [ ] No breaking changes (or documented)
```

## 🏆 Recognition

**Contributors Hall of Fame:**
We celebrate all contributions! Contributors get:
- [ ] **Mention in release notes** for significant contributions
- [ ] **Contributor badge** on GitHub profile
- [ ] **Early access** to new features
- [ ] **Swag** for major contributions (stickers, shirts)
- [ ] **Conference speaking opportunities** for collection authors

**Contribution Levels:**
- 🥉 **Bronze**: 1+ merged PR, documentation contributions
- 🥈 **Silver**: 5+ PRs, collection contributions, bug fixes
- 🥇 **Gold**: 15+ PRs, major feature contributions, maintainer help
- 💎 **Diamond**: Core maintainer, significant architectural contributions

## 🤝 Community Guidelines

### Code of Conduct
- **Be respectful** - Everyone was new once
- **Be constructive** - Focus on the code, not the person
- **Be collaborative** - We're building something together
- **Be inclusive** - Welcome developers of all backgrounds and skill levels

### Getting Help
- **GitHub Discussions** for questions and brainstorming
- **Issues** for bugs and feature requests
- **PR comments** for code-specific questions
- **Discord/Slack** for real-time chat (coming soon!)

### Communication
- **English** is the primary language for code and documentation
- **Clear communication** - Explain your reasoning
- **Patient** - Reviews take time, maintainers are volunteers
- **Responsive** - Address feedback promptly

## 🚦 Contribution Priorities

### 🟢 High Priority (Always Welcome)
- Bug fixes with tests
- Performance improvements
- Security enhancements
- Documentation improvements
- Collection development for popular technologies

### 🟡 Medium Priority (Discuss First)
- New core features
- API changes
- Breaking changes
- Large architectural modifications

### 🟠 Low Priority (Major Discussion Needed)
- Experimental features
- Platform-specific implementations
- Dependencies on external services

## 📞 Contact Maintainers

- **Feature discussions**: Open a GitHub issue
- **Contribution questions**: Use GitHub Discussions
- **Security issues**: Email security@praktor-workflow.io
- **Partnership inquiries**: Email partnerships@praktor-workflow.io

---

## 🎉 Ready to Contribute?

1. **⭐ Star the repo** if you haven't already
2. **🍴 Fork the repository**
3. **📋 Pick an issue** or feature from our roadmap
4. **💻 Start coding** following our guidelines
5. **🔄 Submit your PR** and celebrate! 🎊

**Remember**: Every expert was once a beginner. Don't hesitate to ask questions, and welcome to the Praktor community!

**Your contribution, no matter how small, makes Praktor better for thousands of developers worldwide.** 🌍✨
