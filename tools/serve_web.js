// Serves web/ on http://localhost:8737 (WebAssembly needs http, not file://). node tools/serve_web.js
const http = require("http"), fs = require("fs"), path = require("path");
const dir = path.join(__dirname, "..", "web");
const types = { ".html": "text/html; charset=utf-8", ".js": "text/javascript", ".wasm": "application/wasm", ".css": "text/css" };
http.createServer((req, res) => {
  const name = decodeURIComponent(req.url.split("?")[0]);
  const file = path.join(dir, name === "/" ? "index.html" : name);
  if (!file.startsWith(dir)) { res.writeHead(403); return res.end(); }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); return res.end("not found"); }
    res.writeHead(200, { "Content-Type": types[path.extname(file)] || "application/octet-stream" });
    res.end(data);
  });
}).listen(Number(process.env.PORT) || 8737, function () { console.log("vml site: http://localhost:" + this.address().port); });
