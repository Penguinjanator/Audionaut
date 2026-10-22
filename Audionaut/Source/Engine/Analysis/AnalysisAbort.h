//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <atomic>

namespace audium {

/**
 * @brief Whether an analysis stage should give up.
 *
 * Every stage of an analysis takes an optional abort flag and polls it
 * between its units of work (decoded blocks, STFT frames, BIC windows);
 * AnalysisWorker's destructor sets it so quitting waits for one unit at
 * most. A null flag means nobody can abort.
 */
inline bool isAborted(const std::atomic<bool>* shouldAbort) noexcept
{
    return shouldAbort != nullptr && shouldAbort->load(std::memory_order_relaxed);
}

} // namespace audium
