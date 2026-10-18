// Drives the hw-guard matrix through both layers of the guard:
//   A. the pure evaluator (evaluate())
//   B. the real plugin registration — imports guard.mjs, calls setup() with a
//      fake context that captures the execute.before hook, then invokes that
//      captured hook for each case. This is the "is the guard actually invoked"
//      evidence: it runs the same default export OpenCode loads.
//   C. the opencode.jsonc backstop is path-only: it carries the vendor path
//      (read/edit) rules and NO shell rules — a static glob cannot see the room,
//      and a repo-wide shell rule would police vocabulary everywhere.
//
// Run (no Node on PATH here): with VS Code's bundled Node —
//   $env:ELECTRON_RUN_AS_NODE="1"
//   & "C:\Users\Bruce\AppData\Local\Programs\Microsoft VS Code\Code.exe" tools\hw_guard_test\run.mjs
// Exit 0 = all cases behave as specified; 1 = a mismatch.

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import plugin, { DEFAULT_ROOM, VENDOR_TOKENS, evaluate, MATRIX } from "../../.opencode/plugins/hw-guard/guard.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const BUILD_ROOM = "C:\\Users\\Bruce\\Documents\\Github\\Open3DScanner-Win64-build";

const results = [];
function record(part, name, ok, detail) {
  results.push({ part, name, ok, detail });
}

// --- C: the static backstop must stay path-only --------------------------
{
  const config = readFileSync(join(here, "..", "..", "opencode.jsonc"), "utf8");
  const missing = VENDOR_TOKENS.filter((token) => !config.includes(token));
  const hasShellRule = /"action"\s*:\s*"shell"/.test(config);
  record(
    "C/config",
    "vendor path rules present, no repo-wide shell rules",
    missing.length === 0 && !hasShellRule && config.includes('"permissions"'),
    missing.length
      ? `missing vendor tokens: ${missing.join(", ")}`
      : hasShellRule
        ? "a shell rule remains (cannot be room-scoped)"
        : `${VENDOR_TOKENS.length} vendor tokens, 0 shell rules`,
  );
}

// --- A: the pure evaluator -----------------------------------------------
for (const testCase of MATRIX) {
  const verdict = evaluate(testCase.tool, testCase.input, testCase.cwd, DEFAULT_ROOM);
  const actual = verdict.block ? "block" : "allow";
  record(
    "A/evaluate",
    testCase.name,
    actual === testCase.expect,
    actual === testCase.expect ? actual : `expected ${testCase.expect}, got ${actual}`,
  );
}

// --- B: the real plugin registration -------------------------------------
{
  const captured = {};
  const fakeCtx = {
    options: {},
    location: { directory: BUILD_ROOM },
    tool: { hook: async (name, callback) => { captured[name] = callback; } },
  };
  await plugin.setup(fakeCtx);
  const hook = captured["execute.before"];
  record(
    "B/plugin",
    `setup() registered execute.before (id=${plugin.id})`,
    typeof hook === "function",
    typeof hook === "function" ? "hook captured" : "NO execute.before hook was registered",
  );

  if (typeof hook === "function") {
    for (const testCase of MATRIX) {
      const input = testCase.cwd ? { ...testCase.input, cwd: testCase.cwd } : testCase.input;
      let actual = "allow";
      let reason = "";
      try {
        await hook({ tool: testCase.tool, input });
      } catch (error) {
        actual = "block";
        reason = String(error && error.message ? error.message : error);
      }
      record(
        "B/plugin",
        testCase.name,
        actual === testCase.expect,
        actual === testCase.expect ? reason || actual : `expected ${testCase.expect}, got ${actual}`,
      );
    }
  }
}

// --- report --------------------------------------------------------------
console.log("| part | case | result | detail |");
console.log("|---|---|---|---|");
for (const r of results) {
  console.log(`| ${r.part} | ${r.name} | ${r.ok ? "PASS" : "FAIL"} | ${String(r.detail).replace(/\|/g, "/")} |`);
}
const failed = results.filter((r) => !r.ok);
console.log();
if (failed.length) {
  console.log(`**FAILED: ${failed.length} of ${results.length}**`);
  process.exitCode = 1;
} else {
  console.log(`All ${results.length} cases behave as specified.`);
}
