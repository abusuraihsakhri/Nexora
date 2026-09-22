const stages = [
  {
    name: "Multiboot2 handoff",
    label: "Firmware → kernel",
    cpl: "Ring 0",
    rip: "_start",
    rsp: "bootstrap stack",
    segments: "32-bit setup selectors",
    gs: "not installed",
    ursp: "—",
    gdt: "bootstrap",
    idt: "not loaded",
    heap: "not initialized",
    fact: "Implemented",
    detail: "GRUB/Multiboot2 hands control to the 32-bit entry path. The boot assembly preserves the Multiboot information pointer for later physical-memory discovery."
  },
  {
    name: "Long-mode transition",
    label: "x86-64 entry",
    cpl: "Ring 0",
    rip: "long_mode_start",
    rsp: "64-bit kernel stack",
    segments: "kernel selectors",
    gs: "not installed",
    ursp: "—",
    gdt: "temporary",
    idt: "not loaded",
    heap: "early boot",
    fact: "Implemented",
    detail: "The prototype creates early paging structures, enables PAE and long mode, then enters 64-bit execution. The current early map is intentionally bounded rather than a complete VM design."
  },
  {
    name: "Allocator foundation",
    label: "frames + slabs",
    cpl: "Ring 0",
    rip: "kmain",
    rsp: "kernel stack",
    segments: "Ring 0",
    gs: "not installed",
    ursp: "—",
    gdt: "pending final load",
    idt: "pending",
    heap: "frame bitmap + slab",
    fact: "Implemented / prototype",
    detail: "Phase 14 provides frame allocation and slab caches, including excess free-slab reclamation. Boot still uses a fixed research memory region rather than consuming the retained Multiboot memory map."
  },
  {
    name: "Privilege boundary",
    label: "GDT · TSS · IDT",
    cpl: "Ring 0",
    rip: "kernel init",
    rsp: "TSS.rsp0",
    segments: "Ring 0 selectors",
    gs: "BSP per-CPU record",
    ursp: "0",
    gdt: "loaded + TSS",
    idt: "256 vectors",
    heap: "kernel allocators",
    fact: "Implemented / host-tested",
    detail: "GDT, TSS and IDT structures establish the intended privilege boundary. Double faults use a dedicated IST stack. The active implementation remains single-core until AP bring-up and synchronization are completed."
  },
  {
    name: "Syscall contract",
    label: "SYSCALL / SYSRET",
    cpl: "Ring 0 handler",
    rip: "IA32_LSTAR target",
    rsp: "%gs:0 kernel_rsp",
    segments: "SYSCALL contract",
    gs: "kernel per-CPU",
    ursp: "%gs:8 saved",
    gdt: "active",
    idt: "active",
    heap: "kernel allocators",
    fact: "Implemented / hardened",
    detail: "The entry path uses swapgs and one BSP per-CPU record. The return path validates lower-half canonical RIP and rejects unsafe NT/VM RFLAGS before SYSRET."
  },
  {
    name: "ELF64 validation",
    label: "image acceptance",
    cpl: "Ring 0 loader",
    rip: "validated entry",
    rsp: "future user stack",
    segments: "candidate Ring 3",
    gs: "kernel per-CPU",
    ursp: "future mapping",
    gdt: "active",
    idt: "active",
    heap: "mapping callback model",
    fact: "Implemented / host-tested",
    detail: "The ELF loader checks machine type, segment ranges, overlap, executable entry placement, alignment and W^X. Host tests use an abstract mapper; full boot-time process address-space construction is not yet integrated."
  },
  {
    name: "Ring-3 process execution",
    label: "design target",
    cpl: "Ring 3 target",
    rip: "canonical user VA",
    rsp: "isolated user stack",
    segments: "Ring 3 selectors",
    gs: "user view",
    ursp: "user stack",
    gdt: "active",
    idt: "active",
    heap: "per-process VM required",
    fact: "Not end-to-end yet",
    detail: "The privilege-transition pieces exist, but a complete process VM, real userspace tensor mappings, runnable-process scheduling and fault-to-scheduler recovery must be integrated before this becomes an end-to-end boot path."
  }
];

let index = 0;
let timer = null;

function escapeHtml(value) {
  return value.replace(/[&<>"']/g, (char) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"': "&quot;",
    "'": "&#39;"
  })[char]);
}

function renderStages() {
  const container = document.getElementById("stages");
  container.innerHTML = stages.map((stage, i) => {
    const state = i < index ? "complete" : i === index ? "active" : "queued";
    const status = i < index ? "Complete" : i === index ? "Current" : "Queued";
    return `
      <button class="stage ${state}" type="button" onclick="jumpToStage(${i})" aria-current="${i === index ? "step" : "false"}">
        <span class="stage-index">${String(i + 1).padStart(2, "0")}</span>
        <span class="stage-copy">
          <strong>${escapeHtml(stage.name)}</strong>
          <small>${escapeHtml(stage.label)}</small>
        </span>
        <span class="stage-state">${status}</span>
      </button>`;
  }).join("");
}

function renderState(addLog = true) {
  const stage = stages[index];
  const fields = ["rip", "rsp", "segments", "gs", "ursp", "gdt", "idt", "heap"];
  fields.forEach((key) => {
    document.getElementById(key).textContent = stage[key];
  });

  document.getElementById("privilege").textContent = stage.cpl;
  document.getElementById("stage-title").textContent = stage.name;
  document.getElementById("stage-fact").textContent = stage.fact;
  document.getElementById("stage-detail").textContent = stage.detail;
  document.getElementById("stage-progress").style.width = ((index + 1) / stages.length * 100) + "%";
  document.getElementById("stage-count").textContent = `${index + 1} / ${stages.length}`;

  if (addLog) {
    const terminal = document.getElementById("terminal");
    const line = document.createElement("div");
    line.className = "terminal-line";
    line.innerHTML = `<span>${String(index + 1).padStart(2, "0")}</span><p>${escapeHtml(stage.detail)}</p>`;
    terminal.appendChild(line);
    terminal.scrollTop = terminal.scrollHeight;
  }

  renderStages();
}

function jumpToStage(nextIndex) {
  stopAuto();
  index = nextIndex;
  renderState(true);
}

function stepSim() {
  if (index < stages.length - 1) {
    index += 1;
    renderState(true);
  } else {
    stopAuto();
  }
}

function toggleAuto() {
  if (timer) {
    stopAuto();
    return;
  }
  document.getElementById("play").textContent = "Pause";
  timer = setInterval(() => {
    if (index < stages.length - 1) stepSim();
    else stopAuto();
  }, 1900);
}

function stopAuto() {
  if (timer) clearInterval(timer);
  timer = null;
  document.getElementById("play").textContent = "Auto play";
}

function resetSim() {
  stopAuto();
  index = 0;
  document.getElementById("terminal").innerHTML = "";
  renderState(true);
}

window.addEventListener("DOMContentLoaded", () => {
  renderStages();
  renderState(true);
});
