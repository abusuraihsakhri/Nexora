# Security Policy

Nexora is a research operating-system project. It is not production-hardened and
must not be treated as a security boundary for untrusted workloads yet.

## Reporting a vulnerability

Please use GitHub's private vulnerability reporting feature for this repository
when available. Do not publish proof-of-concept exploitation details before a
fix is available.

Useful reports include:

- affected commit and subsystem;
- architecture and execution environment;
- minimal reproduction;
- observed versus expected behavior;
- security impact and preconditions.

## Current security scope

The active research kernel is intentionally fail-closed where a mechanism has
not yet been implemented safely. In particular, object delegation requires real
lifetime accounting and userspace tensor mapping requires a real VM-backed
mapping path before those ABI operations can be enabled.

The project CI builds the freestanding kernel and executes host-side integration,
benchmark, sanitizer, release-gate, and documentation checks. Passing CI is a
development quality gate, not a claim of production security.
