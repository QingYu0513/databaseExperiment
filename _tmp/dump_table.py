# -*- coding: utf-8 -*-
"""抽取 document.xml 中指定第 n 个表格（0 基）的 XML，美化后输出，用于分析结构。"""
import sys, zipfile, re
import xml.etree.ElementTree as ET

W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
q = lambda t: '{%s}%s' % (W, t)

def tostr(el, depth=0, buf=None):
    if buf is None:
        buf = []
    def local(t):
        return t.split('}')[-1] if '}' in t else t
    attrs = ''.join(' %s="%s"' % (local(k), v) for k, v in el.attrib.items())
    kids = list(el)
    txt = (el.text or '')
    if not kids and not txt.strip():
        buf.append('%s<%s%s/>' % ('  ' * depth, local(el.tag), attrs))
        return buf
    if not kids:
        buf.append('%s<%s%s>%s</%s>' % ('  ' * depth, local(el.tag), attrs, txt, local(el.tag)))
        return buf
    buf.append('%s<%s%s>' % ('  ' * depth, local(el.tag), attrs))
    if txt.strip():
        buf.append('%s  #text: %r' % ('  ' * depth, txt[:200]))
    for k in kids:
        tostr(k, depth + 1, buf)
        if k.tail and k.tail.strip():
            buf.append('%s  #tail: %r' % ('  ' * depth, k.tail[:200]))
    buf.append('%s</%s>' % ('  ' * depth, local(el.tag)))
    return buf

def main(path, index):
    with zipfile.ZipFile(path) as z:
        root = ET.fromstring(z.read('word/document.xml'))
    body = root.find(q('body'))
    tables = [c for c in body if c.tag == q('tbl')]
    print('共 %d 个表格' % len(tables))
    t = tables[index]
    rows = t.findall(q('tr'))
    print('表格 %d 有 %d 行' % (index, len(rows)))
    for ri, tr in enumerate(rows):
        cells = tr.findall(q('tc'))
        print('\n===== 行 %d，%d 个单元格 =====' % (ri, len(cells)))
        for ci, tc in enumerate(cells):
            print('  --- 单元格 %d ---' % ci)
            for line in tostr(tc, 2):
                print(line)

if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]))
