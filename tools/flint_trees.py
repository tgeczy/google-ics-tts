"""Decoder for the Flint/PATTS compact decision tree block.

Block layout (between </DSP>\\n and <MODELS>):
  u32 block_size, then payload:
    u16 nq, u8[nq] name_len, names
    u16 K, u8[K] group_size, u16[sum(group_size)] original question ids   (compact questions)
    u8 num_streams
    per stream: u8 stream_id, u16 num_trees, then per tree:
      u32 num_words, u32 num_bits, u32[num_words] bits (LSB first)   Jacobson level-order bitmap
      u32 c1, u32[c1] rank-per-256-bits ; u32 c2, u8[c2] rank-per-word, pad to 4
      u16 N, u16[N] node values (indexed by rank1 of the node's bit)
"""
import struct
import sys


def tree_block_payload(raw):
    a = raw.index(b'\n', raw.find(b'</DSP>')) + 1
    b = raw.find(b'<MODELS>')
    t = raw[a:b]
    bs = struct.unpack_from('<I', t, 0)[0]
    return t[4:4 + bs]


class Tree:
    def __init__(self, bits, values):
        self.bits = bits
        self.values = values
        rank = []
        r = 0
        for x in bits:
            r += x
            rank.append(r)
        self.rank = rank

    def children(self, p):
        r = self.rank[p]
        return 2 * r - 1, 2 * r

    def is_node(self, p):
        return p < len(self.bits) and self.bits[p] == 1

    def value(self, p):
        return self.values[self.rank[p] - 1]

    def classify(self):
        internal, leaves, bad = [], [], 0
        for p, bit in enumerate(self.bits):
            if not bit:
                continue
            l, r = self.children(p)
            lb, rb = self.is_node(l), self.is_node(r)
            if lb and rb:
                internal.append(p)
            elif not lb and not rb:
                leaves.append(p)
            else:
                bad += 1
        return internal, leaves, bad


def parse(raw):
    pl = tree_block_payload(raw)
    nq = struct.unpack_from('<H', pl, 0)[0]
    pos = 2 + nq
    names = []
    for ln in pl[2:2 + nq]:
        names.append(pl[pos:pos + ln].decode('ascii'))
        pos += ln
    K = struct.unpack_from('<H', pl, pos)[0]
    sizes = pl[pos + 2:pos + 2 + K]
    pos += 2 + K
    compact = []
    for s in sizes:
        compact.append(list(struct.unpack_from('<%dH' % s, pl, pos)))
        pos += 2 * s
    ns = pl[pos]
    pos += 1
    streams = {}
    for _ in range(ns):
        sid = pl[pos]
        nt = struct.unpack_from('<H', pl, pos + 1)[0]
        pos += 3
        trees = []
        for _ in range(nt):
            w, nb = struct.unpack_from('<II', pl, pos)
            words = struct.unpack_from('<%dI' % w, pl, pos + 8)
            pos += 8 + 4 * w
            c1 = struct.unpack_from('<I', pl, pos)[0]
            pos += 4 + 4 * c1
            c2 = struct.unpack_from('<I', pl, pos)[0]
            pos += 4 + c2 + ((4 - c2 % 4) % 4)
            n = struct.unpack_from('<H', pl, pos)[0]
            vals = struct.unpack_from('<%dH' % n, pl, pos + 2)
            pos += 2 + 2 * n
            bits = [(wd >> b) & 1 for wd in words for b in range(32)][:nb]
            trees.append(Tree(bits, vals))
        streams[sid] = trees
    assert pos == len(pl), (pos, len(pl))
    return names, compact, streams


if __name__ == '__main__':
    raw = open(sys.argv[1], 'rb').read()
    names, compact, streams = parse(raw)
    print('questions', len(names), 'compact', len(compact))
    for sid in sorted(streams):
        for i, t in enumerate(streams[sid]):
            internal, leaves, bad = t.classify()
            iv = [t.value(p) for p in internal]
            lv = [t.value(p) for p in leaves]
            print('stream %d tree %d: nodes=%d internal=%d leaves=%d one-child=%d  '
                  'q=[%d-%d] pdf=[%d-%d] uniq_pdf=%d' % (
                      sid, i, len(t.values), len(internal), len(leaves), bad,
                      min(iv), max(iv), min(lv), max(lv), len(set(lv))))
