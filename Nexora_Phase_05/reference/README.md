# Cross-check reference provenance

`ORIGINAL_ROADMAP.md` is copied verbatim from the `aikernel-starter.zip` baseline used for the Nexora/AIKernel project.

- Starter ZIP SHA-256: `6ac1ac67de7cc0770a187afc6bdc06083af0681e0f7243826f035e0ee75776b6`
- Copied roadmap SHA-256: `e51109b00393b4e6af81f3de640f7b95171a0a52050054fae13f9f89ee6b9f9f`

The consistency checker reads the Milestone-5 syscall list from this copied roadmap so the audit remains reproducible inside the release package.

`starter_include/` preserves the small set of original starter type headers used by `compat_check.c` to verify numeric compatibility for dtypes, locations, tensor flags, device masks, work operations/states, dimensions, and input count. The original starter used four output slots whereas the Phase-5 ABI permits eight; that known delta is intentionally excluded from the static assertion and is documented as a Phase-4 adapter check.
