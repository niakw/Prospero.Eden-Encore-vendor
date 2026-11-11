// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only contract test for the exact maintained NS2Research header.
// Vulkan submission/physical allocation authentication is NOT tested here.
#include "ns2_verified_upload_batch.h"
#include "ns2_staging_upload_bounds.h"
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
        .source_memory=1001,.destination_memory=2002,
        .source_memory_offset=0,.destination_memory_offset=0,
        .source_backing_bytes=4096,.destination_backing_bytes=4096,
        .source_offset=0,.destination_offset=0,.bytes=64,
        .channel_generation=7,.queue_generation=3,.allow_reorder=true};
    const StagingUploadBounds sb{
        .staging_offset=128, .mapped_bytes=256, .staging_capacity=1024,
        .staging_binding_offset=4096, .destination_capacity=512,
        .destination_binding_offset=8192, .same_memory=false};
    ck(VerifyStagingUploadCopy(sb,{128,0,256}));
    ck(VerifyStagingUploadCopy(sb,{255,500,12}));
    ck(!VerifyStagingUploadCopy(sb,{127,0,1}));
    ck(!VerifyStagingUploadCopy(sb,{128,0,257}));
    ck(!VerifyStagingUploadCopy(sb,{128,511,2}));
    ck(!VerifyStagingUploadCopy(sb,{1000,0,64}));
    auto overflow_binding=sb;
    overflow_binding.staging_binding_offset=std::numeric_limits<std::uint64_t>::max();
    ck(!VerifyStagingUploadCopy(overflow_binding,{128,0,1}));
    auto alias=sb;alias.same_memory=true;alias.destination_binding_offset=4096;
    ck(!VerifyStagingUploadCopy(alias,{128,128,32}));
    ck(VerifyStagingUploadCopy(alias,{128,256,32}));
    ck(VerifyStagingUploadCopy(alias,{128,0,32}));
    ck(IsCurrentStagingRequest(1, 9, 9));
    ck(IsCurrentStagingRequest(999, 9, 9));
    ck(!IsCurrentStagingRequest(0, 9, 9));
    ck(!IsCurrentStagingRequest(1, 0, 0));
    ck(!IsCurrentStagingRequest(1, 9, 10));
    ck(!IsCurrentStagingRequest(1, 10, 9));
    ck(!IsCurrentStagingRequest(1, 0, 9));
    ck(!IsCurrentStagingRequest(0, 9, 0));
    // R2 actually executes this bounded in-call region fusion under opt-in.
    StagingRegionCoalescer<3> fused;
    ck(fused.Size() == 0);
    ck(fused.Append(sb, {128, 0, 16}));
    ck(fused.Append(sb, {144, 16, 16}));
    ck(fused.Size() == 1 && fused.Copies()[0].bytes == 32);
    ck(fused.Append(sb, {192, 128, 16}));
    ck(fused.Size() == 2 && fused.Copies()[1].source_offset == 192);
    ck(fused.Append(sb, {208, 144, 16}));
    ck(fused.Size() == 2 && fused.Copies()[1].bytes == 32);
    ck(fused.Append(sb, {240, 256, 0}));
    ck(fused.Size() == 2); // existing Vulkan path drops zero-size regions
    ck(!fused.Append(sb, {127, 0, 1}));
    ck(fused.Size() == 2); // invalid copy cannot mutate earlier regions
    ck(fused.Append(sb, {248, 256, 8}));
    ck(fused.Size() == 3);
    ck(!fused.Append(sb, {255, 300, 1})); // bounded capacity
    ck(fused.Size() == 3);
    StagingRegionCoalescer<3> nonadjacent;
    ck(nonadjacent.Append(sb, {128, 0, 8}));
    ck(nonadjacent.Append(sb, {136, 32, 8})); // source contiguous, dest not
    ck(nonadjacent.Size() == 2);
    ck(nonadjacent.Append(sb, {160, 40, 8})); // dest contiguous, src not
    ck(nonadjacent.Size() == 3);
    StagingRegionCoalescer<2> same_memory;
    const StagingUploadBounds shared{0, 64, 128, 0, 128, 0, true};
    ck(same_memory.Append(shared, {0, 16, 8}));
    ck(same_memory.Append(shared, {8, 24, 16}));
    ck(same_memory.Size() == 2); // combined spans would overlap physically
    ck(same_memory.Copies()[0].bytes == 8 && same_memory.Copies()[1].bytes == 16);
    ck(!same_memory.Append(shared, {48, 80, 8})); // capacity exhausted
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
    // Two different VMA allocation IDs can still alias the same bound
    // VkDeviceMemory interval. Reject RAW/WAW and in-copy overlaps.
    auto alias_read=next;
    alias_read.source_allocation=444;alias_read.source_buffer=55;
    alias_read.source_memory=2002;
    alias_read.destination_allocation=555;alias_read.destination_buffer=66;
    alias_read.destination_memory=3003;
    ck(!batch.CanAppend(alias_read)); // reads prior destination by memory alias
    auto alias_write=next;
    alias_write.source_allocation=444;alias_write.source_buffer=55;
    alias_write.source_memory=3003;
    alias_write.destination_allocation=555;alias_write.destination_buffer=66;
    alias_write.destination_memory=2002;
    alias_write.destination_offset=64;
    ck(!batch.CanAppend(alias_write)); // WAW across different VMA allocations
    alias_write.destination_memory_offset=4096;
    ck(batch.CanAppend(alias_write)); // same VkDeviceMemory, disjoint offsets
    auto self_alias=next;
    self_alias.source_allocation=444;self_alias.destination_allocation=555;
    self_alias.source_buffer=55;self_alias.destination_buffer=66;
    self_alias.source_memory=2002;self_alias.destination_memory=2002;
    self_alias.source_offset=0;self_alias.destination_offset=0;
    ck(!batch.CanAppend(self_alias)); // same-copy backing alias
    auto bad_binding=next;
    bad_binding.source_memory_offset=std::numeric_limits<U64>::max()-16;
    ck(!batch.CanAppend(bad_binding)); // binding+capacity would wrap
    bad_binding=next;bad_binding.destination_memory=0;
    ck(!batch.CanAppend(bad_binding)); // missing backing identity
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
