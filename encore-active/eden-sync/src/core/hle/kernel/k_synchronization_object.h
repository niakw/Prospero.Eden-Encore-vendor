// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#include "core/hle/kernel/k_auto_object.h"
#include "core/hle/result.h"

namespace Kernel {

class KernelCore;
class Synchronization;
class KThread;

/// Class that represents a Kernel object that a thread can be waiting on
class KSynchronizationObject : public KAutoObjectWithList {
    KERNEL_AUTOOBJECT_TRAITS(KSynchronizationObject, KAutoObject);

public:
    struct ThreadListNode {
        ThreadListNode* next{};
        KThread* thread{};
    };

    static Result Wait(KernelCore& kernel, s32* out_index, KSynchronizationObject** objects, const s32 num_objects, s64 timeout);

    void Finalize(KernelCore& kernel) override;

    virtual bool IsSignaled(KernelCore& kernel) const = 0;

    std::vector<KThread*> GetWaitingThreadsForDebugging(KernelCore& kernel) const;

    void LinkNode(ThreadListNode* node_) {
        // Link the node to the list.
        if (m_thread_list_tail == nullptr) {
            m_thread_list_head = node_;
        } else {
            m_thread_list_tail->next = node_;
        }

        m_thread_list_tail = node_;
    }

    void UnlinkNode(ThreadListNode* node_) {
        // Unlink the node from the list.
        // Follow the real pointer links; casting the address of the head
        // pointer to a ThreadListNode object violates C++ aliasing rules.
        ThreadListNode** link = std::addressof(m_thread_list_head);
        ThreadListNode* previous = nullptr;
        while (*link != node_) {
            ASSERT(*link != nullptr);
            previous = *link;
            link = std::addressof((*link)->next);
        }

        if (m_thread_list_tail == node_) {
            m_thread_list_tail = previous;
        }

        // Preserve node_->next: NotifyAvailable can still be walking the
        // removed node until its scheduler-locked iteration completes.
        *link = node_->next;
    }

protected:
    explicit KSynchronizationObject(KernelCore& kernel);
    ~KSynchronizationObject() override;

    virtual void OnFinalizeSynchronizationObject() {}

    void NotifyAvailable(KernelCore& kernel, Result result);
    void NotifyAvailable(KernelCore& kernel) {
        return this->NotifyAvailable(kernel, ResultSuccess);
    }

private:
    ThreadListNode* m_thread_list_head{};
    ThreadListNode* m_thread_list_tail{};
};

} // namespace Kernel
