const stages = [
  {
    label: "00 · Multiboot handoff",
    lines: [
      ["prompt", "GRUB → Multiboot2 kernel entry"],
      ["ok", "boot header accepted; interrupts disabled for early transition"]
    ]
  },
  {
    label: "01 · Long mode",
    lines: [
      ["prompt", "construct minimal identity page tables"],
      ["ok", "PAE + EFER.LME + paging enabled"],
      ["ok", "control transferred to x86-64 long mode"]
    ]
  },
  {
    label: "02 · Memory substrate",
    lines: [
      ["prompt", "initialize early heap, frame allocator, and slab caches"],
      ["ok", "tensor/work kernel object caches available"],
      ["muted", "note: fully free slab pages are not yet returned to the frame allocator"]
    ]
  },
  {
    label: "03 · Privilege substrate",
    lines: [
      ["prompt", "load GDT/TSS and IDT"],
      ["ok", "Ring 0 / Ring 3 descriptors installed"],
      ["ok", "exception vectors and syscall MSRs configured"],
      ["muted", "current v0.1 execution scope remains one CPU"]
    ]
  },
  {
    label: "04 · Nexora backend",
    lines: [
      ["prompt", "install capability-aware syscall backend bridge"],
      ["ok", "generation-tagged handles and delegated lifetime rules active"],
      ["muted", "tensor mapping intentionally returns ENOSYS until real user VM mapping exists"]
    ]
  },
  {
    label: "05 · Semantic layer",
    lines: [
      ["prompt", "initialize tensor registry and work-graph model"],
      ["ok", "tensor size/shape validation ready"],
      ["ok", "dependency validation and deterministic scheduler ready"]
    ]
  },
  {
    label: "06 · Qualification",
    lines: [
      ["prompt", "run integrated validation path"],
      ["ok", "graph, deadlock, accounting, capability, and allocator checks complete"],
      ["ok", "Phase 16/17 observability modules linked into active kernel"]
    ]
  },
  {
    label: "07 · Demonstration graph",
    lines: [
      ["prompt", "construct deterministic demonstration workload"],
      ["ok", "ready work selected and graph completed"],
      ["muted", "executor is synchronous; no real GPU/NPU kernel is launched"]
    ]
  },
  {
    label: "08 · Terminal state",
    lines: [
      ["ok", "Nexora initialization complete."],
      ["prompt", "CPU enters halt loop"],
      ["muted", "production userspace launch is not claimed by the current boot path"]
    ]
  }
];

const steps = document.getElementById("bootSteps");
const terminal = document.getElementById("terminal");
const progress = document.getElementById("progressBar");
const runBtn = document.getElementById("runBtn");
const stepBtn = document.getElementById("stepBtn");
const resetBtn = document.getElementById("resetBtn");

let current = -1;
let running = false;

function renderSteps() {
  steps.innerHTML = "";
  stages.forEach((stage, index) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "boot-step";
    button.textContent = stage.label;
    button.addEventListener("click", () => showStage(index, true));
    steps.appendChild(button);
  });
}

function writeLine(kind, text) {
  const line = document.createElement("div");
  const marker = document.createElement("span");
  marker.className = kind;
  marker.textContent = kind === "ok" ? "✓ " : kind === "prompt" ? "› " : "  ";
  line.appendChild(marker);
  line.appendChild(document.createTextNode(text));
  terminal.appendChild(line);
  terminal.scrollTop = terminal.scrollHeight;
}

function paintState() {
  [...steps.children].forEach((el, index) => {
    el.classList.toggle("active", index === current);
    el.classList.toggle("done", index < current);
  });
  const pct = current < 0 ? 0 : ((current + 1) / stages.length) * 100;
  progress.style.width = pct + "%";
}

function reset() {
  current = -1;
  running = false;
  terminal.innerHTML = '<span class="muted">NEXORA boot proof ready.</span><br><span class="muted">Choose “Run sequence” or select a stage.</span>';
  paintState();
  runBtn.disabled = false;
  stepBtn.disabled = false;
}

function showStage(index, clearFuture = false) {
  if (index < 0 || index >= stages.length) return;
  if (clearFuture && index <= current) {
    terminal.innerHTML = "";
    current = -1;
    for (let i = 0; i <= index; i++) {
      current = i;
      writeLine("muted", "— " + stages[i].label + " —");
      stages[i].lines.forEach(([kind, text]) => writeLine(kind, text));
    }
  } else {
    current = index;
    writeLine("muted", "— " + stages[index].label + " —");
    stages[index].lines.forEach(([kind, text]) => writeLine(kind, text));
  }
  paintState();
}

function step() {
  if (current >= stages.length - 1) return;
  showStage(current + 1);
}

async function runAll() {
  if (running) return;
  running = true;
  runBtn.disabled = true;
  stepBtn.disabled = true;
  terminal.innerHTML = "";
  current = -1;
  paintState();
  for (let i = 0; i < stages.length; i++) {
    showStage(i);
    await new Promise(resolve => setTimeout(resolve, 380));
  }
  running = false;
  runBtn.disabled = false;
  stepBtn.disabled = false;
}

renderSteps();
paintState();
runBtn.addEventListener("click", runAll);
stepBtn.addEventListener("click", step);
resetBtn.addEventListener("click", reset);
