#!/usr/bin/env bash
# PS5 Vulkan - link recipe for titles that link RADV.
# Copyright (C) 2026 Mihawk-99
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by tools/build-radv-title.sh (and, later, by every title that links
# the RADV archive), so each links it the same way:
# - the archive my Mesa fork builds with -Dradv-winsys=ps5 (RADV, its compiler
#   and Mesa's runtime in one), linked whole;
# - the payload SDK's libc++, libc++abi and libunwind for ACO's C++, and
#   Clang's builtins for __emutls_get_address, as tools/psbc-link.sh has them;
# - the SDK's platform layer, whose ps5_ functions stand in for the libc
#   functions no system module exports, bound to libc's names here: a title
#   that defined libc's names itself would export them, which the title
#   converter refuses (platform/include/ps5platform/libc.h in the SDK fork).
#
# radv_link_recipe ROOT SDK_ROOT ARCHIVE sets radv_linker_script,
# radv_link_inputs and radv_link_flags. It returns 2 when an input is missing.

radv_link_recipe() {
    local root=$1 sdk_root=$2 archive=$3
    local compiler=${PS5_CLANG:-$(command -v clang || command -v clang-18 || true)}
    [[ -n $compiler ]] || { echo "clang was not found; set PS5_CLANG" >&2; return 2; }
    local builtins
    builtins="$("$compiler" --print-resource-dir)/lib/linux/libclang_rt.builtins-x86_64.a"
    local platform="$sdk_root/target/lib/libps5platform.a"

    radv_linker_script=(-T "$root/tooling/psbc/ps5-pie-unwind.ld" -L "$root/tooling/native")
    # Whole: Mesa's dispatch tables name every entry point through a weak
    # reference, and a weak reference pulls no archive member in, so an entry
    # point whose object nothing else needs would be NULL (the first console
    # run of PPSA99014 called one inside wsi_device_init).
    radv_link_inputs=(-L "$sdk_root/target/lib" --whole-archive "$archive" --no-whole-archive
        --start-group "$sdk_root/target/lib/libc++.a" "$sdk_root/target/lib/libc++abi.a"
        "$sdk_root/target/lib/libunwind.a" "$builtins" "$platform" --end-group)
    # Threads that ask for no stack get the main thread's 2 MiB in direct
    # memory (the platform's thread wraps), for RADV's, the CTS's and libc++'s
    # alike; join and detach free the stacks.
    radv_link_flags=(--wrap=pthread_create --wrap=pthread_join --wrap=pthread_detach)
    # The platform's open_memstream publishes its buffer at fflush and fclose
    # (src/memstream.c in the SDK fork's platform layer): Mesa's u_memstream,
    # RADV's recorded shader IR among its users.
    radv_link_flags+=(--wrap=fclose --wrap=fflush)
    local name
    # Every allocation the title makes goes to the platform's heap in direct
    # memory (ps5platform/heap.h): libc's private heap ran out under the CTS's
    # first shader build.
    for name in malloc calloc realloc free posix_memalign aligned_alloc memalign \
            malloc_usable_size reallocf reallocarray getline getdelim; do
        radv_link_flags+=("--wrap=$name")
    done
    for name in qsort_r mkstemps openlog popen pclose open_memstream __xuname __assert \
            __memset_chk regcomp regexec regfree regerror localtime_r newlocale freelocale \
            strtod_l strtof_l dladdr utimensat localeconv_l strtoll_l strtoull_l strtold_l \
            snprintf_l sscanf_l asprintf_l strcoll_l strxfrm_l strftime_l wcscoll_l wcsxfrm_l \
            btowc_l wctob_l iswctype_l mbrlen_l mbrtowc_l mbsrtowcs_l mbsnrtowcs_l wcrtomb_l \
            wcsnrtombs_l mbtowc_l ___mb_cur_max_l ___runetype_l ___tolower_l ___toupper_l \
            __runes_for_locale catopen catgets catclose backtrace backtrace_symbols_fd \
            __cxa_thread_atexit_impl; do
        radv_link_flags+=("--defsym=$name=ps5_$name")
    done
    # The rest of the platform's libc (ps5platform/libc.h): functions no system
    # module exports, which a title's import leaves pointing at nothing (libc++'s
    # random_device called arc4random through NULL:
    # dEQP-VK.pipeline.*.creation_cache_control), and those exported but
    # refused to a title or faulting in it. The directory functions go
    # together: a DIR from ps5_opendir is the platform's own.
    for name in arc4random arc4random_buf arc4random_uniform gmtime_r statvfs fstatvfs \
            futimens clock_nanosleep getaddrinfo freeaddrinfo if_nameindex if_freenameindex \
            opendir fdopendir readdir rewinddir dirfd closedir nl_langinfo nl_langinfo_l getpwuid_r \
            posix_fallocate access \
            openat unlinkat fchmodat fstatat mkdirat renameat memfd_create; do
        radv_link_flags+=("--defsym=$name=ps5_$name")
    done
    # A bound name the SDK's stub libraries also define would be exported from
    # the title to override theirs, and the title converter refuses exports:
    # every bound name stays local.
    local map="$root/build/radv-platform-local.map"
    mkdir -p "$root/build"
    {
        printf '{\n    local:\n'
        local flag
        for flag in "${radv_link_flags[@]}"; do
            [[ $flag == --defsym=* ]] || continue
            flag=${flag#--defsym=}
            printf '        %s;\n' "${flag%%=*}"
        done
        printf '};\n'
    } > "$map.tmp"
    mv "$map.tmp" "$map"
    radv_link_flags+=(--version-script "$map")

    local file
    for file in "$archive" "$platform" "$sdk_root/target/lib/libc++.a" \
            "$sdk_root/target/lib/libc++abi.a" "$sdk_root/target/lib/libunwind.a" "$builtins"; do
        [[ -f $file ]] || { echo "missing $file" >&2; return 2; }
    done
}
