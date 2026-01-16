#pragma once

#include <memory>

#include "procly/child.hpp"
#include "procly/pipeline.hpp"

namespace procly::internal {

struct Spawned;

struct ChildAccess {
  static std::unique_ptr<procly::Child::Impl>& impl(procly::Child& child) { return child.impl_; }
  static procly::Child from_spawned(Spawned spawned);
};

struct PipelineAccess {
  static std::unique_ptr<procly::PipelineChild::Impl>& impl(procly::PipelineChild& child) {
    return child.impl_;
  }
  static const std::vector<procly::Command>& stages(const procly::Pipeline& pipeline) {
    return pipeline.stages_;
  }
  static bool pipefail(const procly::Pipeline& pipeline) { return pipeline.pipefail_; }
  static bool new_process_group(const procly::Pipeline& pipeline) { return pipeline.new_pgrp_; }
  static const std::optional<procly::Stdio>& stdin_opt(const procly::Pipeline& pipeline) {
    return pipeline.stdin_;
  }
  static const std::optional<procly::Stdio>& stdout_opt(const procly::Pipeline& pipeline) {
    return pipeline.stdout_;
  }
  static const std::optional<procly::Stdio>& stderr_opt(const procly::Pipeline& pipeline) {
    return pipeline.stderr_;
  }
};

}  // namespace procly::internal
