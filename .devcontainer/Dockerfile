# Start with the standard devcontainers base (Ubuntu-based)
FROM mcr.microsoft.com/devcontainers/base:ubuntu

# Install dependencies (direnv + curl for downloading bazelisk)
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       direnv curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install bazelisk (latest release), matching container architecture
RUN arch="$(dpkg --print-architecture)" \
    && case "$arch" in \
         amd64|arm64) bazelisk_arch="$arch" ;; \
         *) echo "Unsupported architecture: $arch" >&2; exit 1 ;; \
       esac \
    && curl -L "https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-${bazelisk_arch}" \
         -o /usr/local/bin/bazelisk \
    && chmod +x /usr/local/bin/bazelisk \
    && ln -s /usr/local/bin/bazelisk /usr/local/bin/bazel

# Cache Bazel binaries inside the image to avoid re-downloading per container run.
ENV BAZELISK_HOME=/opt/bazelisk
RUN mkdir -p "$BAZELISK_HOME" 

COPY .bazelversion /tmp/.bazelversion
COPY .bazeliskrc /tmp/.bazeliskrc
RUN set -a \
    && . /tmp/.bazeliskrc \
    && set +a \
    && USE_BAZEL_VERSION="$(cat /tmp/.bazelversion)" \
       bazelisk version >/dev/null \
    && rm /tmp/.bazelversion /tmp/.bazeliskrc

RUN chown -R vscode:vscode "$BAZELISK_HOME"

# Bazel: make output/cache and convenience symlinks live in vscode's cache,
# not in the workspace (prevents stale bazel-* links pointing to /root/.cache).
RUN mkdir -p /home/vscode/.cache/bazel /home/vscode/.cache/bazel-symlinks \
    && chown -R vscode:vscode /home/vscode/.cache

RUN mkdir -p /home/vscode/.cache \
    && chown -R vscode:vscode /home/vscode/.cache

RUN printf '%s\n' \
  'startup --output_user_root=/home/vscode/.cache/bazel' \
  > /etc/bazel.bazelrc

# Hook direnv into bash (default Codespaces shell)
RUN echo 'eval "$(direnv hook bash)"' >> /etc/bash.bashrc
