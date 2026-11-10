# PS5_FrameGen - one table from the benchmark's results: ours (train/bench.py --json) and the public models'
# (train/bench_public.py, ~/retro-tests/bench-public.json), over all clips, the 3D clips and the 2D ones.
#
#   .venv/bin/python train/bench_table.py NAME=bench-NAME.json ... [--public FILE]
#
# Copyright (C) 2026 Mihawk
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import json
import os
import numpy as np


def three_d_groups():
    path = os.path.expanduser('~/retro-tests/train-clips/3d.list')
    return set(l.strip().rsplit('-', 1)[0] for l in open(path) if l.strip()) if os.path.exists(path) else set()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ours', nargs='*', help='NAME=file.json (train/bench.py --json)')
    ap.add_argument('--public', default=os.path.expanduser('~/retro-tests/bench-public.json'))
    ap.add_argument('--only', help='comma list of public columns to show')
    args = ap.parse_args()
    cols = {}      # column -> {group: psnr}
    if os.path.exists(args.public):
        pub = json.load(open(args.public))['groups']
        for g, row in pub.items():
            for k, v in row.items():
                cols.setdefault(k, {})[g] = v
    for item in args.ours:
        name, path = item.split('=', 1)
        groups = json.load(open(path))['groups']
        for g, row in groups.items():
            for k, v in row.items():
                if k in ('blend', 'repeat'):
                    continue
                cols.setdefault(name, {})[g] = v
    if args.only:
        keep = set(args.only.split(',')) | {'blend', 'repeat'} | set(i.split('=')[0] for i in args.ours)
        cols = {k: v for k, v in cols.items() if k in keep}
    d3 = three_d_groups()
    common = set.intersection(*[set(v) for v in cols.values()])
    is3d = {g for g in common if g.rsplit('-', 1)[0] in d3 or g in d3}
    print(f'{len(common)} groups in common ({len(is3d)} 3D, {len(common) - len(is3d)} 2D)')
    print('| model | all groups | 3D groups | 2D groups |')
    print('| --- | --- | --- | --- |')
    def avg(c, gs):
        return float(np.mean([cols[c][g] for g in gs])) if gs else float('nan')
    for c in sorted(cols, key=lambda c: -avg(c, common)):
        print(f'| {c} | {avg(c, common):.2f} | {avg(c, is3d):.2f} | {avg(c, common - is3d):.2f} |')


if __name__ == '__main__':
    main()
