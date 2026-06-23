# MNUEv2 / Reckless execution comparison

Date: 2026-06-23

Target:

- `F:\MagnusChess`
- MIT license
- MNUEv2 format 4, architecture 7

Read-only reference:

- `D:\Reckless-main\Reckless-main`
- Reckless 0.10.0-dev
- GNU AGPL-3.0

No Reckless source file was modified. No MagnusChess implementation change was
made for this investigation.

## 1. Executive summary

Reckless does not use three concatenated branches and does not provide a
paired-perspective row layout that MagnusChess can copy. It uses two independent
sparse feature tables:

1. a 7,680-row `int16` piece/king-bucket table;
2. a 66,864-row `int8` directed threat-edge table.

Both tables produce 768-wide `int16` accumulators for each perspective. Their
accumulators are added elementwise before pairwise activation. The two
384-wide activated perspectives are then concatenated to a 768-byte dense-head
input.

The most important performance mechanisms are:

1. Threat deltas are generated directly from board mutation events. Reckless
   does not rebuild a threat feature set or diff parent and child sets.
2. It stores a full sparse accumulator state at every ply. Evaluation lazily
   replays deltas from the nearest accurate ancestor, while unmake only
   decrements the network stack index and performs no neural row operations.
3. The selected head begins with a sparse `u8 x int8` 768-to-16 transform. It
   skips zero four-byte activation chunks and uses AVX2 `maddubs/maddwd` or
   AVX-512 VNNI dot products.

MagnusChess already has local AttackGraph source discovery and lazy
materialisation, but still:

- rescans incoming attackers to rebuild tactical status/relation signatures;
- performs inverse row application on materialised unmake;
- materialises all 768 floating-point head activations before checking which
  branch partials are reusable;
- uses a 768-to-32 `float x int16` first head layer.

The clean, format-compatible path is to:

1. benchmark an accumulator-per-materialised-ply or hybrid checkpoint stack;
2. maintain typed incoming-attack counters and compact tactical signatures;
3. avoid building unchanged branch activations and fuse dirty-branch activation
   with its first-head partial;
4. regenerate PGO after the execution model stabilises.

A quantised `u8 x int8` first head layer is the strongest remaining head
optimisation, but it requires a new exporter/format contract and new parity
testing.

From the measured 250,479 NPS MagnusChess baseline, a reasonable cumulative
estimate is:

- format-compatible stages: approximately 0.57M-0.82M NPS;
- with a validated quantised first head: approximately 0.72M-1.00M NPS.

The 0.8M gate remains technically realistic, but it is near the upper end of
format-compatible changes. It should not be treated as guaranteed without
removing inverse row traffic and materially reducing the Attack summary and
dense-head costs.

## 2. Evidence labels

This report uses three labels:

- **Confirmed**: directly established by source, constants, types, loops, or
  measured output.
- **Likely intent**: a design purpose strongly suggested by the implementation.
- **Hypothesis**: requires a MagnusChess A/B benchmark before acceptance.

## 3. License constraints

### Confirmed

- Reckless declares AGPL-3.0 in `LICENSE:1-30` and
  `README.md:151-153`.
- MagnusChess declares MIT in `F:\MagnusChess\LICENSE:1-20`.

MIT code can be included in an AGPL work, but copying or adapting Reckless
implementation code into MagnusChess would make an MIT-only distribution
position untenable. The combined work would need to satisfy AGPL source and
network-use obligations.

Architectural ideas, measured behaviour, public interfaces, dimensions, and
independently derived algorithms are not the same as copying protected source
expression. The safe transfer method is a clean-room MagnusChess
implementation based on required behaviour and benchmarks, without copying
Reckless code, comments, table expressions, or intrinsic sequences.

This is an engineering license assessment, not legal advice.

## 4. Reckless file map

### Network and sparse accumulators

| File | Role |
|---|---|
| `src/nnue.rs` | Network constants, per-thread ownership, lazy accumulator replay, perspective routing, selected head, parameter image. |
| `src/nnue/accumulator.rs` | Piece accumulator refresh cache. |
| `src/nnue/accumulator/psq.rs` | Piece/king-bucket indexing, direct move delta, refresh-cache update kernel. |
| `src/nnue/accumulator/threats.rs` | Packed threat delta, full refresh, scalar/SIMD row update. |
| `src/nnue/accumulator/threats/threat_index.rs` | Directed attacker/target feature index tables and perspective transforms. |
| `src/nnue/accumulator/threats/scalar.rs` | Scalar local threat-edge discovery for change/move/mutate events. |
| `src/nnue/accumulator/threats/vectorized.rs` | ISA-independent vectorised ray-frontier flow. |
| `src/nnue/accumulator/threats/vectorized/avx2.rs` | AVX2 mailbox-to-eight-rays transform and packed delta emission. |
| `src/nnue/accumulator/threats/vectorized/avx512.rs` | AVX-512 VBMI2 ray transform and compressed packed delta emission. |

### Activation and dense inference

| File | Role |
|---|---|
| `src/nnue/forward/vectorized.rs` | Sum sparse families, pairwise activation, NNZ discovery, selected sparse first head, dense tail. |
| `src/nnue/forward/scalar.rs` | Scalar reference/fallback for the same inference flow. |
| `src/nnue/simd/avx2.rs` | AVX2 arithmetic, `int8 -> int16`, pairwise activation, emulated `dpbusd`. |
| `src/nnue/simd/avx512.rs` | AVX-512 arithmetic and optional AVX-512 VNNI `dpbusd`. |
| `src/nnue/simd/neon.rs` | AArch64 NEON backend. |
| `src/nnue/simd/scalar.rs` | Generic scalar primitives. |
| `src/nnue/simd/wasm.rs` | WebAssembly SIMD backend. |

### Board and search integration

| File | Role |
|---|---|
| `src/board/makemove.rs` | Board mutations and NNUE observer callbacks for quiet, capture, EP, castling, and promotion. |
| `src/board.rs` | Board state, persistent search threat maps, `BoardObserver`, legality state. |
| `src/search.rs` | Root refresh, make/push/pop order, TT raw-eval reuse, evaluator call sites. |
| `src/evaluation.rs` | Post-network score correction and fifty-move scaling. |
| `src/transposition.rs` | Eight-byte TT entry containing an `i16` raw evaluation. |
| `src/stack.rs` | Search stack; separate from the NNUE accumulator stacks. |
| `src/thread.rs` | Per-search-thread `Network` ownership. |
| `src/threadpool.rs` | Search-worker construction and sharing. |
| `src/uci.rs` | `eval`, compiler reporting, bench/speedtest command dispatch. |

### Build, format, and benchmarks

| File | Role |
|---|---|
| `build/build.rs` | Compile-time network selection/download, generated attack tables, compiler metadata. |
| `build/attacks.rs` | Generated attack table support. |
| `build/magics.rs` | Generated magic-bitboard constants. |
| `build/maps.rs` | Generated geometric maps. |
| `Cargo.toml` | Release LTO, codegen, panic profile. |
| `Makefile` | Native CPU build and local PGO sequence. |
| `.github/workflows/release.yml` | Generic, AVX2, and AVX-512 PGO release builds. |
| `.github/workflows/pgo.yml` | CI PGO build and bench. |
| `.github/workflows/architectures.yml` | Compile checks for generic, x86-64-v3, x86-64-v4, macOS, and WASM. |
| `src/tools/bench.rs` | Fixed 50-position depth-12 search benchmark. |
| `src/tools/speedtest.rs` | Fixed 50-position timed search benchmark. |
| `networks/v60-7f587dfb.nnue` | Raw compile-time parameter image, 63,266,880 bytes. |

## 5. Confirmed Reckless network architecture

Source:

- dimensions and bucket maps: `src/nnue.rs:82-123`;
- parameter types: `src/nnue.rs:356-366`;
- activation and head: `src/nnue/forward/vectorized.rs:10-157`;
- feature indices: `src/nnue/accumulator/psq.rs:204-210`,
  `src/nnue/accumulator/threats/threat_index.rs:33-116`.

```text
Piece/king-bucket table
    7,680 x 768, int16
          |
          +--> PST accumulator[pov], int16[768]

Directed threat-edge table
    66,864 x 768, int8
          |
          +--> Threat accumulator[pov], int16[768]

For each POV:
    combined[768] = PST[768] + Threat[768]
    pairwise activation:
        combined[0..384] * combined[384..768]
        clipped/shifted/packed -> u8[384]

Final input:
    activated STM[384] || activated NTM[384] = u8[768]

Selected occupancy bucket only, 8 buckets:
    sparse int8 affine 768 -> 16
    clamp [0, 1]
    float affine 16 -> 32
    clamp [0, 1]
    float affine 32 -> 1
    score scale 380
```

### Sparse tables

| Family | Vocabulary | Row width | Weight | Accumulator | Bias |
|---|---:|---:|---|---|---|
| Piece/king bucket | 7,680 | 768 | `int16` | `int16` | shared `int16[768]` |
| Directed threat edge | 66,864 | 768 | `int8` | `int16` | zero |

There are two sparse feature systems, not two independent final networks.
Their accumulator values are summed before activation
(`src/nnue/forward/vectorized.rs:16-48`).

### Perspective layout

Each per-POV accumulator is 768 `int16` values:

```text
offset 0 bytes:      left half,  384 x int16 = 768 bytes
offset 768 bytes:    right half, 384 x int16 = 768 bytes
row stride:          768 values
```

The pairwise product yields 384 bytes per POV. Runtime ordering is:

```text
output[0..384]   = current side-to-move POV
output[384..768] = non-side-to-move POV
```

This is confirmed by `activate_ft` in
`src/nnue/forward/vectorized.rs:10-52`.

### Dense head and buckets

The eight output buckets are selected by occupied-square count, including
kings:

| Bucket | Occupied squares |
|---:|---|
| 0 | 0-8 |
| 1 | 9-12 |
| 2 | 13-16 |
| 3 | 17-19 |
| 4 | 20-22 |
| 5 | 23-25 |
| 6 | 26-28 |
| 7 | 29-32 |

Only the selected bucket is evaluated
(`src/nnue.rs:286-300`).

### Parameters and deployed size

| Section | Parameters | Bytes |
|---|---:|---:|
| Threat weights | 51,351,552 | 51,351,552 |
| Piece weights | 5,898,240 | 11,796,480 |
| Sparse bias | 768 | 1,536 |
| First-head weights | 98,304 | 98,304 |
| First-head bias | 128 | 512 |
| Second-head weights | 4,096 | 16,384 |
| Second-head bias | 256 | 1,024 |
| Output weights | 256 | 1,024 |
| Output bias | 8 | 32 payload, 64 aligned bytes |
| **Total** | **57,353,608** | **63,266,880** |

The deployed image is 60.336 MiB.

### Raw parameter-image offsets

The parameter image is a direct `#[repr(C)]` memory image, with every field
wrapped in 64-byte alignment:

| Section | Offset | Stored bytes |
|---|---:|---:|
| Threat weights | 0 | 51,351,552 |
| Piece weights | 51,351,552 | 11,796,480 |
| Sparse bias | 63,148,032 | 1,536 |
| First-head weights | 63,149,568 | 98,304 |
| First-head bias | 63,247,872 | 512 |
| Second-head weights | 63,248,384 | 16,384 |
| Second-head bias | 63,264,768 | 1,024 |
| Output weights | 63,265,792 | 1,024 |
| Output bias/alignment | 63,266,816 | 64 |

Reckless has no runtime versioned network loader. `build/build.rs:58-65`
selects the model path, and `src/nnue.rs:369-382` uses
`include_bytes!` plus a compile-time transmute into `Parameters`.

This format has no magic, section table, dimension metadata, checksum, or
runtime compatibility checks. It is not suitable as a replacement for
MagnusChess format 4.

## 6. Complete Reckless move-to-evaluation flow

### Normal quiet move

1. `search::make_move` increments the node count, calls
   `Network::push`, then calls `Board::make_move` with the network as observer
   (`src/search.rs:1416-1429`).
2. `Network::push` advances the network ply, stores the PST move/piece/capture
   delta, invalidates both sparse-family accumulators, and clears the threat
   delta list (`src/nnue.rs:169-181`).
3. The board removes/adds the moved piece and calls
   `observer.on_piece_move` (`src/board/makemove.rs:71-75`).
4. The network observer computes local directed-edge removals/additions and
   appends packed `u32` deltas (`src/nnue.rs:342-353`,
   `src/nnue/accumulator/threats/vectorized.rs:130-170`).
5. The board separately rebuilds search legality/threat bitboards using
   setwise attacks and king-ray loops
   (`src/board.rs:408-479`). These board maps are not the NNUE threat feature
   state.
6. If search obtains a usable TT raw evaluation, NNUE evaluation is skipped.
   Otherwise `Network::evaluate` lazily finds the nearest accurate ancestor
   for each family and perspective (`src/nnue.rs:197-240`).
7. PST replay copies the parent 768-wide accumulator while applying one
   add and one remove row (`src/nnue/accumulator/psq.rs:105-164`).
8. Threat replay transforms packed deltas into feature indices for the current
   POV, then copies the parent 768-wide accumulator while fusing add/remove
   rows (`src/nnue/accumulator/threats.rs:141-220`).
9. The two family accumulators are summed, activated, packed to 768 bytes, and
   passed through the selected head (`src/nnue.rs:286-300`).
10. Unmake calls `Network::pop` before board undo. `pop` only decrements the
    network index (`src/search.rs:1432-1435`, `src/nnue.rs:183-185`).

### Update-model classification

| Family | Normal update model | Full refresh cause |
|---|---|---|
| Piece/king bucket | Direct move-event delta, lazily replayed into a per-ply full accumulator | Own king crosses horizontal mirror side or input bucket |
| Threat edge | Direct board-observer edge delta with local ray recomputation, lazily replayed into a per-ply full accumulator | Own king crosses horizontal mirror side |

There is no parent/child active-set diff and no binary refcount structure.
Threat semantics contain one feature per supported directed occupied attack
edge, so each semantic edge is naturally addressable without deduplicating
multiple summary slots.

### Special moves

- **Capture:** board emits a remove at the source and a target identity mutation;
  the PST delta removes the captured piece
  (`src/board/makemove.rs:62-70`,
  `src/nnue/accumulator/psq.rs:126-133`).
- **Promotion:** target identity mutation removes pawn relations and adds
  promoted-piece relations; PST uses promoted type at destination
  (`src/board/makemove.rs:87-95`,
  `src/nnue/accumulator/psq.rs:108-111`).
- **En passant:** move callback plus captured-pawn change callback; PST removes
  the pawn on `to ^ 8`
  (`src/board/makemove.rs:76-80`,
  `src/nnue/accumulator/psq.rs:122-125`).
- **Castling:** rook remove, king move, rook add callbacks; PST adds/removes both
  king and rook rows (`src/board/makemove.rs:51-61`,
  `src/nnue/accumulator/psq.rs:113-121`).
- **Null move:** the board toggles side to move and updates search threat maps,
  but the NNUE stack is not pushed. Sparse semantics are unchanged; only
  STM/NTM activation ordering changes
  (`src/board/makemove.rs:18-28`,
  `src/nnue/forward/vectorized.rs:16-18`).

### Normal and worst-case work

Piece update, per perspective:

- normal quiet: one added row, one removed row, and one 768-`int16` parent
  accumulator read/write;
- castling: two adds and two removes;
- capture/EP: one add and two removes;
- worst refresh: cache diff over six piece types and two colours, with up to 64
  additions/removals.

Threat update, per perspective:

- normal: proportional to local outgoing edges, incoming attackers, and changed
  slider x-rays;
- packed delta capacity: 80 entries;
- each replay copies one 768-`int16` parent accumulator and applies the
  transformed rows;
- worst refresh: scans all occupied pieces and their occupied attack targets.

## 7. Accumulator ownership and memory

### Reckless

`Network::new` allocates 240 PST accumulators and 240 Threat accumulators
(`src/nnue.rs:142-166`, `src/types/mod.rs:25-26`).

Measured with the same Rust layout declarations and compiler ABI:

| Object | Bytes |
|---|---:|
| `PstAccumulator` | 3,136 |
| `ThreatAccumulator` | 3,456 |
| Both families per ply | 6,592 |
| 240-ply accumulator stacks | 1,582,080 |
| 40-entry piece refresh cache | 64,000 |
| 256-entry NNZ lookup table | 8,192 |
| **NNUE-owned payload per thread** | **1,654,272 (1.578 MiB)** |

The current numeric data required to update one child consists of a parent and
child pair. The per-ply numeric accumulator payload is 6,144 bytes:

```text
2 families x 2 POVs x 768 x int16 = 6,144 bytes
```

The remaining 448 bytes in the 6,592-byte per-ply pair are deltas, flags, and
alignment.

Weights are immutable and shared through `Arc<ParametersHandle>`, with optional
NUMA replication (`src/nnue.rs:387-425`). Semantic threat state is not stored
separately from numeric accumulators; the per-ply packed delta list is the
semantic update record.

### MagnusChess

The current format-4 runtime reports:

```text
accumulator_stack_bytes 3,457,280
accumulator_stack 3.29712 MiB
```

`SemanticState` owns:

- two-perspective Position/Attack/Structure slots and refcounts;
- one current two-perspective `int32` accumulator;
- AttackGraph;
- 132 frames;
- 131,072 slot undo entries;
- 131,072 row deltas;
- 16,384 direct eval-cache entries.

The containing `AccumulatorStack::Impl` also retains the older 132-state broad
refresh stack (`src/mnue/MnueV2Network.cpp:1635-1692`,
`3449-3474`). That storage is not the normal semantic hot path but contributes
to per-thread memory.

### Depth-first search trade-off

**Confirmed:** Reckless unmake performs no neural row work. MagnusChess
materialised unmake applies inverse row deltas
(`src/mnue/MnueV2Network.cpp:3386-3443`).

**Hypothesis:** at MagnusChess's measured 0.755 static-eval calls per node and
high Attack row count, a per-materialised-ply accumulator stack or hybrid
checkpoint stack is likely cheaper than reversible mutation. It would add
about 6,144 bytes per stored materialised ply but remove inverse row reads and
accumulator writes.

MagnusChess's current single-accumulator design remains preferable for chains
of moves that are never evaluated. The correct A/B candidate is therefore
lazy child materialisation into a stack slot, not unconditional accumulator
copying on every make.

## 8. Dual-perspective row layout

### Reckless confirmed layout

Reckless does not store perspective pairs contiguously or interleaved.

- Piece row stride: `768 x int16 = 1,536` bytes.
- Threat row stride: `768 x int8 = 768` bytes.
- A feature transition is transformed separately for White and Black POV.
- `Network::evaluate` calls each family update separately for each POV
  (`src/nnue.rs:201-214`).
- PST and Threat update kernels do not update both POVs in one loop.
- There is no explicit sparse-row prefetch in the accumulator kernels.
- Add and remove rows are fused inside one POV update.

PST perspective transformation is in
`src/nnue/accumulator/psq.rs:204-210`; threat transformation is in
`src/nnue/accumulator/threats/threat_index.rs:103-116`.

### MagnusChess layout

| Branch | Row stride | Weight |
|---|---:|---|
| Position | 640 bytes | 320 x `int16` |
| Attack | 288 bytes | 288 x `int8` |
| Structure | 320 bytes | 160 x `int16` |

MagnusChess also emits perspective-specific feature IDs and row deltas. Its
runtime prefetches three cache lines of future rows and fuses adjacent
remove/add pairs (`src/mnue/MnueV2Network.cpp:1843-1882`,
`2058-2155`).

### Consequence

Reckless provides no evidence that a paired-perspective storage format is the
source of its speed. MagnusChess already has the stronger sparse-row dispatch
features here: prefetch, mixed-table dispatch, packed branch/perspective IDs,
and fused remove/add pairs.

A paired-perspective Magnus format could reduce dispatch and address
generation, but it would duplicate or reorganise rows and requires its own
benchmark. It is not a directly transferable Reckless mechanism.

## 9. Sparse update kernels

### Piece `int16` kernel

`PstAccumulator::apply_delta`:

```text
for each SIMD block in 768:
    v = load(parent accumulator)
    for add row: v += load(int16 row)
    for sub row: v -= load(int16 row)
    store(child accumulator)
```

Source: `src/nnue/accumulator/psq.rs:140-164`.

AVX2 processes 16 `int16` lanes. AVX-512 processes 32. Arithmetic is
non-saturating. Row alignment follows the 64-byte aligned parameter image.

### Threat `int8` kernel

`ThreatAccumulator::update`:

```text
transform packed semantic deltas to POV feature IDs
split IDs into adds and removes

for each register-block in 768:
    load parent accumulator block
    while add and remove exist:
        delta = sign_extend(int8 add) - sign_extend(int8 remove)
        accumulator += delta
    process remaining adds/removes
    store child accumulator block
```

Source: `src/nnue/accumulator/threats.rs:141-220`.

| ISA | Row widening | Accumulator | Blocking |
|---|---|---|---|
| Scalar | scalar sign extension | `int16` | scalar |
| AVX2 | 16 `int8` via `_mm256_cvtepi8_epi16` | 16 x `int16` | 8 registers, 128 values/block |
| AVX-512 | 32 `int8` via `_mm512_cvtepi8_epi16` | 32 x `int16` | 24 registers, all 768 values |

The AVX2 primitives are in `src/nnue/simd/avx2.rs:8-49`; AVX-512 primitives
are in `src/nnue/simd/avx512.rs:9-50`.

### Comparison with MagnusChess

MagnusChess:

- accumulates into `int32`, not `int16`;
- dispatches mixed branch widths;
- supports prefetch four deltas ahead;
- fuses opposite-sign adjacent rows;
- mutates one accumulator in place and later performs inverse updates.

Reckless:

- copies parent to child while applying rows;
- uses narrower `int16` accumulator arithmetic;
- keeps fixed 768-wide compile-time loops;
- does no neural work on unmake.

The most concrete kernel-level difference is not a better Reckless widening
sequence. It is ownership: Reckless combines child copy and delta application,
then retains the child accumulator.

## 10. Threat-frontier maintenance

### Reckless confirmed semantics

The 66,864-row Threat table models supported directed relations:

```text
attacking piece identity
attacking square
target piece identity
target square
POV transform
horizontal king-side mirror
```

It does not encode MagnusChess's:

- attacked/defended status summary;
- own/enemy attacker counts;
- hanging/overloaded flags;
- lower-value-attacker flag;
- pin status as a separate summary;
- king-zone aggregate pressure buckets;
- Structure features.

The interaction exclusions and compact index maps are established in
`src/nnue/accumulator/threats/threat_index.rs:33-100`.

### Event-local frontier

For a board mutation, Reckless computes:

- outgoing occupied targets of the changed piece;
- incoming pawn, knight, king, bishop, rook, and queen attackers;
- first relevant sliders on each ray;
- the x-ray edge exposed or hidden behind the changed square.

Scalar source:

- `src/nnue/accumulator/threats/scalar.rs:18-55`;
- identity mutation: `57-88`.

AVX2 source:

- eight-ray permutation and closest occupied square:
  `src/nnue/accumulator/threats/vectorized/avx2.rs:17-115`;
- packed delta emission: `117-178`.

AVX-512 uses VBMI2 compression to write packed deltas in batches
(`src/nnue/accumulator/threats/vectorized/avx512.rs:60-143`).

### Full-board work

In the normal NNUE move path, there is no 64-square threat-feature loop.
The board itself does recompute search threat/legality maps after every move,
but it uses setwise attacks and loops only candidate king-ray sliders
(`src/board.rs:408-479`).

### MagnusChess remaining frontier cost

MagnusChess already maintains:

- `attacks_from[64]`;
- `attackers_to[64]`;
- local affected-source discovery;
- changed-edge updates;
- direct edge slots.

However, every dirty occupied target still scans its complete incoming attacker
bitboard to reconstruct status and relation flags
(`src/mnue/MnueV2Network.cpp:2686-2718`,
`src/mnue/MnueV2Features.cpp:998-1025`).

Pressure slots scan every piece of a dirty type and popcount its attacks into
the king ring (`src/mnue/MnueV2Features.cpp:771-804`).

### Transferable design

Reckless does not contain persistent typed counters. Therefore the following is
an independently derived MagnusChess change, not a source transplant:

```text
changed edge (source, target, add/remove)
    -> update per-target typed incoming counters
    -> update own/enemy count and lower-value counters
    -> update compact status/relation signature
    -> refresh the two summary slots only if signature changes

changed edge entering/leaving king ring
    -> update pressure counter by relative piece type
    -> refresh pressure slot only on bucket/signature transition
```

This preserves MagnusChess's richer trainer semantics while adopting the same
event-driven complexity principle.

## 11. Dense-head data flow

### Reckless

1. Sum PST and Threat accumulator vectors.
2. Pairwise activate and pack a contiguous `u8[768]`.
3. Scan 192 four-byte chunks and build a list of nonzero chunk indices.
4. Evaluate only those chunks against selected-bucket `int8` weights.
5. Produce 16 float activations.
6. Evaluate float 16-to-32 and 32-to-1 layers.

The first head weight layout is input-chunk-major:

```text
bucket
  -> four-byte input chunk
     -> 16 outputs
        -> four int8 weights
```

This follows pointer calculation
`index * L2_SIZE * 4` in
`src/nnue/forward/vectorized.rs:55-95`.

AVX2 emulates unsigned-byte x signed-byte dot products with:

```text
maddubs
maddwd with ones
add_epi32
```

Source: `src/nnue/simd/avx2.rs:76-87`.

AVX-512 uses `dpbusd` when AVX-512 VNNI is enabled
(`src/nnue/simd/avx512.rs:77-100`).

### Approximate first-layer work

Maximum dense arithmetic:

- 192 four-byte input chunks;
- 16 outputs;
- 3,072 byte multiplications.

Actual work is lower when four-byte chunks are all zero. AVX2 handles two
nonzero chunks together. AVX-512 handles all 16 outputs in one vector.

### MagnusChess

MagnusChess uses:

- 768-to-32 first head;
- floating-point activations;
- `int16` head weights converted to float;
- transposed input-major weights;
- separate Position, Attack, and Structure first-layer partial caches.

The AVX2 first partial loops over every input column, broadcasts one float,
loads four groups of eight `int16` weights, converts them to float, and
multiplies/adds (`src/mnue/MnueV2Network.cpp:1176-1224`).

Maximum first-layer arithmetic is 24,576 scalar-equivalent multiplications,
eight times Reckless's 3,072 byte products before considering Reckless NNZ
skipping. MagnusChess also has twice as many first-head outputs: 32 versus 16.

### Confirmed avoidable Magnus work

`forward_cached` builds the complete 768-float activated input before checking
branch cache epochs (`src/mnue/MnueV2Network.cpp:3183-3238`). Only afterward
does it skip reusable branch partials (`3259-3304`).

Therefore repeated evaluation still pays:

- pairwise activation for all three branches;
- integer-to-float conversion;
- clipping and squaring;
- stores of all 768 floats;

even when one or more first-layer branch partials are valid.

### Transfer classification

| Mechanism | Directly usable? | Reason |
|---|---|---|
| Selected bucket only | Already implemented | Both engines do it. |
| Input-major/transposed first head | Already implemented | Magnus transposes at load. |
| Skip unchanged branch activation | Yes | Format-compatible; move activation inside dirty-branch path. |
| Fuse branch activation with first partial | Yes | Format-compatible if arithmetic order/tolerance is tested. |
| `u8 x int8` first head | No, not immediately | Requires exporter, format, scale, and parity changes. |
| Four-byte NNZ chunk skipping | No, not immediately | Depends on byte activation and int8 head contract. |
| AVX-512/VNNI head | Conditional | Requires compiler/runtime/backend policy and quantised head. |

## 12. Search evaluation-call behaviour

### Reckless source facts

- Search performs root full refresh once
  (`src/search.rs:55-60`).
- Main search performs TT/tablebase cutoffs before static evaluation.
- In-check nodes skip static evaluation.
- A TT entry's raw evaluation is reused when valid; otherwise NNUE is called
  (`src/search.rs:446-466`).
- Quiescence performs its early TT cutoff before stand-pat evaluation and also
  reuses TT raw evaluation (`src/search.rs:1204-1246`).
- The TT stores an `i16` raw evaluation
  (`src/transposition.rs:68-76`).

There is no active source instrumentation for eval calls, accumulator updates,
or refresh frequency. Those ratios cannot be measured without changing
Reckless, which was prohibited.

### MagnusChess measured ratio

Existing telemetry binary, fixed 13-position one-thread benchmark:

```text
nodes             733,951
static eval calls 553,976
full head calls   553,976
TT eval reuse     114,496
evals/node        0.754786
```

MagnusChess resolves a static evaluation for most non-cutoff nodes. Its direct
16,384-entry MNUEv2 eval cache had no visible reduction in this run because
`full_head_calls == static_eval_calls`.

Raw Reckless and MagnusChess search NPS must not be interpreted as evaluator
throughput because their search trees, pruning, TT policy, and node accounting
differ.

## 13. MagnusChess cycle evidence

Command used with the existing telemetry build:

```powershell
@(
  'setoption name MNUEfile value F:\MagnusChess\tmp\mnue_v2_fake_quant\dry-run.mnue',
  'isready',
  'mnuev2cycles reset',
  'bench',
  'mnuev2cycles report',
  'quit'
) | F:\MagnusChess\src\build_telemetry\MagnusChess.exe
```

Measured:

| Counter | Events | Sampled cycles/event | Derived frequency |
|---|---:|---:|---:|
| Selected head | 553,976 | 22,807 | 0.755/node |
| Semantic make/update | 723,957 | 506 | 0.986/node |
| Semantic unmake | 723,957 | 7,622 | 0.986/node |
| Row application | 73,582,340 | 205 | 100.3 events/node |

Telemetry build search rate was 56,284 NPS. This is not the production NPS:
the sampled scopes materially perturb the hot path.

The scopes are nested. Row-application time is included inside materialisation
or unmake, so these rows must not be summed as independent percentages.

Still, the result confirms:

- selected-head work is large;
- materialised unmake is much more expensive than semantic make;
- sparse row application occurs at very high frequency;
- removed full reconstruction, deduplication, and set-diff paths recorded zero
  events.

The normal non-telemetry baseline supplied for this comparison is:

```text
247,808
250,479
253,026
median 250,479 NPS
```

## 14. Reckless build and compiler engineering

### Confirmed

- Release uses fat LTO, one codegen unit, and aborting panic
  (`Cargo.toml:21-24`).
- Native local build uses `-Ctarget-cpu=native`
  (`Makefile:1`, `23-24`).
- Release builds are separately compiled for generic x86-64, x86-64-v3 AVX2,
  and x86-64-v4 plus AVX-512 features
  (`.github/workflows/release.yml:15-40`).
- PGO workload is the built-in fixed benchmark
  (`Makefile:40-43`, `.github/workflows/pgo.yml:21-30`).
- ISA selection is compile-time, not a single runtime-dispatched binary
  (`src/nnue.rs:17-79`).
- Fixed dimensions are constants, enabling unrolled and monomorphic loops.

The measured local Reckless binary reported:

```text
rustc 1.95.0
target x86_64-pc-windows-gnu
features include AVX2, AVX-VNNI, BMI2, FMA
```

The current AVX2 dense code does not use AVX-VNNI; it uses
`maddubs/maddwd`. VNNI use is implemented for AVX-512 VNNI.

### MagnusChess comparison

MagnusChess already uses:

- `-march=native -mtune=native`;
- `-Ofast`;
- LTO;
- no exceptions/RTTI;
- loop unrolling and vectorisation;
- PGO generation/use targets.

Source: `F:\MagnusChess\src\Makefile:85-135`, `186-202`,
`315-330`.

Compiler flags are not the primary 4x gap. A fresh profile remains required
because the execution model changed. PGO should be applied only after the next
semantic/head changes settle.

## 15. Read-only Reckless measurement

No source build was run because it would write into the read-only reference
repository. The existing release binary was executed.

Command:

```powershell
@('compiler', 'bench', 'quit') |
  D:\Reckless-main\Reckless-main\target\release\reckless.exe
```

Built-in bench conditions:

- 50 fixed positions;
- depth 12;
- one thread;
- 16 MiB hash;
- 3,204,302 nodes.

Three runs:

| Run | NPS |
|---:|---:|
| 1, cold | 907,213 |
| 2 | 1,023,160 |
| 3 | 1,027,448 |
| **Median** | **1,023,160** |
| **Full range** | **907,213-1,027,448** |

This is evidence that the complete Reckless engine reaches roughly 1.0M NPS
on this machine and binary. It is not an evaluator-to-evaluator comparison
with MagnusChess's 13-position one-second benchmark.

Reckless has no isolated sparse-update, head, or evaluator benchmark command.
Without modifying Reckless, the following could not be measured:

- cycles per threat transition;
- isolated accumulator updates/s;
- isolated dense-head calls/s;
- accumulator refresh rate;
- eval calls/node.

## 16. Side-by-side comparison

| Property | Reckless | MagnusChess MNUEv2 |
|---|---|---|
| Sparse families | 2 | 3 |
| Sparse semantics | piece-square + directed occupied attack edge | Position + rich Attack + Structure |
| Sparse vocabularies | 7,680; 66,864 | 12,288; 153,216; 34,685 |
| Raw row widths | 768; 768 | 320; 288; 160 |
| Sparse weights | piece `int16`, threat `int8` | Position `int16`, Attack `int8`, Structure `int16` |
| Accumulator | `int16` | `int32` |
| Family combination | elementwise sum before activation | nonlinear branch activation, then concatenation |
| Final head input | 768 `u8` | 768 `float` |
| Head | 768x16x32x1 | 768x32x32x1 |
| Output buckets | 8 occupancy buckets | 12 material buckets |
| Selected bucket only | yes | yes |
| First head weights | `int8` | `int16` |
| First head sparsity | zero four-byte chunks skipped | none |
| Perspective row storage | separate transformed row lookup | separate transformed row lookup |
| Accumulator ownership | full state per ply, lazy replay | one current state, lazy rows, inverse unmake |
| Normal feature discovery | direct move observer | dirty slots + AttackGraph |
| Tactical summaries | absent from network | recomputed for dirty targets |
| Binary refcounts | no | yes |
| Row prefetch | no explicit sparse prefetch | four-delta lookahead |
| Add/remove fusion | yes | yes |
| Branch partial cache | no | yes |
| Direct eval cache | TT raw eval | TT raw eval + 16K MNUEv2 cache |
| Runtime network loader | no; compile-time raw image | strict versioned format-4 loader |
| Network size | 60.336 MiB | 60.760 MiB mixed Attack-int8 |
| NNUE per-thread payload | 1.578 MiB | 3.297 MiB reported |

The similar network sizes are misleading. Reckless spends most parameters on
a 768-wide edge table but has simpler feature discovery and a much cheaper
first dense layer.

## 17. Reusable, format-changing, and incompatible techniques

### Directly reusable without feature or format changes

1. Lazy full accumulator child slots or hybrid materialised checkpoints.
2. Direct typed-counter updates from changed AttackGraph edges.
3. Compact per-target status/relation signatures.
4. Persistent king-ring pressure counters.
5. Skip activation construction for cached branch partials.
6. Fuse dirty-branch activation with first-head partial calculation.
7. Compile-time specialised 320/288/160 kernels.
8. Fresh representative PGO and cold-path outlining.

### Require format/export changes

1. `u8` packed head input.
2. `int8` 768-to-32 first-head weights.
3. four-byte NNZ chunk index list.
4. AVX2/VNNI byte-dot-product head.
5. paired-perspective row storage, if independently benchmarked.

### Incompatible with current MNUEv2 semantics

1. Summing Position, Attack, and Structure accumulators.
2. Removing tactical status/relation/pressure features.
3. Replacing 12 material buckets with occupancy buckets.
4. Reducing the first head from 32 to 16.
5. Using Reckless's `int16` accumulator without a Magnus-specific bound proof.
6. Replacing the strict versioned file with a native parameter-memory image.

## 18. Ranked transferable optimisation table

Gain estimates are hypotheses against the 250,479 NPS median and must be
validated independently.

| Technique | Reckless implementation | MagnusChess current implementation | Required MagnusChess change | Estimated gain | Risk | Priority |
|---|---|---|---|---:|---|---:|
| Lazy child accumulator stack | Full accumulator per ply; pop only decrements index | Single accumulator; inverse rows on materialised unmake | Materialise evaluated child into stack/checkpoint slot; no inverse rows | 1.20-1.40x | Medium | 1 |
| Persistent tactical counters/signatures | Not needed because Threat is edge-only | Dirty target rescans all incoming attackers | Maintain typed incoming counts, own/enemy counts, lower-value count, compact signature | 1.15-1.40x | High | 2 |
| Persistent king-pressure counters | Threat network has no pressure summary | Dirty type rescans all pieces of that type | Update ring contribution on changed edges and king movement | 1.03-1.12x | Medium | 3 |
| Skip cached-branch activation | No branch cache | Builds all 768 activations before cache check | Build/fuse only dirty branch input | 1.08-1.20x | Low-Medium | 4 |
| Fuse activation + head partial | Compact byte activation then sparse head | Float scratch then transposed partial | Direct activated value into 32 output accumulators | 1.05-1.15x | Medium | 5 |
| Quantised sparse first head | `u8 x int8`, NNZ chunks, 16 outputs | `float x int16`, dense, 32 outputs | New exporter/format/backend for 32 outputs | 1.25-1.60x head-path; 1.12-1.35x search | High | 6 |
| Compile-time width specialisation | Separate ISA build, constant 768 loops | Constant widths but runtime backend checks and mixed dispatch | Hoist dispatch; specialised branch functions; inspect generated code | 1.02-1.08x | Low | 7 |
| Fresh PGO | Bench-trained release PGO | PGO supported; prior profile stale | Rebuild profile with fixed 13-position search plus evaluator microbench | 1.05-1.15x | Low | 8 |
| Paired-perspective rows | Not implemented | Not implemented | New row layout/format | Unknown, likely small | High | Reject pending evidence |
| Copy Reckless ray SIMD | AVX2/VBMI2 local mailbox rays | Persistent AttackGraph and bitboard rays | Clean-room alternative only if profiling proves source discovery dominant | Unknown | High/license | Defer |

## 19. Proposed MagnusChess patch plan

This is a plan only. No patch was implemented.

### Stage A: accumulator ownership and low-risk hot-path cleanup

Files:

- `src/mnue/MnueV2Network.h`
- `src/mnue/MnueV2Network.cpp`
- `src/mnue/MnueV2Telemetry.h`
- `src/mnue/MnueV2Telemetry.cpp`
- `src/Bench.cpp`
- `src/Uci.cpp`

Changes:

1. Add an experimental lazy materialised accumulator stack:
   - 6,144-byte numeric state per materialised ply;
   - parent pointer/index and branch epochs;
   - copy parent only when the child is evaluated;
   - apply pending child rows once;
   - unmake drops the child slot without inverse row application.
2. Retain the current reversible mode behind a compile-time or debug switch for
   exact A/B parity.
3. Hoist scalar/AVX2 selection outside row loops.
4. Measure whether dormant broad-refresh `states[132]` can be moved to test-only
   storage; this is primarily a memory change.

Format impact: none.

Required tests:

- all existing golden, random, special-move, and AVX2 parity gates;
- evaluated and unevaluated child chains;
- ancestor not evaluated, descendant evaluated;
- bucket transition make/unmake;
- repeated eval and TT-eval reuse;
- accumulator stack overflow/fallback.

Expected telemetry:

- semantic-unmake cycles should collapse;
- inverse row bytes should reach zero;
- total row-application events should fall substantially;
- pending chain distribution should remain unchanged.

Estimated cumulative NPS: 0.30M-0.36M.

Rollback boundary: one ownership strategy flag and its stack storage.

### Stage B: Attack counters and compact signatures

Files:

- `src/mnue/MnueV2Features.h`
- `src/mnue/MnueV2Features.cpp`
- `src/mnue/MnueV2Network.cpp`
- `src/mnue/MnueV2Network.h`
- `src/Bench.cpp`

Proposed state:

```text
IncomingAttackState[64]:
    count by absolute colour and piece type
    own/enemy totals for current occupant
    lower-value enemy count/signature
    defended/status typed mask

PressureState[perspective][relative piece type]:
    exact king-ring attacked-square count
    exported pressure bucket/signature
```

Changes:

1. On every changed AttackGraph edge, increment/decrement typed target counters.
2. On target identity change, reinterpret the existing counters without
   scanning attackers.
3. Update status/relation slots only when the compact signature changes.
4. Update pressure counters from edge ring entry/exit.
5. Keep pin/x-ray geometry as dirty king-ray work.
6. Retain a full recomputation invariant checker in debug/tests.

Format impact: none.

Required tests:

- counter state equals full `attackers_to` scan after every make/unmake;
- signature-to-feature equality for both perspectives;
- target identity change with stable edge;
- king move/ring relocation;
- pin and x-ray changes;
- all existing 1,000 x 100-ply random walks.

Expected telemetry:

- incoming-attacker bit scans per dirty target approach zero;
- tactical summary transitions remain identical;
- Attack semantic cycles fall;
- emitted rows remain unchanged.

Estimated cumulative NPS after A+B: 0.42M-0.58M.

Rollback boundary: counter/signature state can be disabled while retaining the
current canonical slot functions.

### Stage C: selected-head execution

Files:

- `src/mnue/MnueV2Network.cpp`
- `src/mnue/MnueV2Network.h`
- `tools/mnue_v2_trainer/src/export.rs` only for the optional format-changing
  substage
- `docs/MNUE_V2_FORMAT.md` only for the optional format-changing substage

Format-compatible changes:

1. Check branch epochs before building branch activations.
2. Build only dirty branch activations.
3. Fuse pairwise activation with transposed first-head accumulation.
4. Keep exact scalar path and define the accepted floating-point tolerance.
5. Preserve 12 selected-only heads and all dimensions.

Optional format-changing substage:

1. Export first-head weights as `int8`.
2. Define packed activation scale and exact rounding.
3. Add AVX2 `maddubs/maddwd` and optional AVX-VNNI/AVX-512 VNNI paths.
4. Add NNZ four-byte chunk discovery if activation sparsity justifies it.
5. Increment the format version and reject old/new mismatches explicitly.

Required tests:

- branch cache combinations;
- same-position repeated eval;
- bucket transitions;
- scalar/AVX2 exact or documented-tolerance parity;
- exporter round trip;
- saturation and error statistics.

Expected telemetry:

- selected-head cycles fall;
- branch activation bytes written fall;
- partial-cache hit rate becomes meaningful at activation level.

Estimated cumulative NPS:

- format-compatible C: 0.52M-0.72M;
- quantised first head: 0.68M-0.92M before PGO.

Rollback boundary: separate scalar/current-head and experimental-head backend
selection.

### Stage D: PGO and layout

Files:

- `src/Makefile`
- benchmark scripts/commands only as needed
- hot implementation files for attributes/outlining

Changes:

1. Generate a fresh profile after A-C.
2. Profile the fixed 13-position one-thread benchmark and evaluator/update
   microbenchmarks.
3. Outline loader, diagnostics, reference evaluator, corruption checks, and
   telemetry printing from hot translation paths where the compiler does not.
4. Inspect assembly before adding manual prefetch or alignment changes.

Format impact: none.

Required tests: complete existing suite on non-PGO and PGO binaries.

Estimated cumulative NPS:

- format-compatible A-D: 0.57M-0.82M;
- with quantised first head: 0.72M-1.00M.

Rollback boundary: PGO build target and isolated hot/cold annotations.

## 20. Recommended implementation order

1. Add precise non-overlapping materialisation and unmake byte/cycle counters.
2. Implement lazy child accumulator slots and measure inverse-row removal.
3. Add typed incoming counters with full-scan invariant checks.
4. Add pressure counters.
5. Stop constructing activations for cached branches.
6. Fuse dirty-branch activation and first partial.
7. Re-run the fixed three-run benchmark and all parity gates.
8. Decide whether format-compatible work can reach 0.8M.
9. Only then consider a versioned quantised first-head export.
10. Generate fresh PGO last.

## 21. Final assessment

### Confirmed

- Reckless's speed is not explained by paired-perspective row layout.
- Its network semantics are materially cheaper than MagnusChess Attack +
  Structure semantics.
- It eliminates neural unmake work through per-ply full accumulators.
- It uses direct event-local threat-edge deltas.
- It has a much cheaper, sparse, quantised first dense layer.
- MagnusChess still pays to activate all branches before consulting its partial
  cache.
- MagnusChess telemetry identifies selected-head and materialised-unmake/row
  work as major costs.

### Likely design intent

- Reckless spends approximately 1.58 MiB per thread to make accumulator
  ancestry and unmake trivial.
- Its compile-time parameter image and separate ISA binaries maximise
  constant propagation and avoid runtime loader/dispatch overhead.
- Its threat feature design deliberately moves tactical expressiveness into a
  very wide edge table while keeping runtime semantic discovery local.

### Hypotheses requiring benchmark

- A lazy child accumulator stack will gain 20-40%.
- Persistent tactical counters/signatures will gain 15-40%.
- Activation-aware branch caching/fusion will gain 8-20%.
- Format-compatible changes alone can reach the upper 0.7M to low 0.8M range.
- A validated int8 first head is the most credible path to make 0.8M robust
  rather than marginal.

The 0.8M target is technically realistic, but Reckless is not a drop-in
blueprint. Its strongest advantages must be translated to MagnusChess's richer
feature semantics and strict versioned format rather than copied.
