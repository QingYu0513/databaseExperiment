# -*- coding: utf-8 -*-
"""检查生成 docx 的 OOXML 结构是否合规：
1) 表格 gridCol 数 == 每行 tc 数
2) pPr / rPr 子元素顺序是否符合 CT_PPr / CT_RPr 规定
3) 每个 tc 至少含一个 p，且 tc/tbl 结尾结构正确
"""
import sys, zipfile
import xml.etree.ElementTree as ET

W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
q = lambda t: '{%s}%s' % (W, t)
local = lambda t: t.split('}')[-1]

PPR_ORDER = ['pStyle', 'keepNext', 'keepLines', 'pageBreakBefore', 'framePr', 'widowControl',
             'numPr', 'suppressLineNumbers', 'pBdr', 'shd', 'tabs', 'suppressAutoHyphens',
             'kinsoku', 'wordWrap', 'overflowPunct', 'topLinePunct', 'autoSpaceDE', 'autoSpaceDN',
             'bidi', 'adjustRightInd', 'snapToGrid', 'spacing', 'ind', 'contextualSpacing',
             'mirrorIndents', 'suppressOverlap', 'jc', 'textDirection', 'textAlignment',
             'textboxTightWrap', 'outlineLvl', 'divId', 'cnfStyle', 'rPr', 'sectPr', 'pPrChange']

RPR_ORDER = ['rStyle', 'rFonts', 'b', 'bCs', 'i', 'iCs', 'caps', 'smallCaps', 'strike', 'dstrike',
             'outline', 'shadow', 'emboss', 'imprint', 'noProof', 'snapToGrid', 'vanish',
             'webHidden', 'color', 'spacing', 'w', 'kern', 'position', 'sz', 'szCs', 'highlight',
             'u', 'effect', 'bdr', 'shd', 'fitText', 'vertAlign', 'rtl', 'cs', 'em', 'lang',
             'eastAsianLayout', 'specVanish', 'oMath']

TCPR_ORDER = ['cnfStyle', 'tcW', 'gridSpan', 'hMerge', 'vMerge', 'tcBorders', 'shd', 'noWrap',
              'tcMar', 'textDirection', 'tcFitText', 'vAlign', 'hideMark']

TBLPR_ORDER = ['tblStyle', 'tblpPr', 'tblOverlap', 'bidiVisual', 'tblStyleRowBandSize',
               'tblStyleColBandSize', 'tblW', 'jc', 'tblCellSpacing', 'tblInd', 'tblBorders',
               'shd', 'tblLayout', 'tblCellMar', 'tblLook', 'tblCaption', 'tblDescription']


def check_order(el, order, label, errs):
    idx = []
    for ch in el:
        n = local(ch.tag)
        if n in order:
            idx.append(order.index(n))
        else:
            errs.append('%s 出现未登记的子元素 %s' % (label, n))
            return
    if idx != sorted(idx):
        seq = [local(c.tag) for c in el]
        errs.append('%s 子元素顺序错误: %s' % (label, seq))


def main(path):
    with zipfile.ZipFile(path) as z:
        root = ET.fromstring(z.read('word/document.xml'))
    errs = []
    ntbl = ntc = np_ = 0

    for tbl in root.iter(q('tbl')):
        ntbl += 1
        pr = tbl.find(q('tblPr'))
        if pr is None:
            errs.append('表格缺少 tblPr')
        else:
            check_order(pr, TBLPR_ORDER, 'tblPr', errs)
        grid = tbl.find(q('tblGrid'))
        if grid is None:
            errs.append('表格缺少 tblGrid')
            continue
        ncol = len(grid.findall(q('gridCol')))
        for tr in tbl.findall(q('tr')):
            cells = tr.findall(q('tc'))
            if len(cells) != ncol:
                errs.append('行内单元格数 %d != gridCol 数 %d' % (len(cells), ncol))
        for tc in tbl.iter(q('tc')):
            ntc += 1
            cpr = tc.find(q('tcPr'))
            if cpr is not None:
                check_order(cpr, TCPR_ORDER, 'tcPr', errs)
            if tc.find(q('p')) is None:
                errs.append('单元格内没有段落')

    for p in root.iter(q('p')):
        np_ += 1
        ppr = p.find(q('pPr'))
        if ppr is not None:
            check_order(ppr, PPR_ORDER, 'pPr', errs)

    for r in root.iter(q('r')):
        rpr = r.find(q('rPr'))
        if rpr is not None:
            check_order(rpr, RPR_ORDER, 'rPr', errs)

    print('表格 %d 个，单元格 %d 个，段落 %d 个' % (ntbl, ntc, np_))
    if errs:
        print('发现 %d 个问题:' % len(errs))
        for e in errs[:40]:
            print('  -', e)
        return 1
    print('结构校验通过：无顺序违规、无单元格缺段落、gridCol 与单元格数一致')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
