#pragma once

#include "task.hpp"

// Desugars a RunCommandParams into a OrchParams tree.
// Converts command: tasks into equivalent action orchestration nodes at parse time.
OrchParams desugarCommandToorch(const RunCommandParams& params);

