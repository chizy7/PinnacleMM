# Contributing to PinnacleMM

Thank you for your interest in contributing to PinnacleMM! This document explains how to set up your development environment, the conventions the project enforces in CI, and how to get your changes merged.

By participating in this project, you agree to abide by our [Code of Conduct](CODE_OF_CONDUCT.md).

## Ways to Contribute

- **Report bugs** using the [bug report template](https://github.com/chizy7/PinnacleMM/issues/new?template=bug_report.md)
- **Request features** using the [feature request template](https://github.com/chizy7/PinnacleMM/issues/new?template=feature_request.md)
- **Report security vulnerabilities** privately — see [SECURITY.md](.github/SECURITY.md). **Never open a public issue for a security vulnerability.**
- **Improve documentation** — guides live in [`docs/`](docs/)
- **Submit code** — bug fixes, performance improvements, new strategies, exchange connectors, and tests are all welcome. Check the [Roadmap](docs/ROADMAP.md) for planned work and good areas to help.

For large changes, please open an issue first to discuss the approach before investing significant time.

## Development Setup

### Prerequisites

- C++20 compatible compiler (GCC 10+, Clang 10+, or MSVC 2019+)
- CMake 3.14+
- Boost libraries 1.72+
- OpenSSL
- nlohmann_json
- Google Test and Google Benchmark (for tests and benchmarks)
- Python 3 with `pre-commit` (for the git hooks — see below)

spdlog and fmt are auto-downloaded at pinned versions if not installed locally.

On macOS:

```bash
brew install cmake boost openssl nlohmann-json googletest google-benchmark pre-commit
```

On Ubuntu/Debian:

```bash
sudo apt-get install -y cmake build-essential libboost-all-dev libssl-dev \
  nlohmann-json3-dev libgtest-dev libbenchmark-dev
pip install pre-commit
```

### Build and Test

```bash
# Fork the repo on GitHub, then:
git clone https://github.com/<your-username>/PinnacleMM.git
cd PinnacleMM

# Build (auto-checks dependencies)
scripts/run-native.sh build

# Run the full test suite
scripts/run-native.sh test

# Run performance benchmarks
scripts/run-native.sh benchmark
```

Or manually:

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON ..
make -j$(nproc || sysctl -n hw.ncpu)
```

For memory-safety validation during development, build with sanitizers:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON .. && make -j8
```

Note that CI builds with AddressSanitizer enabled, so memory errors will fail your PR even if they don't crash locally.

## Pre-commit Hooks (Required)

The project uses [pre-commit](https://pre-commit.com/) to enforce formatting and linting. Install the hooks once after cloning:

```bash
pre-commit install
```

After this, every `git commit` automatically runs:

- **clang-format** (v17, using the repo's `.clang-format`) on C/C++ files
- **cmake-format** on CMake files
- **hadolint** on Dockerfiles
- **shellcheck** on shell scripts
- General checks: trailing whitespace, end-of-file newlines, YAML validity, merge-conflict markers, large files, line endings

If a hook fails, the commit is aborted. Formatting hooks fix the files in place — just `git add` the fixed files and commit again. To run all hooks against the whole repo manually:

```bash
pre-commit run --all-files
```

CI runs the same hooks on every PR (`.github/workflows/pre-commit.yml`), so skipping local installation just means you find out later.

## Coding Guidelines

- **C++20**, matching the style of the surrounding code
- Formatting is defined by `.clang-format` — don't hand-format; let the hook do it
- This is an ultra-low latency system: on hot paths, avoid allocations, locks, and exceptions; prefer lock-free structures and `std::atomic` consistent with the existing core engine
- Add unit tests (Google Test) for new functionality; add benchmarks (Google Benchmark) for performance-sensitive changes
- Update the relevant docs in `docs/` when behavior or interfaces change

## Commit Messages and PR Titles

PR titles **must** follow [Conventional Commits](https://www.conventionalcommits.org/) — this is enforced by CI (`pr-title-lint.yml`) and PRs with non-conforming titles cannot merge:

```
<type>(<optional scope>): <subject>
```

- Allowed types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `revert`
- The subject must start with a **lowercase** letter
- The full header must be at most 100 characters

Examples:

```
feat(strategy): add inventory-skew quoting to basic market maker
fix(exchange): handle coinbase websocket reconnect race
perf(core): reduce order book update latency with arena allocator
```

Using the same convention for individual commit messages is encouraged (PRs are typically squash-merged using the PR title).

## Pull Request Process

1. Create a topic branch from `main` in your fork
2. Make your changes, with tests
3. Run locally before pushing:
   - `pre-commit run --all-files`
   - `scripts/run-native.sh test`
4. Open a PR against `main` and fill out the PR template completely
5. Make sure CI passes. On every PR, CI runs:
   - **Build and Test** on Ubuntu and macOS, Debug and Release, with AddressSanitizer
   - **Performance benchmarks** (Release builds)
   - **Pre-commit hooks** (formatting/linting)
   - **PR title lint** (conventional commit format)
   - **Docker build test**
6. Address review feedback; a maintainer will merge once approved

## Performance Expectations

PinnacleMM targets microsecond-level latency. If your change touches the core engine, order book, or execution paths:

- Run the relevant benchmarks before and after (`scripts/run-native.sh benchmark`)
- Include the numbers in your PR description
- Regressions on hot paths need a strong justification

## Questions?

- Check the [documentation](docs/) and [Getting Started Guide](docs/user_guide/getting_started.md) first
- Open a [discussion or issue](https://github.com/chizy7/PinnacleMM/issues)
- Contact the maintainer: [chizy@chizyhub.com](mailto:chizy@chizyhub.com)

Thank you for contributing!
