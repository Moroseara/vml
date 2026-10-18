#!/usr/bin/env python3
"""Builds the React application for the web playground into web/app.bundle.js and app.bundle.css."""
import os
import subprocess
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
web_dir = os.path.join(root, "web")

cmd = ["npx", "esbuild", "app/main.jsx", "--bundle", "--minify", "--outfile=app.bundle.js"]
print("Building React web bundle with esbuild...")
res = subprocess.run(cmd, cwd=web_dir)
if res.returncode != 0:
    print("Error: esbuild failed")
    sys.exit(res.returncode)

print("Successfully built web/app.bundle.js and web/app.bundle.css")
