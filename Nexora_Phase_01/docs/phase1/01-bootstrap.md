# Step 1 — Bootstrap and Kernel Entry

## Objective

Produce the smallest deterministic x86-64 Nexora kernel that can be entered by a boot environment, establish an early execution context, and report its state over a debug console.

## Required outcomes

- A freestanding `no_std` kernel target.
- Defined linker layout.
- Stable kernel entry symbol.
- Early stack.
- Early serial/debug output.
- Panic path that does not require heap allocation.
- Boot information structure passed into the kernel.
- Deterministic halt loop.

## Architecture

```text
Firmware / boot environment
        ↓
bootloader
        ↓
Nexora entry
        ↓
early stack
        ↓
early serial
        ↓
boot-information validation
        ↓
kernel_main()
```

## Rules

1. No heap usage.
2. No dynamic allocation.
3. No scheduler.
4. No device-driver framework.
5. No AI-specific abstractions.
6. A fatal failure must print a diagnostic and halt rather than reset silently.

## Suggested starter interfaces

```rust
#[repr(C)]
pub struct BootInfo {
    pub physical_memory_offset: u64,
    pub memory_map_ptr: u64,
    pub memory_map_len: usize,
}

#[no_mangle]
pub extern "C" fn kernel_main(boot_info: &'static BootInfo) -> ! {
    early_console::init();
    validate_boot_info(boot_info);
    log!("Nexora kernel entered");
    halt_loop()
}
```

## Acceptance criteria

- Kernel entry is reached reproducibly.
- Serial output works before memory allocation exists.
- Panic output works without heap allocation.
- Invalid boot information fails cleanly.
- The kernel reaches a stable halt loop.
