#pragma once

// MPI message tags
constexpr int TAG_URL       = 1;  // Master→WorkerA, WorkerA→WorkerB: URL to fetch
constexpr int TAG_TERMINATE = 2;  // termination signal
constexpr int TAG_RESULT    = 3;  // WorkerB→WorkerA: parsed page result
constexpr int TAG_DONE      = 4;  // WorkerA→Master: complete domain results
