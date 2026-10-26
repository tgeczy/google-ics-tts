"""Convert an ICS Flint PHONETIC-ARTS3 voice into an hts_engine API 1.10 .htsvoice.

Usage: python flint2htsvoice.py <flint voice> <out.htsvoice>

Streams written: MCP (mel-LSP, 25 x 3 windows), LF0 (MSD, 1 x 3 windows) and
duration.  The mixed-excitation stream is dropped: the ICS engine_cfg uses
"pulses" excitation, which is what hts_engine's vocoder does.
"""
import io
import contextlib
import struct
import sys
import os

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from flint_parser import FlintVoice  # noqa: E402
import flint_trees  # noqa: E402

TREE_STREAM = {'dur': 0, 'mcp': 1, 'me': 2, 'lf0': 3}


def qs_text(names, compact):
    out = []
    for i, group in enumerate(compact):
        pats = ','.join('"%s"' % names[j] for j in group)
        out.append('QS "Q%d" { %s }\n' % (i, pats))
    return ''.join(out)


def tree_text(tree, state, prefix):
    lines = ['{*}[%d]\n{\n' % state]
    index = {0: 0}
    next_id = -1
    queue = [0]
    while queue:
        p = queue.pop(0)
        yes, no = tree.children(p)

        def ref(c):
            nonlocal next_id
            if tree.is_node(tree.children(c)[0]):
                index[c] = next_id
                next_id -= 1
                queue.append(c)
                return str(index[c])
            return '"%s_s%d_%d"' % (prefix, state, tree.value(c))

        no_ref, yes_ref = ref(no), ref(yes)
        lines.append('%6d "Q%d" %s %s\n' % (index[p], tree.value(p), no_ref, yes_ref))
    lines.append('}\n\n')
    return ''.join(lines)


def single_leaf_root(tree):
    return not tree.is_node(tree.children(0)[0])


def stream_rows(stream, state):
    start = sum(stream.counts[:state])
    return start, stream.counts[state]


def main(src, dst):
    with contextlib.redirect_stdout(io.StringIO()):
        fv = FlintVoice(src).parse()
    names, compact, trees = flint_trees.parse(fv.raw)
    ns = fv.num_states
    qs = qs_text(names, compact)

    dur = fv.streams['duration']
    mcp = fv.streams['cepstra']
    lf0 = fv.streams['f0']
    msd_bits = fv.msd_data[8:]

    def voiced(g):
        return (msd_bits[g >> 3] >> (g & 7)) & 1

    blobs = {}

    # duration: one tree, pdf k holds the 5 state durations
    n = dur.counts[0]
    b = io.BytesIO()
    b.write(struct.pack('<I', n))
    for k in range(n):
        means = [dur.means[s * n + k, 0] for s in range(ns)]
        varis = [dur.variances[s * n + k, 0] for s in range(ns)]
        b.write(np.asarray(means + varis, dtype='<f4').tobytes())
    blobs['DURATION_PDF'] = b.getvalue()
    t = trees[TREE_STREAM['dur']][0]
    blobs['DURATION_TREE'] = (qs + '\n' + tree_text(t, 2, 'dur')).encode('ascii')

    # mcp
    b = io.BytesIO()
    b.write(struct.pack('<%dI' % ns, *mcp.counts))
    for s in range(ns):
        start, cnt = stream_rows(mcp, s)
        rows = np.concatenate([mcp.means[start:start + cnt], mcp.variances[start:start + cnt]], axis=1)
        b.write(rows.astype('<f4').tobytes())
    blobs['STREAM_PDF[MCP]'] = b.getvalue()
    blobs['STREAM_TREE[MCP]'] = (qs + '\n' + ''.join(
        tree_text(trees[TREE_STREAM['mcp']][s], s + 2, 'mcp') for s in range(ns))).encode('ascii')

    # lf0 (MSD)
    b = io.BytesIO()
    b.write(struct.pack('<%dI' % ns, *lf0.counts))
    for s in range(ns):
        start, cnt = stream_rows(lf0, s)
        w = np.array([[float(voiced(start + k))] for k in range(cnt)], dtype=np.float32)
        rows = np.concatenate([lf0.means[start:start + cnt], lf0.variances[start:start + cnt], w], axis=1)
        b.write(rows.astype('<f4').tobytes())
    blobs['STREAM_PDF[LF0]'] = b.getvalue()
    blobs['STREAM_TREE[LF0]'] = (qs + '\n' + ''.join(
        tree_text(trees[TREE_STREAM['lf0']][s], s + 2, 'lf0') for s in range(ns))).encode('ascii')

    for t_list in trees.values():
        for t in t_list:
            assert not single_leaf_root(t)

    wins = {}
    for key, tag in (('MCP', '<mcp>'), ('LF0', '<lf0>')):
        out = []
        for w in fv.window_coeffs[tag]:
            out.append(('%d %s\n' % (int(w[0]), ' '.join(repr(x) for x in w[1:]))).encode('ascii'))
        wins[key] = out

    order = ['DURATION_PDF', 'DURATION_TREE']
    data = io.BytesIO()
    pos = {}

    def put(key, blob):
        start = data.tell()
        data.write(blob)
        pos[key] = (start, data.tell() - 1)

    put('DURATION_PDF', blobs['DURATION_PDF'])
    put('DURATION_TREE', blobs['DURATION_TREE'])
    for key in ('MCP', 'LF0'):
        for i, w in enumerate(wins[key]):
            put('WIN%s%d' % (key, i), w)
    for key in ('MCP', 'LF0'):
        put('STREAM_PDF[%s]' % key, blobs['STREAM_PDF[%s]' % key])
    for key in ('MCP', 'LF0'):
        put('STREAM_TREE[%s]' % key, blobs['STREAM_TREE[%s]' % key])

    def rng(key):
        return '%d-%d' % pos[key]

    header = [
        '[GLOBAL]',
        'HTS_VOICE_VERSION:1.0',
        'SAMPLING_FREQUENCY:%d' % fv.sample_rate,
        'FRAME_PERIOD:%d' % fv.frame_period,
        'NUM_STATES:%d' % ns,
        'NUM_STREAMS:2',
        'STREAM_TYPE:MCP,LF0',
        'FULLCONTEXT_FORMAT:FLINT_ICS',
        'FULLCONTEXT_VERSION:1.0',
        'GV_OFF_CONTEXT:"*-sil+*"',
        'COMMENT:converted from %s' % os.path.basename(src),
        '[STREAM]',
        'VECTOR_LENGTH[MCP]:%d' % (mcp.dim // 3),
        'VECTOR_LENGTH[LF0]:1',
        'IS_MSD[MCP]:0',
        'IS_MSD[LF0]:1',
        'NUM_WINDOWS[MCP]:3',
        'NUM_WINDOWS[LF0]:3',
        'USE_GV[MCP]:0',
        'USE_GV[LF0]:0',
        'OPTION[MCP]:ALPHA=%.3f,GAMMA=%d,LN_GAIN=0' % (fv.freq_warping, fv.stage),
        'OPTION[LF0]:',
        '[POSITION]',
        'DURATION_PDF:' + rng('DURATION_PDF'),
        'DURATION_TREE:' + rng('DURATION_TREE'),
        'STREAM_WIN[MCP]:' + ','.join(rng('WINMCP%d' % i) for i in range(3)),
        'STREAM_WIN[LF0]:' + ','.join(rng('WINLF0%d' % i) for i in range(3)),
        'STREAM_PDF[MCP]:' + rng('STREAM_PDF[MCP]'),
        'STREAM_PDF[LF0]:' + rng('STREAM_PDF[LF0]'),
        'STREAM_TREE[MCP]:' + rng('STREAM_TREE[MCP]'),
        'STREAM_TREE[LF0]:' + rng('STREAM_TREE[LF0]'),
        '[DATA]',
    ]
    with open(dst, 'wb') as f:
        f.write(('\n'.join(header) + '\n').encode('ascii'))
        f.write(data.getvalue())
    print('wrote', dst, os.path.getsize(dst), 'bytes')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
