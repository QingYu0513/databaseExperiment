# -*- coding: utf-8 -*-
"""把 docx 的 document.xml 转成可读的结构化大纲（段落样式 + 文本 + 表格）。"""
import sys, zipfile, re
import xml.etree.ElementTree as ET

W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
NS = {'w': W}

def q(tag):
    return '{%s}%s' % (W, tag)

def para_info(p):
    """返回 (style, text, is_bold)"""
    style = ''
    ppr = p.find(q('pPr'))
    if ppr is not None:
        ps = ppr.find(q('pStyle'))
        if ps is not None:
            style = ps.get(q('val'), '')
    texts = []
    bold = False
    for r in p.iter(q('r')):
        rpr = r.find(q('rPr'))
        if rpr is not None and rpr.find(q('b')) is not None:
            b = rpr.find(q('b'))
            if b.get(q('val')) not in ('0', 'false'):
                bold = True
        for node in r:
            if node.tag == q('t'):
                texts.append(node.text or '')
            elif node.tag == q('tab'):
                texts.append('\t')
            elif node.tag == q('br'):
                texts.append('\n')
    return style, ''.join(texts), bold

def walk(body, out, depth=0):
    idx = 0
    for child in body:
        idx += 1
        if child.tag == q('p'):
            style, text, bold = para_info(child)
            if text.strip() == '' and not style:
                continue
            out.append('%s[P%03d] style=%-22s bold=%-5s | %s'
                       % ('  ' * depth, idx, style or '(none)', bold, text.replace('\n', '\\n')))
        elif child.tag == q('tbl'):
            out.append('%s[TABLE %03d] ========================================' % ('  ' * depth, idx))
            for ri, tr in enumerate(child.findall(q('tr'))):
                cells = []
                for tc in tr.findall(q('tc')):
                    ct = []
                    for p in tc.findall(q('p')):
                        s, t, b = para_info(p)
                        if t.strip():
                            ct.append(t.strip())
                    cells.append(' / '.join(ct) if ct else '')
                out.append('%s   R%02d | %s' % ('  ' * depth, ri, ' || '.join(cells)))
            out.append('%s[END TABLE %03d]' % ('  ' * depth, idx))
        elif child.tag == q('sectPr'):
            out.append('%s[SECTION PROPERTIES]' % ('  ' * depth))

def main(path):
    with zipfile.ZipFile(path) as z:
        xml = z.read('word/document.xml')
    root = ET.fromstring(xml)
    body = root.find(q('body'))
    out = []
    walk(body, out)
    sys.stdout.write('\n'.join(out))
    sys.stdout.write('\n')

if __name__ == '__main__':
    main(sys.argv[1])
