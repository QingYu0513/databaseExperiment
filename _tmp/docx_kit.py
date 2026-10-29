# -*- coding: utf-8 -*-
"""
基于模板 docx 生成《数据库系统》课程实验报告 —— 实验一。

做法：把模板原样复制，只替换 word/document.xml 里正文表格五个单元格的内容，
其余部件（styles/header/footer/media/theme）全部保持模板原样，因此版式与模板一致。
原模板文件不做任何修改。
"""
import sys, os, zipfile, shutil
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape

W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
XMLNS = 'http://www.w3.org/XML/1998/namespace'
ET.register_namespace('w', W)
for pfx, uri in [
    ('wpc', 'http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas'),
    ('mc', 'http://schemas.openxmlformats.org/markup-compatibility/2006'),
    ('o', 'urn:schemas-microsoft-com:office:office'),
    ('r', 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'),
    ('m', 'http://schemas.openxmlformats.org/officeDocument/2006/math'),
    ('v', 'urn:schemas-microsoft-com:vml'),
    ('wp', 'http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing'),
    ('w10', 'urn:schemas-microsoft-com:office:word'),
    ('w14', 'http://schemas.microsoft.com/office/word/2010/wordml'),
    ('w15', 'http://schemas.microsoft.com/office/word/2012/wordml'),
    ('wpic', 'http://schemas.microsoft.com/office/word/2010/wordpicture'),
    ('wne', 'http://schemas.microsoft.com/office/word/2006/wordml'),
]:
    try:
        ET.register_namespace(pfx, uri)
    except Exception:
        pass

def q(t):
    return '{%s}%s' % (W, t)

# ---------------------------------------------------------------- XML 片段构造

BODY_FONT = '宋体'
HEAD_FONT = '微软雅黑'
CODE_FONT = 'Consolas'


def rpr(style, size_half_pt, bold=False, color=None, font=None):
    """构造 <w:rPr>。style: 'body' / 'head' / 'code'。"""
    if font is None:
        font = CODE_FONT if style == 'code' else (HEAD_FONT if style == 'head' else BODY_FONT)
    east = CODE_FONT if style == 'code' else (HEAD_FONT if style == 'head' else BODY_FONT)
    parts = ['<w:rFonts w:ascii="%s" w:hAnsi="%s" w:eastAsia="%s" w:cs="%s"/>' % (font, font, east, font)]
    if bold:
        parts.append('<w:b/><w:bCs/>')
    if color:
        parts.append('<w:color w:val="%s"/>' % color)
    parts.append('<w:sz w:val="%d"/><w:szCs w:val="%d"/>' % (size_half_pt, size_half_pt))
    parts.append('<w:lang w:val="en-US" w:eastAsia="zh-CN"/>')
    return '<w:rPr>%s</w:rPr>' % ''.join(parts)


def run(text, style='body', size=None, bold=False, color=None):
    if size is None:
        size = {'body': 21, 'head': 22, 'code': 18}[style]
    return '<w:r>%s<w:t xml:space="preserve">%s</w:t></w:r>' % (
        rpr(style, size, bold, color), escape(text))


def para(inner, ppr=''):
    return '<w:p>%s%s</w:p>' % (('<w:pPr>%s</w:pPr>' % ppr) if ppr else '', inner)


PPR_BODY = ('<w:widowControl/><w:spacing w:before="40" w:after="40" w:line="340" w:lineRule="auto"/>'
            '<w:ind w:firstLine="420" w:firstLineChars="200"/><w:jc w:val="both"/>')
PPR_HEAD = ('<w:keepNext/><w:widowControl/><w:spacing w:before="180" w:after="80" w:line="340" w:lineRule="auto"/>'
            '<w:jc w:val="left"/>')
PPR_CODE = ('<w:widowControl/><w:shd w:val="clear" w:color="auto" w:fill="F4F4F4"/>'
            '<w:spacing w:before="20" w:after="20" w:line="260" w:lineRule="auto"/>'
            '<w:ind w:left="420" w:right="200"/><w:jc w:val="left"/>')
PPR_LI = ('<w:widowControl/><w:spacing w:before="20" w:after="20" w:line="340" w:lineRule="auto"/>'
          '<w:ind w:left="420" w:hanging="0"/><w:jc w:val="both"/>')
PPR_NOTE = ('<w:widowControl/><w:spacing w:before="40" w:after="40" w:line="320" w:lineRule="auto"/>'
            '<w:ind w:left="420"/><w:jc w:val="both"/>')


def P(text):
    return para(run(text, 'body'), PPR_BODY)


def H(text):
    return para(run(text, 'head', size=23, bold=True, color='1F3864'), PPR_HEAD)


def CODE(lines):
    out = []
    for ln in (lines if isinstance(lines, list) else lines.split('\n')):
        out.append(para(run(ln if ln else ' ', 'code'), PPR_CODE))
    return ''.join(out)


def LI(text):
    return para(run(text, 'body'), PPR_LI)


def NOTE(text):
    return para(run(text, 'body', size=19, color='595959'), PPR_NOTE)


def cell(text, width, bold=False, size=19):
    return ('<w:tc><w:tcPr><w:tcW w:w="%d" w:type="dxa"/>'
            '<w:tcBorders><w:top w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
            '<w:left w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
            '<w:bottom w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
            '<w:right w:val="single" w:sz="4" w:space="0" w:color="808080"/></w:tcBorders>'
            '<w:vAlign w:val="center"/></w:tcPr>'
            '<w:p><w:pPr><w:spacing w:before="30" w:after="30" w:line="280" w:lineRule="auto"/>'
            '<w:jc w:val="left"/></w:pPr>%s</w:p></w:tc>'
            ) % (width, run(text, 'body', size=size, bold=bold))


def TABLE(rows, widths, header=True):
    """rows: list of list of str；widths: 每列宽（dxa），总和建议 <= 8300。"""
    grid = ''.join('<w:gridCol w:w="%d"/>' % w for w in widths)
    out = ['<w:tbl><w:tblPr><w:tblW w:w="%d" w:type="dxa"/><w:jc w:val="left"/>'
           '<w:tblBorders>'
           '<w:top w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
           '<w:left w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
           '<w:bottom w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
           '<w:right w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
           '<w:insideH w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
           '<w:insideV w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
           '</w:tblBorders>'
           '<w:tblLayout w:type="fixed"/>'
           '<w:tblCellMar><w:top w:w="28" w:type="dxa"/><w:left w:w="85" w:type="dxa"/>'
           '<w:bottom w:w="28" w:type="dxa"/><w:right w:w="85" w:type="dxa"/></w:tblCellMar>'
           '</w:tblPr><w:tblGrid>%s</w:tblGrid>' % (sum(widths), grid)]
    for ri, row in enumerate(rows):
        is_head = header and ri == 0
        out.append('<w:tr>')
        if is_head:
            out.append('<w:trPr><w:tblHeader/></w:trPr>')
        for ci, txt in enumerate(row):
            out.append(cell(txt, widths[ci], bold=is_head, size=18 if is_head else 18))
        out.append('</w:tr>')
    out.append('</w:tbl>')
    # 表格后补一个空段落，避免表格与下一段粘连
    out.append('<w:p><w:pPr><w:spacing w:before="0" w:after="60" w:line="200" w:lineRule="auto"/>'
               '</w:pPr></w:p>')
    return ''.join(out)


def SPACER():
    return '<w:p><w:pPr><w:spacing w:before="0" w:after="0" w:line="120" w:lineRule="exact"/>' \
           '</w:pPr></w:p>'
