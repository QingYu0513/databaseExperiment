# -*- coding: utf-8 -*-
"""
从 Word 97-2003 二进制 .doc 中提取正文文本（纯标准库）。

流程：
  1. 解析 OLE2 / CFB 复合文档容器，拿到 WordDocument 和 0Table/1Table 两个流
  2. 从 FIB 里读 fcClx / lcbClx / ccpText
  3. 从 Table 流里解析 CLX 的 piece table（PlcPcd）
  4. 按 piece 拼出正文（压缩 piece 是 cp1252 单字节，非压缩是 UTF-16LE）
"""
import sys, struct

CFB_SIG = b'\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1'


class Ole2:
    def __init__(self, data):
        self.data = data
        assert data[:8] == CFB_SIG, '不是 OLE2 复合文档'
        self.sect_shift = struct.unpack_from('<H', data, 0x1E)[0]
        self.mini_shift = struct.unpack_from('<H', data, 0x20)[0]
        self.sect_size = 1 << self.sect_shift
        self.mini_size = 1 << self.mini_shift
        self.num_fat = struct.unpack_from('<I', data, 0x2C)[0]
        self.dir_start = struct.unpack_from('<I', data, 0x30)[0]
        self.mini_cutoff = struct.unpack_from('<I', data, 0x38)[0]
        self.minifat_start = struct.unpack_from('<I', data, 0x3C)[0]
        self.num_minifat = struct.unpack_from('<I', data, 0x40)[0]
        self.difat_start = struct.unpack_from('<I', data, 0x44)[0]
        self.num_difat = struct.unpack_from('<I', data, 0x48)[0]
        self._build_fat()
        self._build_dir()

    def sec_off(self, sect):
        return 512 + sect * self.sect_size

    def read_sector(self, sect):
        o = self.sec_off(sect)
        return self.data[o:o + self.sect_size]

    def _build_fat(self):
        # DIFAT：前 109 个 FAT 扇区号直接放在头部
        difat = list(struct.unpack_from('<109I', self.data, 0x4C))
        sect = self.difat_start
        cnt = 0
        per = self.sect_size // 4 - 1
        while sect != 0xFFFFFFFE and sect != 0xFFFFFFFF and cnt <= self.num_difat + 1:
            vals = struct.unpack('<%dI' % (self.sect_size // 4), self.read_sector(sect))
            difat.extend(vals[:per])
            sect = vals[per]
            cnt += 1
        fat = []
        for s in difat:
            if s in (0xFFFFFFFF, 0xFFFFFFFE):
                continue
            fat.extend(struct.unpack('<%dI' % (self.sect_size // 4), self.read_sector(s)))
        self.fat = fat

    def chain(self, start):
        out, cur, guard = [], start, 0
        while cur < len(self.fat) and cur not in (0xFFFFFFFE, 0xFFFFFFFF) and guard < 10 ** 7:
            out.append(cur)
            cur = self.fat[cur]
            guard += 1
        return out

    def read_stream_raw(self, start, size):
        buf = b''.join(self.read_sector(s) for s in self.chain(start))
        return buf[:size]

    def _build_dir(self):
        raw = b''.join(self.read_sector(s) for s in self.chain(self.dir_start))
        self.entries = []
        for i in range(0, len(raw), 128):
            e = raw[i:i + 128]
            if len(e) < 128:
                break
            nlen = struct.unpack_from('<H', e, 64)[0]
            name = e[:max(0, nlen - 2)].decode('utf-16-le', 'replace')
            typ = e[66]
            start = struct.unpack_from('<I', e, 116)[0]
            size = struct.unpack_from('<Q', e, 120)[0]
            self.entries.append({'name': name, 'type': typ, 'start': start, 'size': size})
        root = next((x for x in self.entries if x['type'] == 5), None)
        self.root = root
        # mini stream 容器
        self.mini_stream = b''
        if root:
            self.mini_stream = self.read_stream_raw(root['start'], root['size'])
        # mini FAT
        self.minifat = []
        cur, guard = self.minifat_start, 0
        while cur < len(self.fat) and cur not in (0xFFFFFFFE, 0xFFFFFFFF) and guard < 10 ** 7:
            self.minifat.extend(struct.unpack('<%dI' % (self.sect_size // 4), self.read_sector(cur)))
            cur = self.fat[cur]
            guard += 1

    def read_mini(self, start, size):
        out = b''
        cur, guard = start, 0
        while cur < len(self.minifat) and cur not in (0xFFFFFFFE, 0xFFFFFFFF) and guard < 10 ** 7:
            o = cur * self.mini_size
            out += self.mini_stream[o:o + self.mini_size]
            cur = self.minifat[cur]
            guard += 1
        return out[:size]

    def get(self, name):
        for e in self.entries:
            if e['name'] == name and e['type'] == 2:
                if e['size'] < self.mini_cutoff:
                    return self.read_mini(e['start'], e['size'])
                return self.read_stream_raw(e['start'], e['size'])
        return None

    def names(self):
        return [e['name'] for e in self.entries if e['name']]


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def extract_doc_text(path):
    with open(path, 'rb') as f:
        data = f.read()
    ole = Ole2(data)
    wd = ole.get('WordDocument')
    if not wd:
        raise RuntimeError('找不到 WordDocument 流，可用流: %s' % ole.names())

    flags = u16(wd, 0x0A)
    table_name = '1Table' if (flags & 0x0200) else '0Table'
    table = ole.get(table_name)
    if table is None:
        table = ole.get('1Table') or ole.get('0Table')
    if table is None:
        raise RuntimeError('找不到 Table 流')

    # --- 解析 FIB，定位 fcClx / lcbClx / ccpText ---
    pos = 0x20
    csw = u16(wd, pos); pos += 2 + csw * 2
    cslw = u16(wd, pos); pos += 2
    fibRgLw = pos
    ccpText = u32(wd, fibRgLw + 3 * 4)
    pos += cslw * 4
    cbRgFcLcb = u16(wd, pos); pos += 2
    fibRgFcLcb = pos
    fcClx = u32(wd, fibRgFcLcb + 33 * 8)
    lcbClx = u32(wd, fibRgFcLcb + 33 * 8 + 4)

    clx = table[fcClx:fcClx + lcbClx]

    # --- 跳过 Prc 数组，找到 clxt=2 的 PlcPcd ---
    i = 0
    while i < len(clx):
        if clx[i] == 1:
            cb = u16(clx, i + 1)
            i += 3 + cb
        elif clx[i] == 2:
            i += 1
            break
        else:
            i += 1
    lcbPlcPcd = u32(clx, i)
    plc = clx[i + 4: i + 4 + lcbPlcPcd]

    n = (lcbPlcPcd - 4) // 12
    cps = [u32(plc, k * 4) for k in range(n + 1)]
    pcd_off = (n + 1) * 4

    pieces = []
    for k in range(n):
        base = pcd_off + k * 8
        fc = u32(plc, base + 2)
        compressed = bool(fc & 0x40000000)
        off = (fc & 0x3FFFFFFF) // 2 if compressed else fc
        pieces.append((cps[k], cps[k + 1], off, compressed))

    # --- 拼正文 ---
    parts = []
    for cp_s, cp_e, off, compressed in pieces:
        cnt = cp_e - cp_s
        if cnt <= 0:
            continue
        if compressed:
            raw = wd[off:off + cnt]
            parts.append(raw.decode('cp1252', 'replace'))
        else:
            raw = wd[off:off + cnt * 2]
            parts.append(raw.decode('utf-16-le', 'replace'))
    text = ''.join(parts)
    if ccpText and ccpText <= len(text):
        text = text[:ccpText]
    return text


def normalize(text):
    """把 Word 的控制符换成可读形式。"""
    out = []
    for ch in text:
        o = ord(ch)
        if ch == '\r':
            out.append('\n')
        elif o == 0x07:
            out.append('\t|')       # 单元格/行结束
        elif o == 0x0B:
            out.append('\n')
        elif o == 0x0C:
            out.append('\n--- 分页 ---\n')
        elif o == 0x1E:
            out.append('-')
        elif o == 0x13 or o == 0x14 or o == 0x15:
            out.append('')          # 域代码标记
        elif o == 0x01:
            out.append('[图片]')
        elif o < 0x20 and ch not in '\t\n':
            out.append('')
        else:
            out.append(ch)
    return ''.join(out)


if __name__ == '__main__':
    t = extract_doc_text(sys.argv[1])
    sys.stdout.write(normalize(t))
