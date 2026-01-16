# Examples

These examples are runnable and testable Bazel `cc_test` targets.

Run one example:

```sh
bazel run //examples:command_output
```

Run all examples as tests:

```sh
bazel test //examples:all
```

## Example list

- `command_output`: Capture stdout/stderr with `Command::output()`.
- `command_status`: Check exit status and exit code with `Command::status()`.
- `command_env`: Manage environment variables with `env_clear()`, `env()`, and `env_remove()`.
- `command_cwd`: Override working directory with `current_dir()`.
- `stdio_pipes`: Use piped stdin/stdout with `PipeReader`/`PipeWriter`.
- `stdio_files`: Redirect stdin/stdout to files.
- `stdio_null`: Redirect output to the null device.
- `stdio_append`: Append stdout to a file with `OpenMode::write_append`.
- `merge_stderr`: Merge stderr into stdout with `SpawnOptions`.
- `wait_timeout`: Use `WaitOptions` to enforce timeouts.
- `try_wait`: Non-blocking wait with `Child::try_wait()`.
- `pipeline_output`: Capture output from a pipeline.
- `pipeline_pipefail`: Use pipefail aggregation for pipelines.
- `pipeline_pipes`: Pipe data into/out of a pipeline with `PipelineChild`.
