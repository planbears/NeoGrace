#!/usr/bin/env python3
"""Английский словарь интерфейса.

  tools/gen_i18n.py          — проверить, что все русские строки из src/ переведены,
                               и записать src/core/i18n_en.inc
  tools/gen_i18n.py --list   — показать строки без перевода

Словарь — tools/i18n_en.json («русская строка» → «English»).
"""
import json, os, re, sys, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CYR = re.compile('[А-Яа-яЁё]')
LIT = r'"(?:[^"\\\n]|\\.)*"'


def decode(lit):
    out, i = [], 0
    while i < len(lit):
        c = lit[i]
        if c == '\\':
            n = lit[i + 1]
            out.append({'n': '\n', 't': '\t', '\\': '\\', '"': '"'}.get(n, '\\' + n))
            i += 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def extract():
    res = []
    files = sorted(glob.glob(os.path.join(ROOT, 'src/ui/*.cpp')) + glob.glob(os.path.join(ROOT, 'src/core/*.cpp'))
                   + [os.path.join(ROOT, 'src/main.cpp')])
    for f in files:
        s = open(f, encoding='utf-8').read()
        s = re.sub(r'//[^\n]*', '', s)
        s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
        s = re.sub(r'R"\((.*?)\)"', '""', s, flags=re.S)
        for m in re.finditer(LIT + r'(?:\s*' + LIT + ')*', s):
            txt = ''.join(decode(p) for p in re.findall(r'"((?:[^"\\\n]|\\.)*)"', m.group(0)))
            if CYR.search(txt) and txt not in res:
                res.append(txt)
    return res


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'


def main():
    d = json.load(open(os.path.join(ROOT, 'tools/i18n_en.json'), encoding='utf-8'))
    missing = [s for s in extract() if s not in d]
    if missing:
        print('Нет перевода для %d строк:' % len(missing))
        for s in missing:
            print('  ', repr(s))
        if '--list' in sys.argv or missing:
            sys.exit(1)
    with open(os.path.join(ROOT, 'src/core/i18n_en.inc'), 'w', encoding='utf-8') as f:
        f.write('// Сгенерировано tools/gen_i18n.py из tools/i18n_en.json — не править вручную\n')
        for ru in sorted(d):
            f.write('{%s, %s},\n' % (cstr(ru), cstr(d[ru])))
    print('i18n_en.inc: %d строк' % len(d))


main()
