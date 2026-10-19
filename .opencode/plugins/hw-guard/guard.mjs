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

// A listener test is only a violation when it is INVOKED, not when the name is
// merely written in prose — so match it in unquoted command text only.
function unquotedHasListener(text) {
  const unquoted = String(text).replace(/"[^"]*"/g, " ").replace(/'[^']*'/g, " ");
  return LISTENER_TEST.test(unquoted);
}

function containsAny(text, tokens) {
  const lower = String(text).toLowerCase();
  return tokens.some((token) => lower.includes(token.toLowerCase()));
}

// --- narrow rules: fire on intent, not on text --------------------------------
// The first cut matched raw substrings anywhere in a command, which blocked
// ordinary work: prepending a tool directory to PATH was indistinguishable from
// DLL shadowing, and merely NAMING a vendor artifact (in a PR body, a commit
// message, a findings doc) read as touching it. Both are false positives. The
// rules below fire on an operation, not on text.

// 1. PATH shadowing. Only the RIGHT-HAND SIDE of a PATH assignment is inspected —
//    never the rest of the command — and only a module/interposer in the dirs
//    being PREPENDED counts. Prepending tool dirs (gh, git, cmake, ninja, the
//    JDK, vcvars) is the normal way to build PATH here and is allowed; prepending
//    a dir that names a .dll/.so/.dylib or an interposer is shadowing. A bare
//    prepend naming nothing module-like is not caught — see README.
const PATH_ASSIGN_VALUE =
  /(?:^|[\s;&|])(?:set\s+)?(?:path|\$env:path|%path%)\s*=\s*("([^"]*)"|'([^']*)'|([^\s;&|]+))/gi;
const MODULE_IN_VALUE = /\.(?:dll|so|dylib)\b|interpos/i;

function pathWriteShadowsModule(text) {
  PATH_ASSIGN_VALUE.lastIndex = 0;
  let m;
  while ((m = PATH_ASSIGN_VALUE.exec(text)) !== null) {
    const value = m[2] ?? m[3] ?? m[4] ?? "";
    for (const raw of value.split(";")) {
      const seg = raw.trim().replace(/^["']|["']$/g, "");
      if (!seg) continue;
      if (/^(?:%path%|\$env:path)$/i.test(seg)) continue; // the inherited PATH, not an added dir
      if (MODULE_IN_VALUE.test(seg)) return true;
    }
  }
  return false;
}

// 2. Vendor artifacts. For file tools (read/edit/write/patch) the input IS the
//    operand, so naming a vendor path there is access. For a shell command the
//    invariant is an operation ON the artifact: an unquoted, path-like vendor
//    operand, or a file op/redirection whose operand is exactly a vendor path.
//    A vendor name inside quoted prose (a PR body, a doc) is not access.
const SHELL_SPLIT = /[;&|]{1,2}/;
const REDIRECT = />>?\s*("[^"]*"|'[^']*'|\S+)/g;
const FILE_VERB =
  /\b(?:cp|copy|mv|move|ren|rename|rm|del|erase|remove-item|set-content|add-content|out-file|new-item|cat|type|get-content|gc|strings|dumpbin|xxd|notepad)\b/gi;

function stripQuotes(word) {
  return String(word).trim().replace(/^["'`]+|["'`]+$/g, "");
}

// A whole operand that IS a vendor path — has a path separator, a module
// extension, or is the .recon-cache directory. Prose (embedded spaces) is not.
function vendorOperand(word) {
  const w = stripQuotes(word);
  if (!w || /\s/.test(w)) return false;
  if (!containsAny(w, VENDOR_TOKENS)) return false;
  return /[\\/]/.test(w) || /\.(?:dll|so|dylib)$/i.test(w) || /^\.recon-cache/i.test(w);
}

// Operands of a shell string, with quoted spans kept whole.
function shellOperands(text) {
  const out = [];
  const re = /"[^"]*"|'[^']*'|\S+/g;
  let m;
  while ((m = re.exec(text)) !== null) out.push(m[0]);
  return out;
}

function shellTouchesVendor(text) {
  const command = String(text);
  // (a) an UNQUOTED, path-like vendor operand (cat .recon-cache/x, > lib.dll).
  const unquoted = command.replace(/"[^"]*"/g, " ").replace(/'[^']*'/g, " ");
  for (const word of unquoted.split(/\s+/)) {
    if (vendorOperand(word)) return true;
  }
  // (b) a file op / redirection whose operand is exactly a vendor path (quotes ok).
  for (const part of command.split(SHELL_SPLIT)) {
    REDIRECT.lastIndex = 0;
    let r;
    while ((r = REDIRECT.exec(part)) !== null) {
      if (vendorOperand(r[1])) return true;
    }
    FILE_VERB.lastIndex = 0;
    let v;
    while ((v = FILE_VERB.exec(part)) !== null) {
      const rest = part.slice(v.index + v[0].length);
      for (const operand of shellOperands(rest)) {
        if (vendorOperand(operand)) return true;
      }
    }
  }
  return false;
}

// The path operands of a file tool's input (excludes patterns/urls, which name
// what to search for, not a file to touch).
function pathOperands(input) {
  const out = [];
  if (input && typeof input === "object") {
    for (const key of ["filePath", "file_path", "path"]) {
      if (typeof input[key] === "string") out.push(input[key]);
    }
    for (const key of ["filePaths", "paths"]) {
      if (Array.isArray(input[key])) {
        for (const value of input[key]) if (typeof value === "string") out.push(value);
      }
    }
  }
  return out;
}

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
  const roomRe = new RegExp(String(room).replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "i");
  const roomInCwd = typeof cwd === "string" && roomRe.test(cwd);

  // Universal: the listener rule. It is DEFINED by being outside the room, so it
  // cannot be gated on cwd being the room, and it is the one thing wrong
  // everywhere: running the device test outside its room.
  if (isShell) {
    for (const text of texts) {
      if (unquotedHasListener(text) && !roomRe.test(text) && !roomInCwd) {
        return block(
          `run listener tests only from the ${room} worktree, or name that worktree in the command`,
        );
      }
    }
  }

  // Hardware-room rules: enforced only when the command's cwd IS the hardware
  // room. Outside it the plugin is inert — the clean-room wall and the vendor
  // boundary are carried by per-role permission denies, which fire on the
  // ARTIFACT, never on prose about it (see README "Scope"). A text-level rule
  // applied to a session that writes prose just teaches the agent to paraphrase.
  if (!roomInCwd) return { block: false, reason: "" };

  if (isShell) {
    for (const text of texts) {
      if (containsAny(text, INJECTION_TOKENS)) {
        return block(
          `library injection is not allowed in the ${room} room ("${quote(text)}"); the tool launcher owns module resolution`,
        );
      }
      if (pathWriteShadowsModule(text)) {
        return block(
          `a PATH-shadowed module/interposer is not allowed in the ${room} room ("${quote(text)}")`,
        );
      }
      if (shellTouchesVendor(text)) {
        return block(
          `vendor artifacts are read-only in the ${room} room ("${quote(text)}") — see docs/recon/README.md`,
        );
      }
    }
  } else {
    // File tools act ON the path they name; inside the room, naming a vendor
    // artifact there is access.
    for (const path of pathOperands(input)) {
      if (containsAny(path, VENDOR_TOKENS)) {
        return block(
          `vendor artifacts are read-only in the ${room} room ("${quote(path)}") — see docs/recon/README.md`,
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
// The hardware room's cwd vs an ordinary room's cwd. The hardware rules are gated
// on the former; the listener rule is universal (it is defined by being outside).
const ROOM = "C:\\dev\\Open3DScanner-Win64-wt-hw";
const OUT = "C:\\Users\\Bruce\\Documents\\Github\\Open3DScanner-Win64-build";

export const MATRIX = [
  // --- must block IN THE ROOM: hardware rules, cwd = the hardware room -------
  {
    name: "IN-ROOM block: SetDllDirectory",
    tool: "shell",
    input: { command: `powershell -c "[Dll]::SetDllDirectory('C:\\evil')"` },
    cwd: ROOM,
    expect: "block",
  },
  {
    name: "IN-ROOM block: PATH-shadowed interposer",
    tool: "shell",
    input: { command: `$env:PATH = "C:\\evil\\interpose;$env:PATH"` },
    cwd: ROOM,
    expect: "block",
  },
  {
    name: "IN-ROOM block: DYLD_INSERT_LIBRARIES (macOS analogue)",
    tool: "shell",
    input: { command: "DYLD_INSERT_LIBRARIES=/x ./run" },
    cwd: ROOM,
    expect: "block",
  },
  {
    name: "IN-ROOM block: LD_PRELOAD (Linux analogue)",
    tool: "shell",
    input: { command: "LD_PRELOAD=./evil.so ./run" },
    cwd: ROOM,
    expect: "block",
  },
  {
    name: "IN-ROOM block: vendor read via shell",
    tool: "shell",
    input: { command: "strings .recon-cache/vendor/libOrbbecSDK.dll" },
    cwd: ROOM,
    expect: "block",
  },
  {
    name: "IN-ROOM block: vendor edit a vendor DLL",
    tool: "edit",
    input: { filePath: "engine/vendor/libOrbbecSDK.dll" },
    cwd: ROOM,
    expect: "block",
  },
  {
    name: "IN-ROOM block: vendor write the scan DLL",
    tool: "write",
    input: { filePath: ".recon-cache/vendor/lib_orbbec_scan.dll" },
    cwd: ROOM,
    expect: "block",
  },
  {
    name: "IN-ROOM block: vendor copy over the scan DLL (shell write)",
    tool: "shell",
    input: { command: "copy build\\ours.dll lib_orbbec_scan.dll" },
    cwd: ROOM,
    expect: "block",
  },
  {
    name: "IN-ROOM block: vendor read a quoted vendor DLL (exact operand)",
    tool: "shell",
    input: { command: 'cat "lib_orbbec_scan.dll"' },
    cwd: ROOM,
    expect: "block",
  },
  // --- must block OUT OF ROOM: the universal listener rule -------------------
  {
    name: "OUT-OF-ROOM block: listener test from the build room",
    tool: "shell",
    input: {
      command: "./build/tests/hardware/device_message_listener_test/device_message_listener_test",
    },
    cwd: OUT,
    expect: "block",
  },
  {
    name: "OUT-OF-ROOM block: listener test, cwd not the room",
    tool: "shell",
    input: { command: "data_process_message_listener_test.exe" },
    cwd: OUT,
    expect: "block",
  },
  // --- must allow IN THE ROOM: ordinary hardware-room work -------------------
  {
    name: "IN-ROOM allow: vcvars + cmake build",
    tool: "shell",
    input: {
      command:
        `call "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" && cmake --build build --parallel`,
    },
    cwd: ROOM,
    expect: "allow",
  },
  {
    name: "IN-ROOM allow: bare PATH export (no module named)",
    tool: "shell",
    input: { command: "set PATH=C:\\tools\\ninja;%PATH%" },
    cwd: ROOM,
    expect: "allow",
  },
  {
    name: "IN-ROOM allow: git status",
    tool: "shell",
    input: { command: "git status --short" },
    cwd: ROOM,
    expect: "allow",
  },
  {
    name: "IN-ROOM allow: cmake configure",
    tool: "shell",
    input: { command: "cmake -S . -B build -G Ninja" },
    cwd: ROOM,
    expect: "allow",
  },
  {
    name: "IN-ROOM allow: verify-pr tier query",
    tool: "shell",
    input: { command: "python scripts/verify-pr.py --tier 1" },
    cwd: ROOM,
    expect: "allow",
  },
  {
    name: "IN-ROOM allow: read the ABI header",
    tool: "read",
    input: { filePath: "engine/include/openscanner/obscan_api.h" },
    cwd: ROOM,
    expect: "allow",
  },
  {
    name: "IN-ROOM allow: read a findings doc",
    tool: "read",
    input: { filePath: "docs/recon/findings/scan-live-data.md" },
    cwd: ROOM,
    expect: "allow",
  },
  {
    name: "IN-ROOM allow: listener test from the room (cwd)",
    tool: "shell",
    input: {
      command: "./build/tests/hardware/device_message_listener_test/device_message_listener_test",
    },
    cwd: ROOM,
    expect: "allow",
  },
  {
    name: "IN-ROOM allow: listener test naming the room in the command",
    tool: "shell",
    input: {
      command:
        "cd C:\\dev\\Open3DScanner-Win64-wt-hw && ./build/tests/hardware/data_process_message_listener_test/data_process_message_listener_test",
    },
    cwd: OUT,
    expect: "allow",
  },
  {
    name: "IN-ROOM allow: FP1 tool-dir PATH prepend (no module named)",
    tool: "shell",
    input: {
      command:
        `$env:PATH = "C:\\Users\\Bruce\\tools\\gh;C:\\Users\\Bruce\\AppData\\Local\\GitHubDesktop\\app-3.6.7\\resources\\app\\git\\cmd;$env:PATH"; gh pr view 6`,
    },
    cwd: ROOM,
    expect: "allow",
  },
  {
    name: "IN-ROOM allow: FP2 prose naming a vendor artifact",
    tool: "shell",
    input: {
      command:
        `gh pr comment 6 --body "the platform registry rows name libOrbbecSDK.1.7.1.dylib and lib_orbbec_scan.dll"`,
    },
    cwd: ROOM,
    expect: "allow",
  },
  {
    name: "IN-ROOM allow: write a doc that names the artifact",
    tool: "shell",
    input: { command: `Set-Content notes.md "liborbbec_scan.dll is a vendor artifact"` },
    cwd: ROOM,
    expect: "allow",
  },
  // --- must allow OUT OF ROOM: inert except the listener rule ---------------
  {
    name: "OUT-OF-ROOM allow: FP1 tool-dir PATH prepend so gh finds git",
    tool: "shell",
    input: {
      command:
        `$env:PATH = "C:\\Users\\Bruce\\tools\\gh;C:\\Users\\Bruce\\AppData\\Local\\GitHubDesktop\\app-3.6.7\\resources\\app\\git\\cmd;$env:PATH"; gh pr view 6`,
    },
    cwd: OUT,
    expect: "allow",
  },
  {
    name: "OUT-OF-ROOM allow: FP2 a PR body that NAMES vendor artifacts",
    tool: "shell",
    input: {
      command:
        `gh pr comment 6 --body "the platform registry rows name libOrbbecSDK.1.7.1.dylib and lib_orbbec_scan.dll"`,
    },
    cwd: OUT,
    expect: "allow",
  },
  {
    name: "OUT-OF-ROOM allow: FP1+FP2 PATH prepend plus a body naming artifacts",
    tool: "shell",
    input: {
      command:
        `$env:PATH = "C:\\Users\\Bruce\\tools\\gh;C:\\Users\\Bruce\\AppData\\Local\\GitHubDesktop\\app-3.6.7\\resources\\app\\git\\cmd;$env:PATH"; gh pr comment 6 --body "libOrbbecSDK.1.7.1.dylib / lib_orbbec_scan.dll"`,
    },
    cwd: OUT,
    expect: "allow",
  },
  {
    name: "OUT-OF-ROOM allow: write a doc that names the artifact",
    tool: "shell",
    input: { command: `Set-Content notes.md "liborbbec_scan.dll is a vendor artifact"` },
    cwd: OUT,
    expect: "allow",
  },
  {
    name: "OUT-OF-ROOM allow: commit message naming the artifact",
    tool: "shell",
    input: { command: `git commit -m "document libOrbbecSDK"` },
    cwd: OUT,
    expect: "allow",
  },
  {
    name: "OUT-OF-ROOM allow: injection primitive is inert outside the room",
    tool: "shell",
    input: { command: `powershell -c "[Dll]::SetDllDirectory('C:\\evil')"` },
    cwd: OUT,
    expect: "allow",
  },
  {
    name: "OUT-OF-ROOM allow: vendor shell read is inert outside the room",
    tool: "shell",
    input: { command: "strings .recon-cache/vendor/libOrbbecSDK.dll" },
    cwd: OUT,
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
