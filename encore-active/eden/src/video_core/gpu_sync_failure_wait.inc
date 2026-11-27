    // GPU work is serviced on a separate worker. If that worker exits after
    // a fatal exception without advancing the fence, the original unbounded
    // condition-variable wait would strand this CPU thread indefinitely.
    // Normal requests still sleep on sync_request_cv and wake immediately
    // when TickWork acknowledges their fence; polling is only a failure guard.
    void WaitForSyncOperation(const u64 fence) {
        std::unique_lock lck{sync_request_mutex};
        while (!sync_requests_stopped && CurrentSyncRequestFence() < fence) {
            if (auto failure = ::Eden::PeekGpuFailure())
                std::rethrow_exception(failure);
            sync_request_cv.wait_for(lck, std::chrono::milliseconds(100));
        }
        // Observing the error must not consume it: other CPU readers can be
        // waiting for different GPU fences from the same failed title.
        if (auto failure = ::Eden::PeekGpuFailure())
            std::rethrow_exception(failure);
    }
