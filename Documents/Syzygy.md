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
- `go searchmoves` remains authoritative; root tablebase ranking only considers
  allowed moves.
- Internal search probes WDL positions without castling rights and with a zero
  halfmove clock. Successful exact or cutoff probes are saved in the TT.
- UCI `info` lines report aggregate `tbhits`, including Lazy SMP workers.

## Third-Party Code

The probe implementation is Fathom commit
`c9c6fef0dddc05d2e242c183acf5833149ab676d`, vendored under
`third_party/fathom`. See its `LICENSE` and `README.md`.
