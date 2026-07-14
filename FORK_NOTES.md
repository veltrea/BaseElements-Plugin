# About this fork

日本語版: [FORK_NOTES.ja.md](FORK_NOTES.ja.md)

This is a fork of the [BaseElements Plugin](https://github.com/GoyaPtyLtd/BaseElements-Plugin)
by Goya Pty Ltd. All credit for the plugin, its function set, and its documentation belongs to
the upstream authors. This fork exists to serve two specific needs that are out of scope for
upstream, and it stays as close to upstream at the API level as possible.

## Why this fork exists

### 1. 32-bit Windows support

Upstream removed 32-bit Windows (`.fmx`) build support in October 2016 (between v3.3 and v3.3.1).
Many existing FileMaker solutions still run on older 32-bit FileMaker and rely on plugin functions.
Migrating those solutions to 64-bit is hard when their scripts assume an older, 32-bit-only plugin.

This fork restores 32-bit Windows builds so that the plugin can act as a **migration bridge**: the
same plugin can be used on the old 32-bit FileMaker side and on modern 64-bit FileMaker, letting
scripts that depend on it keep working while a solution is converted.

### 2. CJK (Japanese / Chinese / Korean) correctness

Some parts of the plugin — particularly shell execution and text handling — do not behave correctly
in CJK-language environments (for example, garbled output from system commands, or encoding
mismatches on Japanese Windows). This fork treats correct behaviour in CJK locales as a first-class
goal and fixes these issues rather than leaving them as edge cases.

## Compatibility scope (what this fork guarantees, and what it does not)

This fork tracks upstream **at the API level, not at the code level.**

**What is kept the same (the compatibility contract):**

- **Function set** — the plugin exposes the same registered functions as the upstream generation it
  is based on, so it remains a drop-in replacement. Where upstream removed a function that older
  solutions may still call, this fork keeps it, so existing scripts do not break.
- **Observable behaviour** — each function behaves the same as upstream, *except* for (a) bug fixes
  and (b) the CJK corrections described above, both of which are deliberate improvements.

**What is intentionally different (please do not expect otherwise):**

- **Code style / implementation** — the source has been substantially refactored and no longer
  corresponds line-for-line to upstream. Do not expect the internal code to match.
- **Minor-version tracking** — this fork does not follow every upstream minor release. It focuses on
  the two goals above and on stability, not on mirroring upstream's version history.

## Relationship to upstream

- Base: forked from upstream `main` (the 5.0.0 line).
- For the original plugin, its documentation, and general support, please refer to
  [the upstream repository](https://github.com/GoyaPtyLtd/BaseElements-Plugin) and
  [its documentation](https://github.com/GoyaPtyLtd/BaseElements-Plugin/tree/master/docs).
- This fork is maintained independently and is not affiliated with or endorsed by Goya Pty Ltd.
