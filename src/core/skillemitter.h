// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

class ToolRegistry;

// Emits the *machine* portion of an MCP skill: a deterministic Markdown tool
// reference walked from a live ToolRegistry. Hand-written prose (philosophy,
// gotchas, anti-patterns) is concatenated by the consumer.
//
// Output is byte-stable across runs given the same registered tools, so it
// can be diffed in CI to detect drift between an installed skill and the
// currently-shipping tool surface.
namespace SkillEmitter {

// Named render() rather than emit() because Qt defines `emit` as an empty
// macro keyword for signal emission; using it as a function name would expand
// to nonsense at preprocess time.
QString render(const ToolRegistry &registry);

} // namespace SkillEmitter
