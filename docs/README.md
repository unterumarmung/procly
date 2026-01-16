# Docs

This project generates API documentation with Doxygen via Bazel.

## Build

```sh
bazel build //docs:api_docs
```

The generated HTML lives in the build output:

- `bazel-bin/docs/html`

You can also query the exact output path:

```sh
bazel cquery --output=files //docs:api_docs
```

## Serve locally

Serving the docs avoids browser restrictions on `file://` URLs.

```sh
python3 -m http.server --directory bazel-bin/docs/html 8000
```

Open:

- `http://localhost:8000/`

Stop the server with `Ctrl+C`.
