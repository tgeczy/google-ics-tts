"""Complete parser for PHONETIC-ARTS3 / FLINT-HMM-VOICE format.

Extracts:
  - Voice metadata (type, sample rate, etc.)
  - Decision trees with questions
  - All 4 HMM streams (duration, cepstra, ME, F0) with dequantized PDFs
  - Window coefficients
  - MSD voicing data

Usage:
  voice = FlintVoice("path/to/voice")
  voice.parse()
"""
import struct
import numpy as np
import re
from dataclasses import dataclass, field
from typing import List, Optional, Dict, Tuple


@dataclass
class StreamData:
    name: str
    dim: int
    counts: List[int]  # per-state PDF counts
    quantized: np.ndarray  # uint16 raw quantized data
    mean_codebooks: np.ndarray  # shape (dim, 256)
    var_codebooks: np.ndarray   # shape (dim, 256)
    means: Optional[np.ndarray] = None  # dequantized means
    variances: Optional[np.ndarray] = None  # dequantized variances


@dataclass
class TreeBlock:
    num_questions: int
    question_lengths: np.ndarray
    question_strings: List[str]
    tree_data: bytes  # raw compact decision tree data


@dataclass
class FlintVoice:
    path: str
    raw: bytes = field(default=b'', repr=False)

    # Metadata
    voice_type: int = 0
    sample_rate: int = 16000
    frame_period: int = 80
    freq_warping: float = 0.420
    stage: int = 1
    f0_box_cox_lambda: float = 0.0
    description: str = ''
    timestamp: str = ''

    # Sections
    num_states: int = 5
    streams: Dict[str, StreamData] = field(default_factory=dict)
    tree_block: Optional[TreeBlock] = None
    window_coeffs: Dict[str, List[List[float]]] = field(default_factory=dict)
    gv_data: Dict[str, bytes] = field(default_factory=dict)
    msd_data: bytes = b''

    def parse(self):
        with open(self.path, 'rb') as f:
            self.raw = f.read()

        self._parse_header()
        self._parse_trees()
        self._parse_models()
        self._parse_windows()
        self._parse_gv()
        return self

    def _find_section(self, tag):
        """Find content between <TAG>\\n and </TAG>."""
        open_tag = f'<{tag}>'.encode()
        close_tag = f'</{tag}>'.encode()
        start = self.raw.find(open_tag)
        if start < 0:
            return None, -1, -1
        content_start = self.raw.index(b'\n', start) + 1
        end = self.raw.find(close_tag)
        return self.raw[content_start:end], content_start, end

    def _parse_tag_value(self, tag):
        """Parse a simple <TAG>\\nVALUE\\n</TAG> from the header."""
        content, _, _ = self._find_section(tag)
        if content is None:
            return None
        return content.strip().decode('ascii')

    def _parse_header(self):
        self.voice_type = int(self._parse_tag_value('VOICE-TYPE') or '0')
        self.sample_rate = int(self._parse_tag_value('SAMPLE-RATE') or '16000')
        self.frame_period = int(self._parse_tag_value('FRAME-PERIOD') or '80')
        self.freq_warping = float(self._parse_tag_value('FREQ-WARPING') or '0.420')
        self.description = self._parse_tag_value('DESCRIPTION') or ''
        self.timestamp = self._parse_tag_value('TIMESTAMP') or ''

        stage_str = self._parse_tag_value('STAGE')
        if stage_str:
            self.stage = int(stage_str)
        else:
            mgc = self._parse_tag_value('MGC-GAMMA-STAGE')
            self.stage = int(mgc) if mgc else 0

        lam = self._parse_tag_value('F0-BOX-COX-LAMBDA')
        self.f0_box_cox_lambda = float(lam) if lam else 0.0

        print(f"Voice type: {self.voice_type}")
        print(f"Sample rate: {self.sample_rate} Hz")
        print(f"Frame period: {self.frame_period}")
        print(f"Freq warping: {self.freq_warping}")
        print(f"Stage: {self.stage}")
        print(f"Description: {self.description[:80]}")

    def _parse_trees(self):
        """Parse the decision tree block (between </DSP> and <MODELS>)."""
        dsp_end = self.raw.find(b'</DSP>')
        models_start = self.raw.find(b'<MODELS>')

        if dsp_end < 0 or models_start < 0:
            print("WARNING: Can't find tree block boundaries")
            return

        # Tree block starts after </DSP>\n
        tree_start = self.raw.index(b'\n', dsp_end) + 1
        tree_data = self.raw[tree_start:models_start]

        # First uint32: size of entire tree block
        block_size = struct.unpack_from('<I', tree_data, 0)[0]
        print(f"\nTree block: {block_size} bytes declared, {len(tree_data)} actual")

        # Question table: uint16 count, then uint8[count] lengths, then strings
        offset = 4
        num_q = struct.unpack_from('<H', tree_data, offset)[0]
        offset += 2

        q_lens = np.frombuffer(tree_data[offset:offset+num_q], dtype=np.uint8)
        offset += num_q

        questions = []
        for l in q_lens:
            length = int(l)
            q = tree_data[offset:offset+length].decode('ascii')
            questions.append(q)
            offset += length

        print(f"Questions: {num_q}")
        print(f"  First 5: {questions[:5]}")
        print(f"  Last 5: {questions[-5:]}")

        tree_binary = tree_data[offset:4+block_size]
        print(f"Tree binary data: {len(tree_binary)} bytes")

        self.tree_block = TreeBlock(
            num_questions=num_q,
            question_lengths=q_lens,
            question_strings=questions,
            tree_data=tree_binary
        )

    def _parse_models(self):
        """Parse MODELS section with all 4 streams."""
        content, content_start, _ = self._find_section('MODELS')
        if content is None:
            print("ERROR: No MODELS section")
            return

        M = content
        self.num_states = struct.unpack_from('<I', M, 0)[0]
        print(f"\nMODELS: {len(M)} bytes, num_states={self.num_states}")

        stream_names = ['duration', 'cepstra', 'me', 'f0']
        offset = 4

        for name in stream_names:
            offset = self._parse_stream(M, offset, name)

        # Remaining bytes after F0 = MSD voicing bitmap
        self.msd_data = M[offset:]
        print(f"\nMSD voicing data: {len(self.msd_data)} bytes")
        if len(self.msd_data) > 4:
            # Parse header
            v1, v2 = struct.unpack_from('<II', self.msd_data, 0)
            print(f"  Header: {v1}, {v2}")

    def _parse_stream(self, M, offset, name):
        """Parse one quantized stream, return next stream offset."""
        ns = self.num_states
        dim = struct.unpack_from('<H', M, offset)[0]
        counts = [struct.unpack_from('<H', M, offset + 2 + i*2)[0] for i in range(ns)]
        total = sum(counts)

        header_size = 2 + ns * 2
        data_start = offset + header_size
        data_bytes = total * dim * 2
        data_end = data_start + data_bytes
        pad = (4 - (data_end % 4)) % 4
        data_end += pad

        cb_bytes = dim * 2048
        next_offset = data_end + cb_bytes

        # Read quantized data
        quant = np.frombuffer(M[data_start:data_start + data_bytes], dtype='<u2')
        quant = quant.reshape(total, dim)

        # Read codebooks: layout is all mean codebooks first, then all var codebooks
        # Each codebook = 256 float32 = 1024 bytes per dimension
        cb_raw = np.frombuffer(M[data_end:data_end + cb_bytes], dtype='<f4')
        cb_raw = cb_raw.reshape(2, dim, 256)  # (mean/var, dim, 256)
        mean_cb = cb_raw[0]  # (dim, 256)
        var_cb = cb_raw[1]   # (dim, 256)

        # Dequantize: each uint16 = (mean_idx << 8) | var_idx
        mean_idx = (quant >> 8).astype(np.uint8)  # (total, dim)
        var_idx = (quant & 0xFF).astype(np.uint8)  # (total, dim)

        means = np.empty_like(quant, dtype=np.float32)
        variances = np.empty_like(quant, dtype=np.float32)

        for d in range(dim):
            means[:, d] = mean_cb[d, mean_idx[:, d]]
            variances[:, d] = var_cb[d, var_idx[:, d]]

        stream = StreamData(
            name=name,
            dim=dim,
            counts=counts,
            quantized=quant,
            mean_codebooks=mean_cb,
            var_codebooks=var_cb,
            means=means,
            variances=variances
        )

        self.streams[name] = stream

        print(f"\n  {name}: dim={dim}, counts={counts}, total={total}")
        print(f"    Mean range: [{means.min():.4f}, {means.max():.4f}]")
        print(f"    Var range: [{variances.min():.6f}, {variances.max():.4f}]")

        return next_offset

    def _parse_windows(self):
        """Parse WINDOW-COEFFS section."""
        content, _, _ = self._find_section('WINDOW-COEFFS')
        if content is None:
            return

        text = content.decode('ascii')
        # Format: stream_name\nwin0\nwin1\nwin2\n...
        lines = [l.strip() for l in text.split('\n') if l.strip()]

        current_stream = None
        for line in lines:
            if not any(c.isdigit() or c in '.-' for c in line[:3]):
                current_stream = line
                self.window_coeffs[current_stream] = []
            else:
                coeffs = [float(x) for x in line.split()]
                if current_stream:
                    self.window_coeffs[current_stream].append(coeffs)

        print(f"\nWindow coefficients:")
        for name, wins in self.window_coeffs.items():
            print(f"  {name}: {len(wins)} windows")
            for i, w in enumerate(wins):
                print(f"    win{i}: {w}")

    def _parse_gv(self):
        """Parse GLOBAL-VARIANCE section."""
        content, _, _ = self._find_section('GLOBAL-VARIANCE')
        if content is None:
            return

        # GV contains sub-sections like <mcp>...</mcp>, <me>...</me>, <lf0>...</lf0>
        for tag in ['mcp', 'me', 'lf0']:
            open_t = f'<{tag}>'.encode()
            close_t = f'</{tag}>'.encode()
            s = content.find(open_t)
            if s >= 0:
                s = content.index(b'\n', s) + 1
                e = content.find(close_t)
                self.gv_data[tag] = content[s:e]
                print(f"  GV {tag}: {len(self.gv_data[tag])} bytes")


if __name__ == '__main__':
    import sys
    voice = FlintVoice(sys.argv[1])  # assets/eng-usa/voice from the APK
    voice.parse()

    # Validate duration means
    dur = voice.streams['duration']
    print(f"\n=== Duration validation ===")
    print(f"Total PDFs: {dur.means.shape[0]}")
    print(f"Mean of means: {dur.means.mean():.2f} frames")
    print(f"Median of means: {np.median(dur.means):.2f} frames")

    # Validate F0
    f0 = voice.streams['f0']
    print(f"\n=== F0 validation ===")
    print(f"Static F0 means (first dim): [{f0.means[:,0].min():.2f}, {f0.means[:,0].max():.2f}]")
    print(f"  exp range: [{np.exp(f0.means[:,0].min()):.1f}, {np.exp(f0.means[:,0].max()):.1f}] Hz")

    # Validate cepstra
    cep = voice.streams['cepstra']
    print(f"\n=== Cepstra validation ===")
    print(f"Dimensions: {cep.dim} (should be 75 = 25 static × 3 windows)")
    print(f"Static dims (first 25) mean range: [{cep.means[:,:25].min():.4f}, {cep.means[:,:25].max():.4f}]")

    print(f"\n=== MSD voicing ===")
    if len(voice.msd_data) > 8:
        v1, v2 = struct.unpack_from('<II', voice.msd_data, 0)
        bitmap = voice.msd_data[8:]
        total_f0 = sum(f0.counts)
        # Count voiced bits
        bits = 0
        voiced = 0
        for b in bitmap:
            for bit in range(8):
                if bits < total_f0:
                    if b & (1 << bit):
                        voiced += 1
                    bits += 1
        print(f"  F0 PDFs: {total_f0}, voiced: {voiced} ({100*voiced/total_f0:.1f}%)")
