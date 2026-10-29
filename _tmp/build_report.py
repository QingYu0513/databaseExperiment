# -*- coding: utf-8 -*-
"""从模板生成实验一报告。

只替换 word/document.xml 中正文表格五个单元格的内容，其余部件（样式表、页眉页脚、
图片、主题、编号定义）全部按原字节复制，因此版式与模板完全一致。
模板文件本身不做任何修改。
"""
import sys, os, zipfile, shutil, datetime
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import report_content


def find_elems(s, tag, start=0, end=None):
    """返回 s[start:end] 内所有 <w:tag ...>...</w:tag> 或 <w:tag .../> 的 (起, 止) 区间。"""
    if end is None:
        end = len(s)
    open_tok = '<w:' + tag
    close_tok = '</w:' + tag + '>'
    res = []
    i = start
    while True:
        j = s.find(open_tok, i, end)
        if j < 0:
            break
        k = j + len(open_tok)
        if k >= len(s) or s[k] not in ' \t\r\n>/':
            i = k
            continue
        gt = s.find('>', k)
        if gt < 0 or gt >= end:
            break
        if s[gt - 1] == '/':
            res.append((j, gt + 1))
            i = gt + 1
            continue
        close = s.find(close_tok, gt + 1, end)
        if close < 0:
            break
        res.append((j, close + len(close_tok)))
        i = close + len(close_tok)
    return res


def build_document_xml(doc, sections):
    tables = find_elems(doc, 'tbl')
    if len(tables) < 2:
        raise RuntimeError('模板里没有找到正文表格，共 %d 个表格' % len(tables))
    tb_s, tb_e = tables[1]
    tbl = doc[tb_s:tb_e]

    rows = find_elems(tbl, 'tr')
    if len(rows) != len(sections):
        raise RuntimeError('正文表格有 %d 行，但提供了 %d 节内容' % (len(rows), len(sections)))

    new_rows = []
    for idx, (r_s, r_e) in enumerate(rows):
        row = tbl[r_s:r_e]
        tcs = find_elems(row, 'tc')
        if not tcs:
            raise RuntimeError('第 %d 行没有单元格' % idx)
        c_s, c_e = tcs[0]
        cell = row[c_s:c_e]
        ps = find_elems(cell, 'p')
        if not ps:
            raise RuntimeError('第 %d 行的单元格没有段落' % idx)
        head_p = cell[ps[0][0]:ps[0][1]]          # 保留原有标题段落
        prefix = cell[:ps[0][0]]                   # <w:tc> + <w:tcPr>
        new_cell = prefix + head_p + sections[idx] + '</w:tc>'
        new_row = row[:c_s] + new_cell + row[c_e:]
        new_rows.append(new_row)

    new_tbl = tbl[:rows[0][0]] + ''.join(new_rows) + tbl[rows[-1][1]:]
    return doc[:tb_s] + new_tbl + doc[tb_e:]


def main(tpl_path, out_path):
    sections = report_content.all_sections()
    if len(sections) != 5:
        raise RuntimeError('需要 5 节内容，实际 %d' % len(sections))

    with zipfile.ZipFile(tpl_path) as z:
        names = z.namelist()
        doc_xml = z.read('word/document.xml').decode('utf-8')
        new_xml = build_document_xml(doc_xml, sections)

        tmp_out = out_path + '.tmp'
        with zipfile.ZipFile(tmp_out, 'w', zipfile.ZIP_DEFLATED) as zo:
            for n in names:
                if n == 'word/document.xml':
                    zo.writestr(n, new_xml.encode('utf-8'))
                else:
                    zo.writestr(n, z.read(n))
    if os.path.exists(out_path):
        os.remove(out_path)
    os.rename(tmp_out, out_path)
    print('已生成: %s' % out_path)
    print('  模板 document.xml  %d 字节' % len(doc_xml.encode('utf-8')))
    print('  报告 document.xml  %d 字节' % len(new_xml.encode('utf-8')))
    print('  部件数量 %d（与模板一致）' % len(names))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
