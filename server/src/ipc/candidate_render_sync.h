#pragma once

#include <cstdint>

namespace FanyImeIpc
{
// A digit/space selection settles against the live page_words, but the user is looking at the
// asynchronously painted CandidatePageSnapshot. Pin-frequency reorders a page after every commit, so
// a selection that runs ahead of the painted frame commits a candidate the user never saw.
//
// The wait bound is load-bearing: the TSF side reads this request's reply with a 50ms timeout
// (windows/src/IPC/Ipc.cpp TryReadDataFromServerPipeWithTimeout) and treats a miss as a broken
// transport -- it closes the pipe, reconnects and replays the keystroke. The wait must leave room
// inside that window for the rest of ProcessSelectionKey (candidate resolution and the frequency
// write), so it stays well under the reply timeout on purpose. On timeout the caller continues
// with the current data and the diagnostic log records the miss. Do not raise this without
// re-deriving the TSF budget.
constexpr int kCandidateSelectionRenderWaitMaxMs = 30;

// Header-only pure policy so tests can pin it without linking the Windows/server stack.
// Generation 0 means "never published/rendered". A host-drawn (UI-less) or invisible candidate
// window has no on-screen list to match either, so none of those cases waits.
constexpr bool ShouldWaitForCandidateRender(std::uint64_t rendered, std::uint64_t current, bool uiless,
                                            bool visible) noexcept
{
    if (uiless || !visible)
    {
        return false;
    }
    if (current == 0 || rendered == 0)
    {
        return false;
    }
    return rendered < current;
}
} // namespace FanyImeIpc
