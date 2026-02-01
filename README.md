# procly

Modern process execution + pipelines for C++.

`procly` is a small, cross-platform library for running subprocesses, wiring stdin/stdout/stderr, and building pipelines—without shell surprises.

- Linux + macOS supported (Windows planned)
- C++20 API, with a limited C++17 compatibility mode
- Result-based error handling (`std::expected`-compatible)
- Deadlock-safe stdout/stderr capture
- First-class pipelines (`cmd1 | cmd2`)

## Outline

- [Why procly](#why-procly)
- [Quickstart](#quickstart)
- [Design highlights](#design-highlights)
- [API overview](#api-overview)
- [Install](#install)
- [Build, test, format, docs](#build-test-format-docs)
- [Platform notes](#platform-notes)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)

## Why procly

C++ has too many ways to spawn processes and too many footguns to go with them.

procly is for when you want:

- a clean `Command` builder (argv, env, cwd, stdio)
- predictable semantics (no shell, no quoting rules)
- robust capture that doesn’t hang under load
- pipelines that feel like the shell, but behave like a library

## Quickstart

### Run a command and capture output

```cpp
#include <procly/command.hpp>

auto out = procly::Command("/bin/echo")
  .arg("hello")
  .output();

if (!out) {
  std::cerr << out.error().code.message() << ": " << out.error().context << "\n";
  return;
}

std::cout << "stdout: " << out->stdout_data;
std::cout << "code:   " << out->status.code().value_or(-1) << "\n";
```

### Pipe into a process (interactive)

```cpp
#include <procly/command.hpp>
#include <procly/pipe.hpp>
#include <procly/stdio.hpp>

auto child = procly::Command("/bin/cat")
  .stdin(procly::Stdio::piped())
  .stdout(procly::Stdio::piped())
  .spawn();

if (!child) {
  return;
}

auto in = child->take_stdin();
auto out = child->take_stdout();

if (in && out) {
  in->write_all("hello\n");
  in->close();

  auto data = out->read_all();
  if (data) {
    std::cout << *data;
  }
}

child->wait();
```

### Pipelines

```cpp
#include <procly/command.hpp>
#include <procly/pipeline.hpp>

auto out = (procly::Command("/bin/ps").arg("aux")
         |  procly::Command("/usr/bin/grep").arg("ssh"))
  .pipefail(true)
  .output();
```

## Design highlights

### No shell by default

procly passes argv directly to the OS. If you want shell behavior, be explicit:

```cpp
procly::Command("/bin/sh").args({"-c", "..."});
```

### Output capture that doesn’t deadlock

stdout/stderr capture is drained in a poll-based loop on POSIX. Large outputs are safe.

### Portable status model

The core API exposes:

- `status.success()`
- `status.code()` (when representable)
- platform extensions for details like POSIX signals (`procly::unix`)

## API overview

### Command

- `Command(program)`
- `.arg(...)`, `.args(...)`
- `.env(k, v)`, `.env_remove(k)`, `.env_clear()`
- `.current_dir(path)`
- `.stdin(Stdio)`, `.stdout(Stdio)`, `.stderr(Stdio)`
- `.options(SpawnOptions)`
- `.spawn()`, `.status()`, `.output()`
- `.spawn_or_throw()`, `.status_or_throw()`, `.output_or_throw()`

### Stdio

- `Stdio::inherit()`
- `Stdio::null()`
- `Stdio::piped()`
- `Stdio::file(path)` (open mode optional)
- `Stdio::fd(fd)` (POSIX)

### Child

- `child.id()`
- `child.take_stdin()`, `child.take_stdout()`, `child.take_stderr()`
- `child.wait()`, `child.try_wait()`, `child.wait(WaitOptions)`
- `child.terminate()`, `child.kill()`

### Pipeline

- `Command | Command` → `Pipeline`
- `Pipeline::pipefail(true)`
- `Pipeline::new_process_group(true)`
- `Pipeline::spawn()`, `Pipeline::status()`, `Pipeline::output()`

## Install

procly is not published in the bzlmod registry yet. Use an override or an archive.

### bzlmod (git override)

```starlark
# MODULE.bazel
bazel_dep(name = "procly", version = "0.0.0")

git_override(
    module_name = "procly",
    remote = "https://github.com/<you>/procly.git",
    commit = "<commit-sha>",
)
```

### WORKSPACE (http_archive)

```starlark
# WORKSPACE
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "procly",
    urls = ["https://github.com/<you>/procly/archive/refs/tags/v0.1.0.tar.gz"],
    strip_prefix = "procly-0.1.0",
    sha256 = "<sha256>",
)
```

Then depend on the library:

```starlark
cc_library(
    name = "my_app",
    srcs = ["main.cc"],
    deps = ["@procly//:procly"],
)
```

## Build, test, format, docs

### Build

```sh
bazel build //:procly
```

C++17 mode (reduced overload set):

```sh
bazel build --config=cxx17 //:procly
```

### Tests

```sh
bazel test //...
```

### Format

`bazel run //tools:bazel_env` creates the bazel-env tool bin dir used by `format` and relies on direnv by default.
Install direnv first (see `https://direnv.net`).

```sh
bazel run //tools:bazel_env
```

```sh
direnv allow
```

```sh
format
```

### Lint

`aspect lint` is the Aspect CLI lint runner (not the Bazel CLI).

```sh
aspect lint
```

```sh
aspect lint --fix
```

### Docs

```sh
bazel build //docs:api_docs
python3 -m http.server --directory bazel-bin/docs/html 8000
```

See `docs/README.md` for more details.

## Platform notes

- Linux: ✅ spawn, capture, pipelines
- macOS: ✅ spawn, capture, pipelines
- Windows: ⏳ planned

## Roadmap

- Windows backend (CreateProcessW + job objects)
- More stdio options (explicit open modes, append)
- Streaming capture (callbacks, line-based processing)
- Optional PTY support

## Contributing

- Format: `format`
- Lint: `aspect lint` (or `aspect lint --fix`)
- Tests: `bazel test //tests/unit:all //tests/integration:all`
- Docs: public API changes should include Doxygen comments

## License

Apache-2.0
