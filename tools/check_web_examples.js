// Runs every playground example (web/examples.js) on web/vml.wasm; fails if one does not
// finish cleanly. Usage: node tools/check_web_examples.js
const path = require("path");
const fs = require("fs");
const root = path.join(__dirname, "..");
const VmlWeb = require(path.join(root, "web/vml-web.js"));
const EXAMPLES = require(path.join(root, "web/examples.js"));

(async () => {
  const vml = await VmlWeb.load(fs.readFileSync(path.join(root, "web/vml.wasm")));
  let failed = 0;
  for (const ex of EXAMPLES) {
    const r = vml.run(ex.code);
    const ok = r.code === 0 && r.stderr === "";
    if (!ok) failed++;
    console.log(`${ok ? "ok  " : "FAIL"} ${ex.id} (${r.ms.toFixed(1)} ms)`);
    if (!ok || process.argv.includes("-v")) console.log("  " + (r.stdout + r.stderr).trim().split("\n").join("\n  "));
  }
  console.log(failed ? `Web examples: ${failed} FAILED` : "Web examples: PASS");
  process.exit(failed ? 1 : 0);
})();
