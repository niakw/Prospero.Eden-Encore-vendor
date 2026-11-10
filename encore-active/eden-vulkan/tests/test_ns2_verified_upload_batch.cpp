// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only contract test for the exact maintained NS2Research header.
// Vulkan submission/physical allocation authentication is NOT tested here.
#include "ns2_verified_upload_batch.h"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace Vulkan::NS2Research;
int main() {
    int checks{};
    auto ck=[&](bool ok){ ++checks; if(!ok)throw std::runtime_error("verified batch test #"+std::to_string(checks)); };
    const VerifiedCopy base{.source_buffer=11,.destination_buffer=22,
        .source_allocation=111,.destination_allocation=222,
        .source_backing_bytes=4096,.destination_backing_bytes=4096,
        .source_offset=0,.destination_offset=0,.bytes=64,
        .channel_generation=7,.queue_generation=3,.allow_reorder=true};
    VerifiedUploadBatch batch;
    ck(batch.Empty()&&batch.Size()==0&&batch.GroupCount()==0);
    ck(batch.CanAppend(base));ck(batch.Append(base));
    ck(batch.Size()==1&&batch.GroupCount()==1&&batch.Groups()[0].first==0);
    ck(!batch.CanAppend(base)); // same destination overwrite
    auto next=base;next.source_offset=64;next.destination_offset=64;
    ck(batch.CanAppend(next));ck(batch.Append(next));
    ck(batch.Size()==2&&batch.GroupCount()==1&&batch.Groups()[0].count==2);
    next.destination_offset=127;
    ck(!batch.CanAppend(next));
    next.destination_offset=128;
    ck(batch.CanAppend(next));
    auto raw=next;raw.source_allocation=222;raw.source_buffer=33;
    raw.destination_allocation=333;raw.destination_buffer=44;
    ck(!batch.CanAppend(raw)); // RAW hazard
    auto war=next;war.destination_allocation=111;war.destination_buffer=33;
    war.source_allocation=333;war.source_buffer=44;
    ck(!batch.CanAppend(war)); // WAR hazard
    auto waw=next;waw.source_allocation=333;waw.source_buffer=44;
    waw.destination_allocation=222;waw.destination_buffer=22;
    waw.destination_offset=128;
    ck(batch.CanAppend(waw));ck(batch.Append(waw));
    ck(batch.GroupCount()==2&&batch.Groups()[1].first==2);
    waw.destination_offset=127;
    ck(!batch.CanAppend(waw)); // alias across different VkBuffer source
    auto change=next;change.channel_generation=8;
    ck(!batch.CanAppend(change));
    change=next;change.queue_generation=4;
    ck(!batch.CanAppend(change));
    change=next;change.allow_reorder=false;
    ck(!batch.CanAppend(change));
    change=next;change.bytes=0;
    ck(!batch.CanAppend(change));
    change=next;change.source_buffer=0;
    ck(!batch.CanAppend(change));
    change=next;change.destination_buffer=0;
    ck(!batch.CanAppend(change));
    change=next;change.source_allocation=0;
    ck(!batch.CanAppend(change));
    change=next;change.destination_allocation=0;
    ck(!batch.CanAppend(change));
    change=next;change.source_allocation=222;
    ck(!batch.CanAppend(change)); // same allocation source/dst rejected
    change=next;change.source_offset=4097;
    ck(!batch.CanAppend(change));
    change=next;change.destination_offset=4097;
    ck(!batch.CanAppend(change));
    change=next;change.source_offset=4096;change.bytes=1;
    ck(!batch.CanAppend(change));
    change=next;change.destination_offset=4096;change.bytes=1;
    ck(!batch.CanAppend(change));
    change=next;change.source_offset=4032;change.bytes=64;
    change.destination_offset=4032;
    ck(batch.CanAppend(change)); // exact inclusive end of backing
    batch.Clear();
    ck(batch.Empty()&&batch.Size()==0&&batch.GroupCount()==0);
    // Each append gets disjoint target and source bytes, reaching exactly 64.
    for (std::uint64_t i=0;i<VerifiedUploadBatch::MaxCopies;++i){
        auto c=base;c.source_offset=i*64;c.destination_offset=i*64;
        ck(batch.Append(c));
    }
    ck(batch.Size()==64&&batch.GroupCount()==1);
    auto overflow=base;overflow.source_offset=0;overflow.destination_offset=0;
    ck(!batch.CanAppend(overflow));
    ck(!batch.Append(overflow));
    ck(batch.Copies().size()==VerifiedUploadBatch::MaxCopies);
    ck(batch.Groups().size()==1&&batch.Groups()[0].count==64);
    batch.Clear();
    auto high=base;high.source_backing_bytes=std::numeric_limits<U64>::max();
    high.destination_backing_bytes=std::numeric_limits<U64>::max();
    high.source_offset=std::numeric_limits<U64>::max()-64;
    high.destination_offset=std::numeric_limits<U64>::max()-64;
    ck(batch.Append(high)); // valid exact end without unsigned wrap
    high.source_offset=std::numeric_limits<U64>::max()-62;
    high.destination_offset=std::numeric_limits<U64>::max()-62;
    ck(!batch.CanAppend(high)); // bounds refusal
    batch.Clear();
    auto missing=base;missing.queue_generation=0;
    ck(!batch.CanAppend(missing));
    missing=base;missing.channel_generation=0;
    ck(!batch.CanAppend(missing));
    std::cout<<"PASS: "<<checks<<" verified upload batch ownership, range, alias and epoch contracts\n";
}
