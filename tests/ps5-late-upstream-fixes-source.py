#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Source proof for two late upstream PS5/Lichii fixes.

0075: alpha-test references only participate when the comparison consumes them
and are quantized to 8-bit alpha precision, avoiding one pipeline per fade step.
0081: worker-stop publication uses the same mutex as WaitForRequests' predicate
to prevent a condition-variable lost wake.

No console/runtime claim is made by this source check.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
fixed = (ROOT / "encore-active/eden-vulkan/fixed_pipeline_state.cpp").read_text()
pinned = (ROOT / "sources/eden/src/common/thread_worker.h").read_text()
active = (ROOT / "encore-active/eden/src/common/thread_worker.h").read_text()

assert "#include <cmath>" in fixed
assert "const u32 packed_alpha_test = alpha_test_func.Value();" in fixed
assert "PackComparisonOp(Maxwell::ComparisonOp::Never_GL)" in fixed
assert "PackComparisonOp(Maxwell::ComparisonOp::Always_GL)" in fixed
assert "std::round(regs.alpha_test_ref * 255.0f) / 255.0f" in fixed

old = """            ++workers_stopped;
            wait_condition.notify_all();"""
new = """            {
                // Lichii #290 / PS5 patch 0081: WaitForRequests checks this predicate while
                // holding queue_mutex. Publish worker termination under the same mutex so a
                // notify cannot land between the predicate check and the condition wait.
                std::scoped_lock lock{queue_mutex};
                ++workers_stopped;
            }
            wait_condition.notify_all();"""
assert pinned.count(old) == 1
assert active.count(new) == 1
assert active.replace(new, old, 1) == pinned
wait = active.split("void WaitForRequests(", 1)[1].split("\n    }", 1)[0]
assert "std::unique_lock lock{queue_mutex};" in wait
assert "workers_stopped >= workers_queued" in wait

print("PS5_LATE_UPSTREAM_FIXES_SOURCE_PASS alpha_ref_quantized=1 worker_stop_same_mutex=1 console_unverified=1")
