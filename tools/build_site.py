#!/usr/bin/env python3
"""web/site.html (page content) -> web/index.html (full document for any static host, e.g. GitHub Pages).
The same site.html is what gets published as the hosted preview."""
import os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
body = open(os.path.join(root, "web/site.html"), encoding="utf-8").read()
doc = ("<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
       "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, viewport-fit=cover\">\n"
       "<style>[hidden]{display:none!important}body{margin:0}img{max-width:100%}"
       ":root{padding-top:env(safe-area-inset-top,0px);padding-bottom:env(safe-area-inset-bottom,0px)}</style>\n"
       + body.replace("\n<style>", "\n<style>", 1) + "\n</html>\n")
open(os.path.join(root, "web/index.html"), "w", encoding="utf-8").write(doc)
print("wrote web/index.html")
