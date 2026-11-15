#!/usr/bin/env python3
# Draws the README's charts as static SVGs, one light and one dark each (GitHub picks one with
# <picture> and prefers-color-scheme). Numbers copied from docs/benchmarks.md; run again when
# they change:   python3 -I scripts/readme-charts.py docs/images
import sys
from pathlib import Path

THEMES = {
    'light': {'ours': '#2a6a9e', 'ref': '#a9b6c3', 'text': '#16202a', 'muted': '#4c5c6c',
              'faint': '#6a7a8a', 'rule': '#d0d9e2'},
    'dark': {'ours': '#3f9ad4', 'ref': '#55657a', 'text': '#e4edf5', 'muted': '#97a7b8',
             'faint': '#7d8d9e', 'rule': '#30363d'},
}
SANS = "-apple-system, BlinkMacSystemFont, 'Segoe UI', 'Noto Sans', Helvetica, Arial, sans-serif"
MONO = "ui-monospace, SFMono-Regular, 'SF Mono', Menlo, Consolas, 'Liberation Mono', monospace"

WIDTH = 760
LABEL_W = 130  # the names' column
VALUE_ROOM = 90  # room after the longest bar for its value
BAR_H = 14
ROW_H = 24


def esc(text):
    return text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def chart(title, subtitle, groups, footer, theme, top=None):
    """groups: [(heading, [(label, series, value, text)])]: one scale per group, or `top` for all."""
    c = THEMES[theme]
    out = []
    y = 24
    out.append(f'<text x="0" y="{y}" font-family="{SANS}" font-size="17" font-weight="600" '
               f'fill="{c["text"]}">{esc(title)}</text>')
    y += 22
    out.append(f'<text x="0" y="{y}" font-family="{SANS}" font-size="13" '
               f'fill="{c["muted"]}">{esc(subtitle)}</text>')
    y += 8
    track = WIDTH - LABEL_W - VALUE_ROOM
    for heading, rows in groups:
        y += 26
        out.append(f'<text x="0" y="{y}" font-family="{SANS}" font-size="13" font-weight="600" '
                   f'fill="{c["text"]}">{esc(heading)}</text>')
        y += 8
        scale = top or max(r[2] for r in rows)
        out.append(f'<line x1="{LABEL_W - 0.5}" y1="{y}" x2="{LABEL_W - 0.5}" '
                   f'y2="{y + ROW_H * len(rows)}" stroke="{c["rule"]}" stroke-width="1"/>')
        for label, series, value, text in rows:
            mid = y + ROW_H / 2
            ours = series == 'ours'
            weight = '600' if ours else '400'
            out.append(f'<text x="{LABEL_W - 12}" y="{mid + 4.5}" text-anchor="end" '
                       f'font-family="{SANS}" font-size="13" font-weight="{weight}" '
                       f'fill="{c["text"] if ours else c["muted"]}">{esc(label)}</text>')
            w = max(3.0, track * value / scale)
            r = min(4.0, w / 2)
            out.append(f'<path d="M{LABEL_W} {mid - BAR_H / 2} h{w - r:.1f} a{r} {r} 0 0 1 {r} {r} '
                       f'v{BAR_H - 2 * r} a{r} {r} 0 0 1 {-r} {r} h{-(w - r):.1f} z" '
                       f'fill="{c[series]}"/>')
            out.append(f'<text x="{LABEL_W + w + 8:.1f}" y="{mid + 4.5}" font-family="{MONO}" '
                       f'font-size="12.5" font-weight="{weight}" '
                       f'fill="{c["text"] if ours else c["muted"]}">{esc(text)}</text>')
            y += ROW_H
    y += 30
    out.append(f'<text x="0" y="{y}" font-family="{SANS}" font-size="11.5" '
               f'fill="{c["faint"]}">{esc(footer)}</text>')
    height = y + 8
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{height}" '
            f'viewBox="0 0 {WIDTH} {height}" role="img" aria-label="{esc(title)}">\n'
            f'<title>{esc(title)}</title>\n' + '\n'.join(out) + '\n</svg>\n')


def copy_rows(nordend, fcp, xcp, cp):
    def s(v):
        return '6.6 s' if v == 6.6 else f'{v:.2f} s'

    return [('Nordend', 'ours', nordend, s(nordend)), ('fcp', 'ref', fcp, s(fcp)),
            ('xcp', 'ref', xcp, s(xcp)), ('cp', 'ref', cp, s(cp))]


COPY = dict(
    title='Copying a folder, against cp, fcp and xcp',
    subtitle='Seconds, lower is better. Every run ends in sync: the data is on the disk.',
    groups=[('10 000 small files, read from the disk', copy_rows(1.26, 1.09, 1.63, 2.04)),
            ('5 024 files, 4.3 GB, already in the page cache', copy_rows(3.49, 3.40, 4.00, 6.6))],
    footer='Nordend 0.8.0 (8 copiers), GNU cp 9.4, fcp 0.2.2, xcp 0.24.9 · SATA SSD to NVMe btrfs, '
           'Ryzen 7 5700G · 2026-11-15',
)

RAW_TIMES = [('OM System ORF', 3.45, 0.03), ('Nikon NEF', 2.46, 0.14), ('Sony ARW', 3.31, 0.21),
             ('Fujifilm RAF', 3.56, 0.38), ('Panasonic RW2', 2.21, 0.04), ('DJI DNG', 1.87, 0.03)]

RAW = dict(
    title="A raw photo's thumbnail: Nordend's own reader against exiv2",
    subtitle='Milliseconds, lower is better: open the file, find the preview for a 160 px '
             'thumbnail, read it.',
    groups=[(name, [('exiv2', 'ref', exiv2, f'{exiv2:.2f} ms'),
                    ('Nordend', 'ours', ours, f'{ours:.2f} ms')]) for name, exiv2, ours in RAW_TIMES],
    footer='Best of 30, page cache warm, exiv2 0.28 · Ryzen 7 5700G · 2026-11-08',
    top=max(exiv2 for _, exiv2, _ in RAW_TIMES),
)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else 'docs/images')
    for theme in THEMES:
        (out / f'chart-copy-{theme}.svg').write_text(chart(theme=theme, **COPY))
        (out / f'chart-raw-{theme}.svg').write_text(chart(theme=theme, **RAW))
    print('written to', out)


if __name__ == '__main__':
    main()
