# Syzygy Tablebase Integration

MagnusChess probes local Syzygy tablebases through Fathom.

## UCI Options

- `SyzygyPath`: directory containing `.rtbw` and optional `.rtbz` files.
- `SyzygyProbeLimit`: maximum piece count to probe. `0` disables probing.
- `SyzygyProbeDepth`: minimum search depth when the position has exactly the
  configured probe-limit piece count.
- `Syzygy50MoveRule`: applies the 50-move rule to root DTZ/WDL ranking.

## Search Behavior

- Root positions use DTZ ranking when available and fall back to WDL if DTZ
  files are missing.
- Any castling right excludes the root from Syzygy, even when castling will be
  lost on the next move. In that case `tbhits` can still come from eligible
  internal nodes, but the root score and PV remain products of normal search.
- `go searchmoves` remains authoritative; root tablebase ranking only considers
  allowed moves.
- Internal search probes WDL positions without castling rights and with a zero
  halfmove clock. Successful exact or cutoff probes are saved in the TT.
- At PV nodes, non-cutting WDL lower and upper bounds tighten the search window
  and cap the returned score, matching Stockfish's tablebase pruning behavior.
- Decisive tablebase PVs are validated against the best WDL/DTZ rank. The
  invalid suffix is removed, then DTZ-ranked moves extend the line toward mate.
  Equal-DTZ moves prefer lower opponent mobility while avoiding 50-move draws
  and threefold repetitions.
- UCI `info` lines report aggregate `tbhits`, including Lazy SMP workers.
- UCI scores distinguish tablebase wins from proven mates. Following
  Stockfish, a root TB win/loss is displayed in the `cp +/-20000` range;
  `score mate N` is reserved for a mate distance proven by search.

## Third-Party Code

The probe implementation is Fathom commit
`c9c6fef0dddc05d2e242c183acf5833149ab676d`, vendored under
`third_party/fathom`. See its `LICENSE` and `README.md`.
