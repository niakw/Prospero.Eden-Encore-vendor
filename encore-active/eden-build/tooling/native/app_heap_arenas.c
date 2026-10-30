// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

/* Shared native-app allocator integration, originally used by the CTS runner. */
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
void *eden_heap_pages(size_t);
void eden_heap_pages_free(void *, size_t);
size_t eden_heap_pages_physical_size(void *);

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *address, size_t size);
void __real_free(void *address);
int __real_posix_memalign(void **address, size_t alignment, size_t size);
size_t __real_malloc_usable_size(const void *address);

void *sceLibcMspaceCreate(const char *name, void *base, size_t size,
                          unsigned flags);
void *sceLibcMspaceMalloc(void *mspace, size_t size);
void *sceLibcMspaceCalloc(void *mspace, size_t count, size_t size);
void *sceLibcMspaceRealloc(void *mspace, void *address, size_t size);
void sceLibcMspaceFree(void *mspace, void *address);
int sceLibcMspacePosixMemalign(void *mspace, void **address, size_t alignment,
                              size_t size);
size_t sceLibcMspaceMallocUsableSize(const void *address);

/* ponytail: fixed 3072 MiB, process-lifetime heap; revisit the budget only for
 * measured application needs. Do not unmap underneath late C++ destructors. */
#define PS5_OPENGL_HEAP_SIZE (3072u * 1024u * 1024u)

#ifdef EDEN_HEAP_GUARD
#include "heap_guard.h"
#endif
static atomic_int ps5_heap_state;
static void *ps5_heap_base;
static void *ps5_heap_mspace;
/* Owned-heap usable bytes only: not GPU mappings, foreign heaps or process RSS.
 * Relaxed snapshots are observations, not an allocator synchronization fence. */
static atomic_size_t ps5_heap_live_bytes, ps5_heap_peak_bytes, ps5_heap_blocks;
static atomic_size_t ps5_heap_failures, ps5_heap_ambiguous_zero_reallocs;

static void ps5_heap_resize_stats(size_t before, size_t after) {
  size_t live = after >= before
      ? atomic_fetch_add_explicit(&ps5_heap_live_bytes, after - before,
                                  memory_order_relaxed) + after - before
      : atomic_fetch_sub_explicit(&ps5_heap_live_bytes, before - after,
                                  memory_order_relaxed) - (before - after);
  size_t peak = atomic_load_explicit(&ps5_heap_peak_bytes, memory_order_relaxed);
  while (live > peak && !atomic_compare_exchange_weak_explicit(
      &ps5_heap_peak_bytes, &peak, live, memory_order_relaxed, memory_order_relaxed)) {}
}

static void *ps5_heap_record_allocation(void *address, int nonzero) {
  if (address) {
    ps5_heap_resize_stats(0, sceLibcMspaceMallocUsableSize(address));
    atomic_fetch_add_explicit(&ps5_heap_blocks, 1, memory_order_relaxed);
  } else if (nonzero) {
    atomic_fetch_add_explicit(&ps5_heap_failures, 1, memory_order_relaxed);
  }
  return address;
}

/* Eden PS5 port: a heap that takes its memory as it grows, with per-thread arenas.
 *
 * Memory. PS5 direct memory is real from the first byte, and the CPU shares one 12 GiB pool with
 * the GPU. The heap used to take its whole 3 GiB at start; the largest game measured used
 * 1.16 GiB of it, and the rest was graphics memory that game then ran out of. So the heap now
 * reserves its 3 GiB as address space only and commits it a piece (128 MiB) at a time, from the
 * start of the range, when an allocation finds no room. Every piece (or run of pieces, for one
 * request larger than a piece) is a native allocator arena of its own; an address tells its piece, and
 * the piece its mspace. Memory is not given back: a piece stays for the next game.
 *
 * Large blocks. A request of 32 MiB or more gets memory of its own, outside the range, and gives
 * it back when it is freed: a game file read whole, or a buffer that doubles as it grows, would
 * otherwise leave the heap larger for good and its pieces full of holes no other large block fits.
 *
 * Arenas. One shared allocator arena serialised every allocation in the process. Its lock collapses
 * under contention: a malloc/free pair costs 225 ns alone but 33/67/138 us with
 * 2/4/8 contending threads (EDEN_PERF_ALLOC),
 * which made allocation-heavy shader compilation ~100-250x slower than on a
 * Zen 2 host. A fixed split of the heap into arenas (run 220741) exhausted a
 * partition at level load, so capacity is not partitioned here: each allocating thread (up to
 * EDEN_HEAP_MAX_ARENAS) gets a lazily created 8 MiB arena allocated *from* the heap, aligned to
 * its own 8 MiB slot. Small requests use the thread's arena and fall back to the heap's own
 * spaces; frees and reallocs find the owner in O(1) from the address.
 */
#include <pthread.h>
#include <string.h>
#include <stdio.h>

#define EDEN_HEAP_ARENA_SHIFT 23u /* 8 MiB arenas and slots (<=256 MiB reserved) */
#define EDEN_HEAP_ARENA_SIZE (1u << EDEN_HEAP_ARENA_SHIFT)
/* Absolute 8 MiB slots covering the heap (+1: the base need not be slot aligned). */
#define EDEN_HEAP_SLOTS ((PS5_OPENGL_HEAP_SIZE >> EDEN_HEAP_ARENA_SHIFT) + 1u)
#define EDEN_HEAP_MAX_ARENAS 32u
/* Larger requests (and all over-aligned ones) always use the heap's own spaces. */
#define EDEN_HEAP_SMALL_REQUEST (256u * 1024u)
/* The heap grows by pieces of this size. */
#define EDEN_HEAP_PIECE_SHIFT 27u /* 128 MiB */
#define EDEN_HEAP_PIECE_SIZE ((size_t)1 << EDEN_HEAP_PIECE_SHIFT)
#define EDEN_HEAP_PIECES (PS5_OPENGL_HEAP_SIZE >> EDEN_HEAP_PIECE_SHIFT)
_Static_assert(EDEN_HEAP_PIECES <= 32, "heap root dirty bitmap needs <=32 pieces");

void *eden_heap_reserve(size_t size);             /* address space only; NULL when refused */
int eden_heap_commit(void *address, size_t size); /* initial backing, with startup owner */
int eden_heap_commit_growth(void *address, size_t size, int64_t *physical); /* growth lock held */
void eden_heap_rollback_growth(void *address, size_t size, int64_t physical); /* no mspace published */
void eden_heap_abandon_initial(void *address, size_t reserved);
int64_t eden_heap_first_direct_owner(void); /* only if first mspace create failed */

static void *_Atomic eden_heap_slots[EDEN_HEAP_SLOTS]; /* arena mspace owning each slot */
static void *eden_heap_arena_list[EDEN_HEAP_MAX_ARENAS];
static atomic_uint eden_heap_arena_count;
static atomic_uint eden_heap_next_shared;
static pthread_key_t eden_heap_arena_key;
static int eden_heap_arena_key_valid;
static atomic_flag eden_heap_arena_create_lock = ATOMIC_FLAG_INIT;
/* Who holds the creation lock and how many threads wait for it (read by the development stall
 * watchdog, headless/stall_watchdog.h). */
static _Atomic unsigned long eden_heap_create_holder;
static atomic_uint eden_heap_create_waiters;

/* The heap's own mspaces in the order they were made; each spans one or more pieces in a row. */
static void *eden_heap_spaces[EDEN_HEAP_PIECES];
static atomic_uint eden_heap_space_count;
/* The mspace each piece belongs to. */
static void *_Atomic eden_heap_piece_space[EDEN_HEAP_PIECES];
/* Each growth mspace owns one or more consecutive 128-MiB pieces. This
 * observational ledger NEVER authorizes reclaim: no allocator quiescence
 * protocol exists yet, and child arenas can pin the parent backing. */
static _Atomic unsigned char eden_heap_piece_root[EDEN_HEAP_PIECES];
static atomic_uchar eden_heap_root_span[EDEN_HEAP_PIECES];
static _Atomic int64_t eden_heap_root_direct_owner[EDEN_HEAP_PIECES];
static atomic_long eden_heap_root_held_bytes[EDEN_HEAP_PIECES];
static atomic_long eden_heap_root_held_blocks[EDEN_HEAP_PIECES];
static atomic_uint eden_heap_root_arena_pins[EDEN_HEAP_PIECES];
/* Pieces committed so far: [0, used). All of them when the heap could not be reserved to grow. */
static atomic_uint eden_heap_pieces_used;
static int eden_heap_grows;
static atomic_uint eden_heap_space_hint; /* where the last allocation from the spaces succeeded */
static atomic_flag eden_heap_grow_lock = ATOMIC_FLAG_INIT;
/* The growth lock owns this flag. A failed mspace construction now rolls
 * its just-mapped physical pages back to a PROT_NONE reservation, but retrying
 * that rare failure on each malloc could repeatedly commit+zero 128 MiB on a
 * real-time guest thread. Reject subsequent growth while existing spaces work. */
static int eden_heap_growth_mspace_failed;

/* Requests of this size or more get memory of their own (eden_heap_pages), freed with them. */
#define EDEN_HEAP_LARGE_REQUEST ((size_t)32 << 20)
#define EDEN_HEAP_LARGE_BLOCKS 256u
/* The large blocks alive: address 0 marks a free entry, 1 one being filled in. */
static struct {
  _Atomic uintptr_t address;
  size_t size; /* requested payload */
  size_t physical_size; /* native direct bytes incl. leading page and rounding */
} eden_heap_large[EDEN_HEAP_LARGE_BLOCKS];
static atomic_uint eden_heap_large_count;
static atomic_size_t eden_heap_large_bytes;
static atomic_size_t eden_heap_large_physical_bytes;
/* Every foreign/real-libc free or realloc would scan 256 entries while ANY
 * large direct-owned block lived. This monotonic Bloom filter rules out the
 * overwhelmingly common negative case using only two atomic bit checks.
 * Never clear a published bit: stale positives are harmless, false negatives
 * would misroute a direct-owned block to libc and corrupt physical ownership.
 * Four KiB for the process lifetime, allocation-free on every path. */
#define EDEN_HEAP_LARGE_FILTER_WORDS 512u
#define EDEN_HEAP_LARGE_FILTER_MASK (EDEN_HEAP_LARGE_FILTER_WORDS * 64u - 1u)
static _Atomic uint64_t eden_heap_large_filter[EDEN_HEAP_LARGE_FILTER_WORDS];

static uint64_t eden_heap_large_hash(uintptr_t address) {
  /* Data pointers are page-aligned or higher: hash away alignment zeros. */
  uint64_t value = (uint64_t)address >> 12;
  value ^= value >> 33;
  value *= UINT64_C(0xff51afd7ed558ccd);
  value ^= value >> 33;
  value *= UINT64_C(0xc4ceb9fe1a85ec53);
  return value ^ (value >> 33);
}

static void eden_heap_large_note(uintptr_t address) {
  const uint64_t hash = eden_heap_large_hash(address);
  const unsigned first = (unsigned)hash & EDEN_HEAP_LARGE_FILTER_MASK;
  const unsigned second = (unsigned)(hash >> 32) & EDEN_HEAP_LARGE_FILTER_MASK;
  atomic_fetch_or_explicit(&eden_heap_large_filter[first >> 6],
                           UINT64_C(1) << (first & 63), memory_order_relaxed);
  atomic_fetch_or_explicit(&eden_heap_large_filter[second >> 6],
                           UINT64_C(1) << (second & 63), memory_order_relaxed);
}

static int eden_heap_large_maybe(uintptr_t address) {
  const uint64_t hash = eden_heap_large_hash(address);
  const unsigned first = (unsigned)hash & EDEN_HEAP_LARGE_FILTER_MASK;
  const unsigned second = (unsigned)(hash >> 32) & EDEN_HEAP_LARGE_FILTER_MASK;
  return (atomic_load_explicit(&eden_heap_large_filter[first >> 6], memory_order_relaxed) &
          (UINT64_C(1) << (first & 63))) != 0 &&
         (atomic_load_explicit(&eden_heap_large_filter[second >> 6], memory_order_relaxed) &
          (UINT64_C(1) << (second & 63))) != 0;
}

int sceKernelUsleep(unsigned int microseconds);

/* Arena creation is rare (once per thread) but runs under a spin flag, and its holder also takes
 * an mspace lock. The emulated cores and the GPU thread run at real-time priority pinned to
 * one CPU each; spinning there forever starves a preempted lower-priority holder queued on that
 * CPU, which hung game starts. After a short spin, sleep so the holder can run. */
static void eden_heap_create_acquire(void) {
  if (atomic_flag_test_and_set_explicit(&eden_heap_arena_create_lock, memory_order_acquire)) {
    atomic_fetch_add_explicit(&eden_heap_create_waiters, 1, memory_order_relaxed);
    for (unsigned spins = 0;
         atomic_flag_test_and_set_explicit(&eden_heap_arena_create_lock, memory_order_acquire); ++spins) {
      if (spins < 64)
        __builtin_ia32_pause();
      else
        sceKernelUsleep(50);
    }
    atomic_fetch_sub_explicit(&eden_heap_create_waiters, 1, memory_order_relaxed);
  }
  atomic_store_explicit(&eden_heap_create_holder, (unsigned long)pthread_self(), memory_order_relaxed);
}

static void eden_heap_create_release(void) {
  atomic_store_explicit(&eden_heap_create_holder, 0, memory_order_relaxed);
  atomic_flag_clear_explicit(&eden_heap_arena_create_lock, memory_order_release);
}

/* Development stall reports: the creation lock's holder (0: free) and its waiters. */
unsigned long eden_heap_create_lock_state(unsigned *waiters) {
  if (waiters)
    *waiters = atomic_load_explicit(&eden_heap_create_waiters, memory_order_relaxed);
  return atomic_load_explicit(&eden_heap_create_holder, memory_order_relaxed);
}

static void *eden_heap_owner(const void *address);
struct eden_heap_thread; /* forward declaration for TLS cache destructor */
static void eden_heap_physical_note(struct eden_heap_thread *self,
                                    const void *address, long bytes, long blocks);

/* Per-thread allocation statistics. The live/peak/block counters are process-wide atomics, and
 * updating them on every malloc and free made all allocating threads bounce the same cache
 * lines: EDEN_PERF_ALLOC measured 210 ns per malloc/free pair alone and 874/1590 ns with 4/8
 * threads, while guest cores compile JIT blocks (allocation-heavy) together during loads. Each
 * thread now accumulates its changes and applies them every 64 operations, beyond 1 MiB, and at
 * thread exit; snapshots lag by that much. The record also keeps the thread's arena, and a
 * record freed by an exiting thread is reused with its arena by the next thread. */
/* Freed small blocks are kept per thread and reused without calling into the mspace, whose
 * calls serialise across threads even with separate arenas (EDEN_PERF_ALLOC with per-thread
 * arenas and statistics: 232 ns per pair alone, 803/1423 ns with 4/8 threads). A bin holds blocks
 * whose usable size is in [16 * bin, 16 * bin + 15], linked through their first word, with the
 * usable size in the second; a request of size s takes from bin ceil(s / 16). Bins are bounded,
 * and an exiting thread returns its blocks to their mspaces. */
#define EDEN_TCACHE_BINS 128u
#define EDEN_TCACHE_LIMIT 64u
/* One thread used to retain up to ~8 MiB across its 127 small-object bins,
 * even after every allocation was freed. Keep 1 MiB of fast warm reuse per
 * thread, and return the rest to its owning mspace for other threads/titles.
 * This does NOT physically unmap the heap's 128 MiB backing pieces. */
#define EDEN_TCACHE_TOTAL_LIMIT ((size_t)1 << 20)
struct eden_heap_thread {
  void *arena;
  long live;
  long blocks;
  unsigned pending;
  uint32_t root_dirty; /* only roots touched since last counter flush */
  long physical_bytes[EDEN_HEAP_PIECES];
  long physical_blocks[EDEN_HEAP_PIECES];
  atomic_size_t cached_bytes; /* only the owning thread writes; snapshots may read */
  void *bins[EDEN_TCACHE_BINS];
  unsigned char counts[EDEN_TCACHE_BINS];
};
#define EDEN_HEAP_THREADS 512u
static struct eden_heap_thread eden_heap_threads[EDEN_HEAP_THREADS];
static atomic_uchar eden_heap_thread_used[EDEN_HEAP_THREADS];

static void eden_heap_apply(long bytes, long blocks) {
  if (blocks)
    atomic_fetch_add_explicit(&ps5_heap_blocks, (size_t)blocks, memory_order_relaxed);
  if (bytes >= 0)
    ps5_heap_resize_stats(0, (size_t)bytes);
  else
    ps5_heap_resize_stats((size_t)-bytes, 0);
}

static void eden_heap_flush(struct eden_heap_thread *self) {
  eden_heap_apply(self->live, self->blocks);
  /* No per-frame work. Walk only the roots actually touched by this
   * thread's last 64 allocator operations, rather than all 24 heap roots. */
  uint32_t dirty=self->root_dirty;
  self->root_dirty=0;
  while (dirty) {
    const unsigned root=(unsigned)__builtin_ctz(dirty);
    dirty &= dirty-1;
    if (self->physical_bytes[root]) {
      atomic_fetch_add_explicit(&eden_heap_root_held_bytes[root], self->physical_bytes[root], memory_order_relaxed);
      self->physical_bytes[root]=0;
    }
    if (self->physical_blocks[root]) {
      atomic_fetch_add_explicit(&eden_heap_root_held_blocks[root], self->physical_blocks[root], memory_order_relaxed);
      self->physical_blocks[root]=0;
    }
  }
  self->live = 0;
  self->blocks = 0;
  self->pending = 0;
}

/* Owner-thread only. Cache entries are ALREADY freed to the application;
 * drain makes their space reusable by other guest/game threads without
 * releasing any live allocation, mspace or direct physical backing. */
static size_t eden_heap_drain_thread_cache(struct eden_heap_thread *self) {
  const size_t drained = atomic_load_explicit(&self->cached_bytes, memory_order_relaxed);
  for (unsigned bin = 0; bin < EDEN_TCACHE_BINS; ++bin) {
    while (self->bins[bin] != NULL) {
      void *block = self->bins[bin];
      self->bins[bin] = *(void **)block;
      const size_t usable=((size_t *)block)[1];
      sceLibcMspaceFree(eden_heap_owner(block), block);
      eden_heap_physical_note(self, block, -(long)usable, -1);
    }
    self->counts[bin] = 0;
  }
  atomic_store_explicit(&self->cached_bytes, 0, memory_order_relaxed);
  return drained;
}

static void eden_heap_thread_exit(void *value) {
  struct eden_heap_thread *self = value;
  (void)eden_heap_drain_thread_cache(self);
  eden_heap_flush(self);
  atomic_store_explicit(&eden_heap_thread_used[self - eden_heap_threads], 0, memory_order_release);
}

/* Called only by the surviving runner/launcher thread after guest teardown.
 * Workers drain through their TLS destructor; never inspect their private
 * free-lists from another thread, and never unmap their backing. */
size_t eden_heap_release_current_tcache(void) {
  size_t drained = 0;
  /* There may be no TLS cache on the surviving launcher thread even when
   * short-lived guest workers have used later heap roots. Always bias the
   * next title toward reusing already-committed earlier mspaces. */
  if (eden_heap_arena_key_valid) {
    struct eden_heap_thread *self = pthread_getspecific(eden_heap_arena_key);
    if (self) {
      drained = eden_heap_drain_thread_cache(self);
      eden_heap_flush(self);
    }
  }
  /* A hint reset is metadata maintenance, never a release or compaction.
   * Allocation threads are allowed to race this relaxed preference only. */
  atomic_store_explicit(&eden_heap_space_hint, 0, memory_order_relaxed);
  return drained;
}

unsigned eden_heap_arenas_created(void) {
  return atomic_load_explicit(&eden_heap_arena_count, memory_order_relaxed);
}

/* Reporting only: a relaxed sum of private thread caches, never an exact
 * allocator quiescence guarantee and never used to decide whether to unmap. */
size_t eden_heap_tcache_held(void) {
  size_t total = 0;
  for (unsigned i = 0; i < EDEN_HEAP_THREADS; ++i)
    total += atomic_load_explicit(&eden_heap_threads[i].cached_bytes, memory_order_relaxed);
  return total;
}

/* Memory the heap's pieces hold now: this part only grows. */
size_t eden_heap_committed(void) {
  return (size_t)atomic_load_explicit(&eden_heap_pieces_used, memory_order_relaxed) << EDEN_HEAP_PIECE_SHIFT;
}
/* Physical mspace root retention has two different blockers:
 * - child 8-MiB mspace arenas pin their parent's direct backing;
 * - even unpinned roots have no proven allocator quiescence / destruction ABI.
 * This observational summary NEVER grants permission to unmap any root. */
struct eden_heap_retention {
  size_t pinned_bytes;
  size_t unpinned_bytes;
  unsigned pinned_roots;
  unsigned unpinned_roots;
};
static struct eden_heap_retention eden_heap_retention_snapshot(void) {
  struct eden_heap_retention stats = {0};
  const unsigned used=atomic_load_explicit(&eden_heap_pieces_used,memory_order_acquire);
  for (unsigned root=0; root<used; ++root) {
    const unsigned span=atomic_load_explicit(&eden_heap_root_span[root],memory_order_acquire);
    if (!span) continue;
    /* Committed physical ownership exists even if held counters read zero.
     * Overflow is impossible: at most EDEN_HEAP_PIECES * 128 MiB. */
    const size_t bytes=(size_t)span << EDEN_HEAP_PIECE_SHIFT;
    if (atomic_load_explicit(&eden_heap_root_arena_pins[root],memory_order_relaxed)) {
      stats.pinned_bytes += bytes;
      ++stats.pinned_roots;
    } else {
      stats.unpinned_bytes += bytes;
      ++stats.unpinned_roots;
    }
  }
  return stats;
}
/* The launcher calls this after guest workers join and its own TLS drain.
 * Non-joined threads can still have pending deltas: values are diagnostic
 * bounds, NOT exact, synchronized proof that a space is empty. */
void eden_heap_report_roots(const char *phase) {
  const unsigned used=atomic_load_explicit(&eden_heap_pieces_used,memory_order_acquire);
  for (unsigned root=0; root<used; ++root) {
    const unsigned span=atomic_load_explicit(&eden_heap_root_span[root],memory_order_acquire);
    if (!span) continue;
    const long bytes=atomic_load_explicit(&eden_heap_root_held_bytes[root],memory_order_relaxed);
    const long blocks=atomic_load_explicit(&eden_heap_root_held_blocks[root],memory_order_relaxed);
    const unsigned pins=atomic_load_explicit(&eden_heap_root_arena_pins[root],memory_order_relaxed);
    const unsigned has_pa=atomic_load_explicit(&eden_heap_root_direct_owner[root],memory_order_relaxed)>=0;
    printf("EDEN_HEAP_ROOT phase=%s root=%u pieces=%u held_bytes=%ld held_blocks=%ld arena_pins=%u physical_owner=%u reclaim=disabled\n",
           phase?phase:"unknown", root, span, bytes, blocks, pins, has_pa);
  }
  const struct eden_heap_retention retention=eden_heap_retention_snapshot();
  printf("EDEN_HEAP_RETENTION phase=%s pinned_bytes=%zu unpinned_bytes=%zu pinned_roots=%u unpinned_roots=%u reclaim=disabled quiescence=unproven\n",
         phase?phase:"unknown", retention.pinned_bytes, retention.unpinned_bytes,
         retention.pinned_roots, retention.unpinned_roots);
}

/* Memory of the large blocks alive now, and how many there are. */
size_t eden_heap_large_held(unsigned *blocks) {
  if (blocks)
    *blocks = atomic_load_explicit(&eden_heap_large_count, memory_order_relaxed);
  return atomic_load_explicit(&eden_heap_large_bytes, memory_order_relaxed);
}
/* Reporting only, NOT permission to reclaim underlying native allocations. */
size_t eden_heap_large_physical_held(void) {
  return atomic_load_explicit(&eden_heap_large_physical_bytes, memory_order_relaxed);
}

/* The entry of a large block, or NULL: most addresses are not one, and with none alive
 * nothing is searched. */
static int eden_heap_large_find(const void *address) {
  if (address == NULL ||
      atomic_load_explicit(&eden_heap_large_count, memory_order_acquire) == 0 ||
      !eden_heap_large_maybe((uintptr_t)address))
    return -1;
  for (unsigned i = 0; i < EDEN_HEAP_LARGE_BLOCKS; ++i)
    if (atomic_load_explicit(&eden_heap_large[i].address, memory_order_acquire) == (uintptr_t)address)
      return (int)i;
  return -1;
}

/* A large block of its own (zeroed), or NULL: no entry free, or no memory. */
static void *eden_heap_large_allocate(size_t bytes) {
  for (unsigned i = 0; i < EDEN_HEAP_LARGE_BLOCKS; ++i) {
    uintptr_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&eden_heap_large[i].address, &expected, 1, memory_order_acquire,
                                                 memory_order_relaxed))
      continue;
    void *block = eden_heap_pages(bytes);
    if (block == NULL) {
      atomic_store_explicit(&eden_heap_large[i].address, 0, memory_order_release);
      return NULL;
    }
    const size_t physical_bytes = eden_heap_pages_physical_size(block);
    if (physical_bytes < bytes) abort();
    eden_heap_large[i].size = bytes;
    eden_heap_large[i].physical_size = physical_bytes;
    /* Publish both bits BEFORE the count/address become visible to any
     * receiving thread. A miss may return only for never-owned pointers. */
    eden_heap_large_note((uintptr_t)block);
    atomic_fetch_add_explicit(&eden_heap_large_bytes, bytes, memory_order_relaxed);
    atomic_fetch_add_explicit(&eden_heap_large_physical_bytes, physical_bytes, memory_order_relaxed);
    atomic_fetch_add_explicit(&eden_heap_large_count, 1, memory_order_release);
    atomic_store_explicit(&eden_heap_large[i].address, (uintptr_t)block, memory_order_release);
    return block;
  }
  return NULL;
}

static void eden_heap_large_free(int entry) {
  void *block = (void *)atomic_load_explicit(&eden_heap_large[entry].address, memory_order_relaxed);
  const size_t bytes = eden_heap_large[entry].size;
  const size_t physical_bytes = eden_heap_large[entry].physical_size;
  /* The entry stops naming the address before the memory goes: the next large block may be
   * given the same address at once. */
  atomic_store_explicit(&eden_heap_large[entry].address, 1, memory_order_release);
  eden_heap_pages_free(block, bytes);
  atomic_fetch_sub_explicit(&eden_heap_large_bytes, bytes, memory_order_relaxed);
  const size_t old_physical = atomic_fetch_sub_explicit(&eden_heap_large_physical_bytes,
                                                        physical_bytes, memory_order_relaxed);
  if (old_physical < physical_bytes) abort();
  atomic_fetch_sub_explicit(&eden_heap_large_count, 1, memory_order_release);
  atomic_store_explicit(&eden_heap_large[entry].address, 0, memory_order_release);
}

static int ps5_heap_ready(void) {
  int state = atomic_load_explicit(&ps5_heap_state, memory_order_acquire);
  if (state == 2)
    return 1;
  if (state != 0)
    return 0;

  int expected = 0;
  if (!atomic_compare_exchange_strong_explicit(
          &ps5_heap_state, &expected, 1, memory_order_acq_rel,
          memory_order_acquire))
    return expected == 2;

  /* Address space for the whole heap and memory for its first piece; where a range cannot be
   * reserved, the whole heap at once as before. */
  void *base = eden_heap_reserve(PS5_OPENGL_HEAP_SIZE);
  size_t first_size = EDEN_HEAP_PIECE_SIZE;
  unsigned first_pieces = 1;
  if (base != NULL) {
    if (eden_heap_commit(base, EDEN_HEAP_PIECE_SIZE) == 0) {
      eden_heap_grows = 1;
    } else {
      /* A 3 GiB virtual reservation succeeded, but its first 128 MiB
       * physical allocation failed. Retrying a dense 3 GiB block cannot
       * recover from that OOM and used to strand the reserved VA. */
      if (munmap(base, PS5_OPENGL_HEAP_SIZE) != 0) __builtin_trap();
      atomic_store_explicit(&ps5_heap_state, -1, memory_order_release);
      return 0; /* existing libc fallback */
    }
  } else {
    /* Only missing VA reservation permits the legacy dense fallback. */
    base = eden_heap_pages(PS5_OPENGL_HEAP_SIZE);
    first_size = PS5_OPENGL_HEAP_SIZE;
    first_pieces = EDEN_HEAP_PIECES;
  }
  if (base == NULL) {
    atomic_store_explicit(&ps5_heap_state, -1, memory_order_release);
    return 0;
  }

  ps5_heap_base = base;
  ps5_heap_mspace = sceLibcMspaceCreate("Eden-headless", base, first_size, 0);
  if (ps5_heap_mspace == NULL) {
    ps5_heap_base = NULL;
    if (eden_heap_grows) {
      /* No live allocations exist yet. Previously this error leaked the
       * 128 MiB direct backing and all 3 GiB reserved VA for app lifetime. */
      eden_heap_abandon_initial(base, PS5_OPENGL_HEAP_SIZE);
    } else {
      eden_heap_pages_free(base, PS5_OPENGL_HEAP_SIZE);
    }
    atomic_store_explicit(&ps5_heap_state, -1, memory_order_release);
    return 0;
  }
  eden_heap_spaces[0] = ps5_heap_mspace;
  atomic_store_explicit(&eden_heap_root_direct_owner[0],
                        eden_heap_grows ? eden_heap_first_direct_owner() : -1,memory_order_relaxed);
  for (unsigned piece=0; piece<first_pieces; ++piece) {
    atomic_store_explicit(&eden_heap_piece_root[piece],0,memory_order_relaxed);
    atomic_store_explicit(&eden_heap_piece_space[piece],ps5_heap_mspace,memory_order_relaxed);
  }
  /* Publish the root only after the entire piece→root and owner table. */
  atomic_store_explicit(&eden_heap_root_span[0],(unsigned char)first_pieces,memory_order_release);
  atomic_store_explicit(&eden_heap_pieces_used, first_pieces, memory_order_relaxed);
  atomic_store_explicit(&eden_heap_space_count, 1, memory_order_release);
  /* libthr keeps specific data in its own storage; no recursion into malloc. */
  eden_heap_arena_key_valid = pthread_key_create(&eden_heap_arena_key, eden_heap_thread_exit) == 0;

  atomic_store_explicit(&ps5_heap_state, 2, memory_order_release);
  return 1;
}

/* A block inside the heap's range: an arena's or a piece's. */
static int eden_heap_in_range(const void *address) {
  /* Acquire publication before reading non-atomic heap metadata. */
  if (atomic_load_explicit(&ps5_heap_state, memory_order_acquire) != 2)
    return 0;
  uintptr_t value = (uintptr_t)address;
  uintptr_t base = (uintptr_t)ps5_heap_base;
  return value >= base && value - base < PS5_OPENGL_HEAP_SIZE;
}

/* Any block of this heap: one in its range, or a large block with memory of its own. */
static int ps5_heap_owns(const void *address) {
  return eden_heap_in_range(address) || eden_heap_large_find(address) >= 0;
}

/* The usable size of a block this heap owns. */
static size_t eden_heap_usable(const void *address) {
  if (eden_heap_in_range(address))
    return sceLibcMspaceMallocUsableSize(address);
  const int entry = eden_heap_large_find(address);
  return entry >= 0 ? eden_heap_large[entry].size : 0;
}

static uintptr_t eden_heap_slot(const void *address) {
  return ((uintptr_t)address >> EDEN_HEAP_ARENA_SHIFT) -
         ((uintptr_t)ps5_heap_base >> EDEN_HEAP_ARENA_SHIFT);
}

/* The mspace owning an owned block: its slot's arena, else the space of its piece. */
static void *eden_heap_owner(const void *address) {
  const uintptr_t slot = eden_heap_slot(address);
  void *arena = atomic_load_explicit(&eden_heap_slots[slot], memory_order_acquire);
  if (arena != NULL)
    return arena;
  const uintptr_t piece = ((uintptr_t)address - (uintptr_t)ps5_heap_base) >> EDEN_HEAP_PIECE_SHIFT;
  return atomic_load_explicit(&eden_heap_piece_space[piece], memory_order_acquire);
}

/* ---- the heap's own spaces ---- */

enum { EDEN_HEAP_MALLOC, EDEN_HEAP_CALLOC, EDEN_HEAP_ALIGNED };

/* One request to one mspace: malloc(a), calloc(a, b) or an a-aligned block of b bytes. */
static void *eden_heap_space_try(void *space, int kind, size_t a, size_t b) {
  if (kind == EDEN_HEAP_MALLOC)
    return sceLibcMspaceMalloc(space, a);
  if (kind == EDEN_HEAP_CALLOC)
    return sceLibcMspaceCalloc(space, a, b);
  void *address = NULL;
  return sceLibcMspacePosixMemalign(space, &address, a, b) == 0 ? address : NULL;
}

int sceKernelDebugOutText(int channel, const char *text);

/* Why the heap could not grow, for the kernel log: the allocation that asked is about to fail.
 * No stdio here (it allocates), and only the first few times. */
static void eden_heap_grow_refused(const char *line) {
  static atomic_uint reported;
  if (atomic_fetch_add_explicit(&reported, 1, memory_order_relaxed) < 8)
    sceKernelDebugOutText(0, line);
}

/* Growth runs while the allocator's own growth lock is held. Do not
 * invoke snprintf/stdio or another potentially malloc-using formatter here.
 * This bounded decimal encoder uses only stack bytes, no allocation or
 * locking, and rejects truncation instead of emitting misleading telemetry. */
static int eden_heap_append_decimal(char *out, size_t capacity, size_t *length,
                                    const char *prefix, size_t number) {
  /* Reject a zero-capacity or invalid cursor before capacity - 1 arithmetic.
   * All production call sites use a local 176-byte buffer, but the helper
   * itself must be safe under isolated edge-case tests. */
  if (!out || !length || !prefix || !capacity || *length >= capacity) return 0;
  while (*prefix) {
    if (*length >= capacity - 1) return 0;
    out[(*length)++] = *prefix++;
  }
  char reversed[3 * sizeof(size_t) + 1];
  unsigned digits = 0;
  do {
    reversed[digits++] = (char)('0' + (number % 10));
    number /= 10;
  } while (number != 0);
  if (digits >= capacity - *length) return 0;
  while (digits)
    out[(*length)++] = reversed[--digits];
  out[*length] = '\0';
  return 1;
}

static void eden_heap_log_growth(size_t request, unsigned root, size_t pieces,
                                 unsigned spaces) {
  char line[176];
  size_t length = 0;
  const size_t committed_mib = ((size_t)root + pieces) << (EDEN_HEAP_PIECE_SHIFT - 20u);
  if (!eden_heap_append_decimal(line, sizeof(line), &length,
                                "EDEN_HEAP_GROW req_bytes=", request) ||
      !eden_heap_append_decimal(line, sizeof(line), &length, " root=", root) ||
      !eden_heap_append_decimal(line, sizeof(line), &length, " pieces=", pieces) ||
      !eden_heap_append_decimal(line, sizeof(line), &length, " spaces=", spaces) ||
      !eden_heap_append_decimal(line, sizeof(line), &length, " committed_mib=", committed_mib))
    return;
  if (length + 1 >= sizeof(line)) return;
  line[length++] = '\n';
  line[length] = '\0';
  sceKernelDebugOutText(0, line);
}

/* Adds a space with room for `bytes`: memory for the next pieces of the range and an mspace
 * over them. `seen` is how many spaces the caller tried: when another thread added one
 * meanwhile, that one is tried first. Returns 0 when the heap is at its limit or the console
 * has no memory left. */
static int eden_heap_grow(size_t bytes, unsigned seen) {
  for (unsigned spins = 0; atomic_flag_test_and_set_explicit(&eden_heap_grow_lock, memory_order_acquire); ++spins) {
    if (spins < 64)
      __builtin_ia32_pause();
    else
      sceKernelUsleep(50);
  }
  int grown = 0;
  /* Capture immutable ownership metadata under the growth lock. Emit the
   * kernel record only AFTER unlocking: a slow system logger must never
   * extend the allocator's serialized 128-MiB commit critical section. */
  const char *report_refusal = NULL;
  size_t report_request = 0, report_pieces = 0;
  unsigned report_root = 0, report_spaces = 0;
  const unsigned count = atomic_load_explicit(&eden_heap_space_count, memory_order_relaxed);
  const unsigned used = atomic_load_explicit(&eden_heap_pieces_used, memory_order_relaxed);
  /* Room for the request and for the mspace's own records. */
  const size_t overhead = (size_t)1 << 20;
  /* Both additions can overflow independently; an overflowing rounded piece
   * count used to become zero and enter the new-mspace path with size zero. */
  const size_t need = bytes <= (size_t)-1 - overhead ? bytes + overhead : 0;
  const size_t pieces = need && need <= (size_t)-1 - (EDEN_HEAP_PIECE_SIZE - 1)
      ? (need + EDEN_HEAP_PIECE_SIZE - 1) >> EDEN_HEAP_PIECE_SHIFT : 0;
  if (count != seen) {
    grown = 1;
  } else if (eden_heap_growth_mspace_failed) {
    /* Previous growth pages were already rolled back. Avoid stalling a guest
     * frame with repeated 128 MiB commit+zero+rollback attempts. */
    report_refusal = ("EDEN_HEAP_GROW refused: earlier mspace creation failed; growth disabled\n");
  } else if (eden_heap_grows && pieces && used <= EDEN_HEAP_PIECES &&
             pieces <= EDEN_HEAP_PIECES - used) {
    void *start = (char *)ps5_heap_base + ((size_t)used << EDEN_HEAP_PIECE_SHIFT);
    const size_t size = pieces << EDEN_HEAP_PIECE_SHIFT;
    int64_t physical = -1;
    if (eden_heap_commit_growth(start, size, &physical) == 0) {
      /* Only this growth lock sees the new pages. Do NOT publish the piece
       * count or any owner until native arena construction has succeeded. */
      void *space = sceLibcMspaceCreate("Eden-heap", start, size, 0);
      if (space != NULL) {
        atomic_store_explicit(&eden_heap_root_direct_owner[used],physical,memory_order_relaxed);
        for (size_t piece=0; piece<pieces; ++piece) {
          atomic_store_explicit(&eden_heap_piece_root[used+piece],(unsigned char)used,memory_order_relaxed);
          atomic_store_explicit(&eden_heap_piece_space[used+piece],space,memory_order_release);
        }
        eden_heap_spaces[count] = space;
        /* A diagnostic reader seeing the span may now access initialized
         * piece ownership data, even while growth remains in flight. */
        atomic_store_explicit(&eden_heap_root_span[used],(unsigned char)pieces,memory_order_release);
        atomic_store_explicit(&eden_heap_pieces_used, used + (unsigned)pieces, memory_order_release);
        atomic_store_explicit(&eden_heap_space_count, count + 1, memory_order_release);
        report_request = bytes;
        report_root = used;
        report_pieces = pieces;
        report_spaces = count + 1;
        grown = 1;
      } else {
        /* No block from the new piece was ever issued, and no slot/space
         * refers to it. Restore a PROT_NONE virtual guard BEFORE returning
         * its exact direct physical owner to the kernel. */
        eden_heap_rollback_growth(start, size, physical);
        /* Fail closed on future new-piece attempts, not on existing spaces.
         * Prevent repeated expensive zeroing if mspace creation is broken. */
        eden_heap_growth_mspace_failed = 1;
        report_refusal = ("EDEN_HEAP_GROW refused: mspace failed; physical piece rolled back; growth disabled\n");
      }
    } else {
      report_refusal = ("EDEN_HEAP_GROW refused: the console has no memory for a new piece\n");
    }
  } else {
    report_refusal = ("EDEN_HEAP_GROW refused: the heap is at its limit\n");
  }
  atomic_flag_clear_explicit(&eden_heap_grow_lock, memory_order_release);
  /* May be reordered against another thread's later growth record. The
   * logged root/extent/count are captured under the lock, not re-read. */
  if (report_pieces)
    eden_heap_log_growth(report_request, report_root, report_pieces, report_spaces);
  if (report_refusal)
    eden_heap_grow_refused(report_refusal);
  return grown;
}

/* A block from the heap's spaces, then from a new one. A small request starts at the space that
 * served the last one (it is asked for millions of times: compiled-code records); a request of
 * 1 MiB or more starts at the first space, which keeps the later ones free of long-lived blocks
 * and able to take the next large request. */
static void *eden_heap_main(int kind, size_t a, size_t b) {
  /* Caller normally validates calloc, but the shared mspace helper must
   * never calculate a wrapped request (especially aligned a + b). */
  if (kind == EDEN_HEAP_CALLOC && b && a > (size_t)-1 / b) return NULL;
  if (kind == EDEN_HEAP_ALIGNED && a > (size_t)-1 - b) return NULL;
  const size_t bytes = kind == EDEN_HEAP_MALLOC ? a : kind == EDEN_HEAP_CALLOC ? a * b : a + b;
  for (;;) {
    const unsigned count = atomic_load_explicit(&eden_heap_space_count, memory_order_acquire);
    const int small = bytes < ((size_t)1 << 20);
    const unsigned hint = small ? atomic_load_explicit(&eden_heap_space_hint, memory_order_relaxed) : 0;
    for (unsigned i = 0; i < count; ++i) {
      const unsigned index = (hint + i) % count;
      void *address = eden_heap_space_try(eden_heap_spaces[index], kind, a, b);
      if (address != NULL) {
        if (small && i != 0)
          atomic_store_explicit(&eden_heap_space_hint, index, memory_order_relaxed);
        return address;
      }
    }
    if (bytes == 0 || !eden_heap_grow(bytes, count))
      return NULL;
  }
}

/* Allocate and register one arena block from the heap (NULL on failure). */
static void *eden_heap_create_arena(void) {
  eden_heap_create_acquire();
  void *arena = NULL;
  const unsigned count = atomic_load_explicit(&eden_heap_arena_count, memory_order_relaxed);
  if (count < EDEN_HEAP_MAX_ARENAS) {
    void *block = eden_heap_main(EDEN_HEAP_ALIGNED, EDEN_HEAP_ARENA_SIZE, EDEN_HEAP_ARENA_SIZE);
    if (block != NULL) {
      arena = sceLibcMspaceCreate("Eden-arena", block, EDEN_HEAP_ARENA_SIZE, 0);
      if (arena != NULL) {
        const uintptr_t slot = eden_heap_slot(block);
        eden_heap_arena_list[count] = arena;
        const unsigned piece=(unsigned)(((uintptr_t)block-(uintptr_t)ps5_heap_base)>>EDEN_HEAP_PIECE_SHIFT);
        const unsigned root=atomic_load_explicit(&eden_heap_piece_root[piece],memory_order_relaxed);
        atomic_fetch_add_explicit(&eden_heap_root_arena_pins[root],1,memory_order_relaxed);
        atomic_store_explicit(&eden_heap_slots[slot], arena, memory_order_release);
        atomic_store_explicit(&eden_heap_arena_count, count + 1, memory_order_release);
      } else {
        sceLibcMspaceFree(eden_heap_owner(block), block);
      }
    }
  }
  eden_heap_create_release();
  return arena;
}

/* This thread's record, claimed on its first allocation. NULL: none free (process-wide counters). */
static struct eden_heap_thread *eden_heap_self(void) {
  if (!eden_heap_arena_key_valid)
    return NULL;
  struct eden_heap_thread *self = pthread_getspecific(eden_heap_arena_key);
  if (self != NULL)
    return self;
  for (unsigned i = 0; i < EDEN_HEAP_THREADS; ++i) {
    unsigned char expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&eden_heap_thread_used[i], &expected, 1,
                                                 memory_order_acquire, memory_order_relaxed))
      continue;
    self = &eden_heap_threads[i];
    self->live = 0;
    self->blocks = 0;
    self->pending = 0;
    self->root_dirty = 0;
    memset(self->physical_bytes,0,sizeof(self->physical_bytes));
    memset(self->physical_blocks,0,sizeof(self->physical_blocks));
    atomic_store_explicit(&self->cached_bytes, 0, memory_order_relaxed);
    if (pthread_setspecific(eden_heap_arena_key, self) != 0) {
      atomic_store_explicit(&eden_heap_thread_used[i], 0, memory_order_release);
      return NULL;
    }
    return self;
  }
  return NULL;
}

/* Charge a block to its physical root, including blocks inside child arenas.
 * Explicit self is essential when draining in a TLS key destructor. */
static void eden_heap_physical_note(struct eden_heap_thread *self,
                                    const void *address, long bytes, long blocks) {
  if (!address || (!bytes && !blocks)) return;
  const unsigned piece=(unsigned)(((uintptr_t)address-(uintptr_t)ps5_heap_base)>>EDEN_HEAP_PIECE_SHIFT);
  if (piece>=EDEN_HEAP_PIECES) __builtin_trap();
  const unsigned root=atomic_load_explicit(&eden_heap_piece_root[piece],memory_order_relaxed);
  /* The caller has already looked up its TLS record, or deliberately
   * provided NULL when slots are exhausted. Never retry TLS registration. */
  if (self) {
    self->physical_bytes[root]+=bytes;
    self->physical_blocks[root]+=blocks;
    self->root_dirty |= UINT32_C(1) << root;
  } else {
    if (bytes) atomic_fetch_add_explicit(&eden_heap_root_held_bytes[root],bytes,memory_order_relaxed);
    if (blocks) atomic_fetch_add_explicit(&eden_heap_root_held_blocks[root],blocks,memory_order_relaxed);
  }
}
static void eden_heap_charge(struct eden_heap_thread *self, long bytes, long blocks) {
  self->live += bytes;
  self->blocks += blocks;
  if (++self->pending >= 64 || self->live > (1l << 20) || self->live < -(1l << 20))
    eden_heap_flush(self);
}

static void eden_heap_account(long bytes, long blocks) {
  struct eden_heap_thread *self = eden_heap_self();
  if (self == NULL)
    eden_heap_apply(bytes, blocks);
  else
    eden_heap_charge(self, bytes, blocks);
}

/* Reuse the same TLS lookup for logical and physical ownership accounting.
 * Keeping this joint path avoids doubling pthread_getspecific on the common
 * uncached malloc/free path compared with the original allocator. */
static void eden_heap_account_physical(const void *address, long bytes, long blocks) {
  struct eden_heap_thread *self=eden_heap_self();
  eden_heap_physical_note(self,address,bytes,blocks);
  if (self) eden_heap_charge(self,bytes,blocks);
  else eden_heap_apply(bytes,blocks);
}
static void *eden_heap_record(void *address, int nonzero) {
  if (address == NULL)
    return ps5_heap_record_allocation(address, nonzero); /* counts the failure */
  const size_t usable=eden_heap_usable(address);
  if (eden_heap_in_range(address))
    eden_heap_account_physical(address,(long)usable,1);
  else
    eden_heap_account((long)usable,1);
  return address;
}

/* This thread's arena, created on its first small allocation. NULL = the heap's own spaces. */
static void *eden_heap_thread_arena(void) {
  struct eden_heap_thread *self = eden_heap_self();
  if (self == NULL)
    return NULL;
  if (self->arena != NULL)
    return self->arena;
  void *arena = eden_heap_create_arena();
  if (arena == NULL) {
    /* Arena limit reached: share the existing arenas round-robin. */
    const unsigned count = atomic_load_explicit(&eden_heap_arena_count, memory_order_acquire);
    if (count == 0)
      return NULL;
    arena = eden_heap_arena_list[atomic_fetch_add_explicit(&eden_heap_next_shared, 1, memory_order_relaxed) % count];
  }
  self->arena = arena;
  return arena;
}

/* A cached block for bytes (0 < bytes <= 16 * (EDEN_TCACHE_BINS - 1)), charged as allocated. */
static void *eden_tcache_take(size_t bytes) {
  const size_t bin = (bytes + 15) >> 4;
  if (bytes == 0 || bin >= EDEN_TCACHE_BINS)
    return NULL;
  struct eden_heap_thread *self = eden_heap_self();
  if (self == NULL || self->bins[bin] == NULL)
    return NULL;
  void *block = self->bins[bin];
  self->bins[bin] = ((void **)block)[0];
  self->counts[bin]--;
  const size_t usable = ((size_t *)block)[1];
  const size_t cached = atomic_load_explicit(&self->cached_bytes, memory_order_relaxed);
  if (cached < usable) __builtin_trap(); /* corrupt free-list / accounting */
  atomic_store_explicit(&self->cached_bytes, cached - usable, memory_order_relaxed);
  eden_heap_charge(self, (long)usable, 1);
  return block;
}

/* Keeps a freed owned block of this usable size; 0 when the bin is full or out of range. */
static int eden_tcache_keep(void *address, size_t usable) {
  const size_t bin = usable >> 4;
  if (bin == 0 || bin >= EDEN_TCACHE_BINS)
    return 0;
  struct eden_heap_thread *self = eden_heap_self();
  if (self == NULL || self->counts[bin] >= EDEN_TCACHE_LIMIT)
    return 0;
  const size_t cached = atomic_load_explicit(&self->cached_bytes, memory_order_relaxed);
  if (usable > EDEN_TCACHE_TOTAL_LIMIT ||
      cached > EDEN_TCACHE_TOTAL_LIMIT - usable)
    return 0;
  ((void **)address)[0] = self->bins[bin];
  ((size_t *)address)[1] = usable;
  self->bins[bin] = address;
  self->counts[bin]++;
  atomic_store_explicit(&self->cached_bytes, cached + usable, memory_order_relaxed);
  eden_heap_charge(self, -(long)usable, -1);
  return 1;
}

static void *eden_heap_allocate(size_t count, size_t size, int zero) {
  const size_t bytes = count * size;
  if (bytes >= EDEN_HEAP_LARGE_REQUEST && eden_heap_grows) {
    /* Memory of its own (it arrives zeroed). With every entry taken, or no memory left to take,
     * the heap's spaces: a piece committed earlier may still have the room. */
    void *block = eden_heap_large_allocate(bytes);
    if (block != NULL)
      return block;
  }
  if (bytes < EDEN_HEAP_SMALL_REQUEST) {
    void *arena = eden_heap_thread_arena();
    if (arena != NULL) {
      void *address = zero ? sceLibcMspaceCalloc(arena, count, size)
                           : sceLibcMspaceMalloc(arena, bytes);
      if (address != NULL)
        return address;
    }
  }
  return zero ? eden_heap_main(EDEN_HEAP_CALLOC, count, size) : eden_heap_main(EDEN_HEAP_MALLOC, bytes, 0);
}

void *__wrap_malloc(size_t size) {
  if (!ps5_heap_ready())
    return __real_malloc(size);
  void *cached = eden_tcache_take(size);
  return cached != NULL ? cached : eden_heap_record(eden_heap_allocate(1, size, 0), size != 0);
}

void *__wrap_calloc(size_t count, size_t size) {
  if (!ps5_heap_ready())
    return __real_calloc(count, size);
  if (size != 0 && count > (size_t)-1 / size) {
    atomic_fetch_add_explicit(&ps5_heap_failures, 1, memory_order_relaxed);
    return NULL;
  }
  void *cached = eden_tcache_take(count * size);
  if (cached != NULL)
    return memset(cached, 0, count * size);
  return eden_heap_record(eden_heap_allocate(count, size, 1),
                                    count != 0 && size != 0);
}

/* The C library permits realloc(p, 0) to release p and return NULL.
 * We make that behavior explicit for Eden-owned blocks, so free-list owners,
 * large direct mappings and live-byte counters all take the SAME free path.
 * Foreign pointers still use the original libc realloc semantics. */
void __wrap_free(void *address);
void *__wrap_realloc(void *address, size_t size) {
  if (address == NULL)
    return __wrap_malloc(size);
  const int in_range = eden_heap_in_range(address);
  const int large = in_range ? -1 : eden_heap_large_find(address);
  if (!in_range && large < 0)
    return __real_realloc(address, size);
  if (size == 0) {
    __wrap_free(address);
    return NULL;
  }
  const size_t before = in_range ? sceLibcMspaceMallocUsableSize(address) : eden_heap_large[large].size;
  if (large >= 0) {
    /* A large block keeps its memory while the new size still needs most of it. */
    if (size <= before && size >= before / 2)
      return address;
  } else if (size < EDEN_HEAP_LARGE_REQUEST || !eden_heap_grows) {
    void *owner = eden_heap_owner(address);
    void *result = sceLibcMspaceRealloc(owner, address, size);
    if (result) {
      const long delta=(long)sceLibcMspaceMallocUsableSize(result)-(long)before;
      eden_heap_account_physical(result,delta,0);
      return result;
    }
  }
  /* The block cannot stay where it is (its arena or its piece of the heap is full, or it
   * crosses the size at which blocks get memory of their own): move it. */
  void *moved = eden_heap_allocate(1, size, 0);
  if (moved == NULL) {
    atomic_fetch_add_explicit(&ps5_heap_failures, 1, memory_order_relaxed);
    return NULL;
  }
  memcpy(moved, address, before < size ? before : size);
  const size_t moved_usable=eden_heap_usable(moved);
  const long delta=(long)moved_usable-(long)before;
  /* A moved realloc is one logical block before and after the move.
   * Its physical owner can change from a root mspace to direct memory,
   * or between two roots. Resolve TLS only once for both root updates. */
  struct eden_heap_thread *self=eden_heap_self();
  if (eden_heap_in_range(moved))
    eden_heap_physical_note(self,moved,(long)moved_usable,1);
  if (self) eden_heap_charge(self,delta,0);
  else eden_heap_apply(delta,0);
  if (large>=0)
    eden_heap_large_free(large);
  else {
    sceLibcMspaceFree(eden_heap_owner(address),address);
    eden_heap_physical_note(self,address,-(long)before,-1);
  }
  return moved;
}

void __wrap_free(void *address) {
  if (eden_heap_in_range(address)) {
    const size_t usable = sceLibcMspaceMallocUsableSize(address);
    if (eden_tcache_keep(address, usable))
      return;
    eden_heap_account_physical(address,-(long)usable,-1);
    sceLibcMspaceFree(eden_heap_owner(address), address);
    return;
  }
  const int large = eden_heap_large_find(address);
  if (large >= 0) {
    eden_heap_account(-(long)eden_heap_large[large].size, -1);
    eden_heap_large_free(large);
  } else
    __real_free(address);
}

int __wrap_posix_memalign(void **address, size_t alignment, size_t size) {
  if (!ps5_heap_ready())
    return __real_posix_memalign(address, alignment, size);
  /* An alignment no space accepts must not make the heap grow looking for one that does. */
  if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0)
    return EINVAL;
  int result = ENOMEM;
  if (size < EDEN_HEAP_SMALL_REQUEST && alignment <= 4096) {
    void *arena = eden_heap_thread_arena();
    if (arena != NULL)
      result = sceLibcMspacePosixMemalign(arena, address, alignment, size);
  }
  if (result != 0) {
    /* A large block's own memory starts on a page; the heap's spaces align anything. */
    void *block = NULL;
    if (alignment <= 4096 && size >= EDEN_HEAP_LARGE_REQUEST && eden_heap_grows)
      block = eden_heap_large_allocate(size);
    if (block == NULL)
      block = eden_heap_main(EDEN_HEAP_ALIGNED, alignment, size);
    if (block != NULL) {
      *address = block;
      result = 0;
    } else {
      result = ENOMEM;
    }
  }
  if (result == 0)
    eden_heap_record(*address, size != 0);
  else
    atomic_fetch_add_explicit(&ps5_heap_failures, 1, memory_order_relaxed);
  return result;
}

size_t __wrap_malloc_usable_size(const void *address) {
  return ps5_heap_owns(address) ? eden_heap_usable(address) : __real_malloc_usable_size(address);
}

void ps5_opengl_heap_stats_print(unsigned iteration) {
  if (iteration == 0) {
    int state = atomic_load_explicit(&ps5_heap_state, memory_order_acquire);
    printf("[ps5-opengl-cts] mspace state=%d base=%p size=%u\n",
           state, state == 2 ? ps5_heap_base : NULL, PS5_OPENGL_HEAP_SIZE);
  }
}

/* The native import converter rejects unresolved weak application symbols.
 * A diagnostic build's strong definition overrides this default no-op. */
__attribute__((weak)) void ps5_opengl_gpu_snapshot(const char *phase, unsigned iteration) {
  (void)phase;
  (void)iteration;
}
void ps5_opengl_heap_snapshot(const char *phase, unsigned iteration) {
  ps5_opengl_gpu_snapshot(phase, iteration);
  printf("[ps5-opengl-heap] phase=%s sample=%u state=%d live_bytes=%zu peak_bytes=%zu "
         "blocks=%zu failures=%zu ambiguous_zero_reallocs=%zu\n", phase, iteration,
         atomic_load_explicit(&ps5_heap_state, memory_order_acquire),
         atomic_load_explicit(&ps5_heap_live_bytes, memory_order_relaxed),
         atomic_load_explicit(&ps5_heap_peak_bytes, memory_order_relaxed),
         atomic_load_explicit(&ps5_heap_blocks, memory_order_relaxed),
         atomic_load_explicit(&ps5_heap_failures, memory_order_relaxed),
         atomic_load_explicit(&ps5_heap_ambiguous_zero_reallocs, memory_order_relaxed));
}
