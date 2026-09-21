# Step 2 — Early Kernel Infrastructure

## Objective

Create the non-dynamic infrastructure needed by every later subsystem.

## Components

- early console/logger;
- panic implementation;
- architecture abstraction boundary;
- central constants and address helpers;
- build/version metadata;
- boot-state structure;
- minimal fixed-capacity diagnostics;
- safe wrappers around common low-level operations.

## Suggested layout

```text
src/
├── kernel.rs
├── log/
├── panic/
├── arch/
│   └── x86_64/
└── boot/
```

## Logging

Support fixed levels:

```text
TRACE
DEBUG
INFO
WARN
ERROR
FATAL
```

The early logger must not allocate.

## Build metadata

Track:

- Nexora version;
- compiler/toolchain version;
- build profile;
- architecture;
- commit identifier when available;
- enabled kernel features.

## Error philosophy

Expected failure:
`Result<T, Error>`

Kernel invariant violation:
panic/fatal halt.

Do not silently ignore failed initialization.

## Acceptance criteria

- Early logger is usable from all bootstrap code.
- Panic path is deterministic.
- Architecture-dependent code is isolated under `arch/x86_64`.
- No heap assumptions exist.
- Build metadata is printable at boot.
