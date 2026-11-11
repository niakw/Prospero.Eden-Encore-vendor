// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only extraction of the real generic BufferCache destructor. Test-only
// resident owner and runtime stubs verify that Finish precedes member teardown.
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
struct Record {
    std::vector<int>* events{};
    int id{};
    explicit Record(std::vector<int>& log, int value) : events(&log), id(value) {}
    Record(Record&& other) noexcept
        : events(std::exchange(other.events, nullptr)), id(other.id) {}
    Record(const Record&) = delete;
    ~Record() { if(events) events->push_back(id); }
};
struct GPU {
    std::vector<int>* events{};
    void Finish() { events->push_back(1); }
};
struct Params { static constexpr bool IS_OPENGL = false; };
template<class P> struct BufferCache {
    GPU& runtime;
    std::vector<Record> slot_buffers;
    std::vector<Record> deleted_frame_ring;
    explicit BufferCache(GPU& gpu) : runtime(gpu) {}
    ~BufferCache();
};
@@REAL_CACHE_DESTRUCTOR@@
int main() {
    std::vector<int> order;
    GPU gpu{&order};
    {
        BufferCache<Params> cache{gpu};
        cache.slot_buffers.emplace_back(order, 2);
        cache.deleted_frame_ring.emplace_back(order, 3);
    }
    if (order != std::vector<int>{1,3,2}) {
        std::cerr << "Unexpected GPU finish/destruction ordering\n";
        return 1;
    }
    std::cout << "PASS: R2 exact generic cache destructor drains GPU before resident and delayed-ring owners\n";
}
