#!/usr/bin/env python3
"""Embed the rendered screens into the mockup page.

    build.py <screens dir> <template.html> <out.html> [preview.html]
"""
import base64, json, pathlib, sys

src, tpl, out = map(pathlib.Path, sys.argv[1:4])
images = {p.stem: 'data:image/png;base64,' + base64.b64encode(p.read_bytes()).decode()
          for p in sorted(src.glob('*.png')) if not p.stem.startswith('sheet')}
page = tpl.read_text().replace('/*__IMAGES__*/{}', json.dumps(images, separators=(',', ':')))
out.write_text(page)
print(f'{out}: {len(images)} screens, {len(page) // 1024} KB')
if len(sys.argv) > 4:
    # local preview only: the host adds the document skeleton itself
    pathlib.Path(sys.argv[4]).write_text(
        '<!doctype html><html lang="en"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1"></head><body>'
        + page + '</body></html>')
