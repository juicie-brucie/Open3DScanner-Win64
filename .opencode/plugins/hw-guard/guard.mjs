// hw-guard — the OpenCode hardware backstop for the Windows fork.
//
// The parent's protection is a Claude Code PreToolUse hook
// (.claude/hooks/guard-hw-run.sh) that blocks a shell command by exiting 2.
// This is OpenCode, so the equivalent is a V2 plugin: it registers
// ctx.tool.hook("execute.before", ...) and throws to abort the tool call — the
// documented V2 replacement for the V1 tool.execute.before throw (see the
// OpenCode V2 "Migrate plugins from V1" guide). opencode.jsonc carries the
// static half as `permissions` deny rules.
//
// This module deliberately imports nothing: the plugin shape is a plain
// { id, setup } object (V2 accepts that — `Plugin.define` is only a helper),
// and the evaluator is pure, so `tools/hw_guard_test/run.mjs` can import and
// exercise the real registration with no dependencies installed.

export const DEFAULT_ROOM = "wt-hw";

// Steering module/library resolution by hand. The parent blocked
// DYLD_INSERT_LIBRARIES (macOS); these are the Windows analogues
// (SetDllDirectory / AddDllDirectory / SetDefaultDllDirectories = an explicit
// DLL directory; LoadLibrary = a manual load; PATH shadowing is handled below),
// plus the Linux one so the guard travels.
export const INJECTION_TOKENS = [
  "SetDllDirectory",
  "AddDllDirectory",
  "SetDefaultDllDirectories",
  "LoadLibrary",
  "DYLD_INSERT_LIBRARIES",
  "LD_PRELOAD",
];

// Vendor artifacts: never read, never edited (the clean-room wall). Mirrors the
// parent hook's .recon-cache / libOrbbecSDK / liborbbec_scan rule and adds the
// Windows DLL name.
export const VENDOR_TOKENS = [
  ".recon-cache",
  "libOrbbecSDK",
  "lib_orbbec_scan",
  "liborbbec_scan",
];

// The parent matched *message_listener_test*; the fork's binaries keep the same
// suffix (device_message_listener_test, data_process_message_listener_test).
const LISTENER_TEST = /message_listener_test/i;

function containsAny(text, tokens) {
  const lower = String(text).toLowerCase();
  return tokens.some((token) => lower.includes(token.toLowerCase()));
}

// A PATH write that also names a module/interposer is DLL shadowing. A bare
// PATH write is how every toolchain sets up (vcvars, a ninja directory), so it
// is intentionally NOT blocked — see README "What this does not cover".
const PATH_ASSIGN = /(path\s*=|\$env:path|%path%)/i;
const MODULE_HINT = /(\.dll|interpos|orbbec|obscan)/i;

// Pull every string a tool call carries that could name a path or a command.
export function candidateTexts(tool, input) {
  const texts = [];
  if (typeof input === "string") texts.push(input);
  if (input && typeof input === "object") {
    for (const key of ["command", "cmd", "filePath", "file_path", "path", "pattern", "url"]) {
      if (typeof input[key] === "string") texts.push(input[key]);
    }
    for (const key of ["filePaths", "paths"]) {
      if (Array.isArray(input[key])) {
        for (const value of input[key]) if (typeof value === "string") texts.push(value);
      }
    }
  }
  return texts;
}

function block(reason) {
  return { block: true, reason };
}

function quote(text) {
  const flat = String(text).replace(/\s+/g, " ").trim();
  return flat.length > 80 ? flat.slice(0, 77) + "..." : flat;
}

// Pure decision function — the whole guard, testable in isolation.
//   tool  the OpenCode tool name ("shell", "edit", "write", "patch", "read", ...)
//   input the tool arguments
//   cwd   the best-known working directory (input.cwd if carried, else the
//         plugin location); only the listener-test rule uses it
//   room  the designated hardware worktree substring (default "wt-hw")
export function evaluate(tool, input, cwd, room = DEFAULT_ROOM) {
  const name = String(tool || "").toLowerCase();
  const isShell = name === "shell" || name === "bash";
  const texts = candidateTexts(tool, input);

  for (const text of texts) {
    if (containsAny(text, INJECTION_TOKENS)) {
      return block(
        `library injection is not allowed here ("${quote(text)}"); the tool launcher owns module resolution`,
      );
    }
    if (isShell && PATH_ASSIGN.test(text) && MODULE_HINT.test(text)) {
      return block(`a PATH-shadowed module/interposer is not allowed here ("${quote(text)}")`);
    }
    if (containsAny(text, VENDOR_TOKENS)) {
      return block(
        `vendor artifacts are read-only in this lane ("${quote(text)}") — see docs/recon/README.md`,
      );
    }
  }

  if (isShell) {
    const roomRe = new RegExp(String(room).replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "i");
    const roomInCwd = typeof cwd === "string" && roomRe.test(cwd);
    for (const text of texts) {
      if (LISTENER_TEST.test(text) && !roomRe.test(text) && !roomInCwd) {
        return block(
          `run listener tests only from the ${room} worktree, or name that worktree in the command`,
        );
      }
    }
  }

  return { block: false, reason: "" };
}

// The matrix the test drives — the port of the parent hook's must-block /
// must-allow cases. `cwd`, when present, is the shell tool's working directory;
// the plugin half of the test injects it into the tool input, as the shell tool
// would carry it.
export const MATRIX = [
  // --- must block ---------------------------------------------------------
  {
    name: "dll-search: SetDllDirectory",
    tool: "shell",
    input: { command: `powershell -c "[Dll]::SetDllDirectory('C:\\evil')"` },
    expect: "block",
  },
  {
    name: "dll-search: PATH-shadowed interposer",
    tool: "shell",
    input: { command: `$env:PATH = "C:\\evil\\interpose;$env:PATH"` },
    expect: "block",
  },
  {
    name: "injection: DYLD_INSERT_LIBRARIES (macOS analogue)",
    tool: "shell",
    input: { command: "DYLD_INSERT_LIBRARIES=/x ./run" },
    expect: "block",
  },
  {
    name: "injection: LD_PRELOAD (Linux analogue)",
    tool: "shell",
    input: { command: "LD_PRELOAD=./evil.so ./run" },
    expect: "block",
  },
  {
    name: "vendor: read via shell",
    tool: "shell",
    input: { command: "strings .recon-cache/vendor/libOrbbecSDK.dll" },
    expect: "block",
  },
  {
    name: "vendor: edit a vendor DLL",
    tool: "edit",
    input: { filePath: "engine/vendor/libOrbbecSDK.dll" },
    expect: "block",
  },
  {
    name: "vendor: write the scan DLL",
    tool: "write",
    input: { filePath: ".recon-cache/vendor/lib_orbbec_scan.dll" },
    expect: "block",
  },
  {
    name: "listener: outside the room (cwd = build room)",
    tool: "shell",
    input: {
      command: "./build/tests/hardware/device_message_listener_test/device_message_listener_test",
    },
    cwd: "C:\\Users\\Bruce\\Documents\\Github\\Open3DScanner-Win64-build",
    expect: "block",
  },
  {
    name: "listener: no cwd, plugin location is not the room",
    tool: "shell",
    input: { command: "data_process_message_listener_test.exe" },
    expect: "block",
  },
  // --- must allow ---------------------------------------------------------
  {
    name: "build: vcvars + cmake",
    tool: "shell",
    input: {
      command:
        `call "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" && cmake --build build --parallel`,
    },
    expect: "allow",
  },
  {
    name: "toolchain: bare PATH export (no module named)",
    tool: "shell",
    input: { command: "set PATH=C:\\tools\\ninja;%PATH%" },
    expect: "allow",
  },
  {
    name: "git: status",
    tool: "shell",
    input: { command: "git status --short" },
    expect: "allow",
  },
  {
    name: "cmake: configure",
    tool: "shell",
    input: { command: "cmake -S . -B build -G Ninja" },
    expect: "allow",
  },
  {
    name: "verify-pr: tier query",
    tool: "shell",
    input: { command: "python scripts/verify-pr.py --tier 1" },
    expect: "allow",
  },
  {
    name: "read: the ABI header (contract, not a vendor artifact)",
    tool: "read",
    input: { filePath: "engine/include/openscanner/obscan_api.h" },
    expect: "allow",
  },
  {
    name: "read: a findings doc",
    tool: "read",
    input: { filePath: "docs/recon/findings/scan-live-data.md" },
    expect: "allow",
  },
  {
    name: "listener: from the room (cwd)",
    tool: "shell",
    input: {
      command: "./build/tests/hardware/device_message_listener_test/device_message_listener_test",
    },
    cwd: "C:\\dev\\Open3DScanner-Win64-wt-hw",
    expect: "allow",
  },
  {
    name: "listener: explicit room path in the command",
    tool: "shell",
    input: {
      command:
        "cd C:\\dev\\Open3DScanner-Win64-wt-hw && ./build/tests/hardware/data_process_message_listener_test/data_process_message_listener_test",
    },
    expect: "allow",
  },
];

// The plugin definition. index.ts re-exports this as the default, so the test
// below exercises exactly the object OpenCode loads.
export default {
  id: "openscanner.hw-guard",
  async setup(ctx) {
    const room =
      ctx && ctx.options && typeof ctx.options.room === "string" ? ctx.options.room : DEFAULT_ROOM;
    await ctx.tool.hook("execute.before", (event) => {
      const input = event.input;
      const inputCwd =
        input && typeof input === "object" && typeof input.cwd === "string" ? input.cwd : "";
      const cwd = inputCwd || (ctx.location && ctx.location.directory) || "";
      const verdict = evaluate(event.tool, input, cwd, room);
      if (verdict.block) throw new Error(`hw-guard: ${verdict.reason}`);
    });
  },
};
