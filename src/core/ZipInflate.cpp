#include "ZipInflate.hpp"
#include "WinLibs.hpp"
#include "StringUtil.hpp"
#include <vector>
#include <filesystem>

namespace mr {
namespace core {

namespace fs = std::filesystem;

namespace zipx {

const size_t WIN_SIZE = 32768;

struct BitIn {
    HANDLE h = INVALID_HANDLE_VALUE;
    unsigned long long left = 0;
    unsigned char buf[1 << 16];
    size_t bufPos = 0;
    size_t bufLen = 0;
    unsigned held = 0;
    bool bad = false;

    bool fill() {
        if (bufPos < bufLen) return true;
        if (left == 0) return false;
        DWORD want = static_cast<DWORD>((left < sizeof(buf)) ? left : static_cast<unsigned long long>(sizeof(buf)));
        DWORD got = 0;
        bufPos = 0;
        bufLen = 0;
        if (!ReadFile(h, buf, want, &got, nullptr) || got == 0) { bad = true; return false; }
        bufLen = got;
        left -= got;
        return true;
    }

    int bit() {
        if (!fill()) { bad = true; return 0; }
        int b = (buf[bufPos] >> held) & 1;
        if (++held == 8) { held = 0; bufPos++; }
        return b;
    }

    int bits(int need) {
        int val = 0;
        for (int i = 0; i < need; i++) val |= bit() << i;
        return val;
    }

    void alignByte() {
        if (held) { held = 0; bufPos++; }
    }

    int rawByte() {
        if (!fill()) { bad = true; return -1; }
        return buf[bufPos++];
    }
};

struct Win {
    HANDLE h = INVALID_HANDLE_VALUE;
    unsigned char ring[WIN_SIZE];
    size_t pos = 0;
    unsigned long long emitted = 0;
    bool bad = false;

    void put(unsigned char c) {
        ring[pos++] = c;
        emitted++;
        if (pos == WIN_SIZE) flush();
    }

    void flush() {
        if (!pos) return;
        DWORD wr = 0;
        if (!WriteFile(h, ring, static_cast<DWORD>(pos), &wr, nullptr) || wr != pos) bad = true;
        pos = 0;
    }

    unsigned char back(size_t dist) { return ring[(pos + WIN_SIZE - dist) & (WIN_SIZE - 1)]; }
};

struct Huff {
    short count[16];
    std::vector<short> symbol;
};

bool buildHuff(Huff& h, const short* lengths, int n) {
    for (int len = 0; len <= 15; len++) h.count[len] = 0;
    for (int sym = 0; sym < n; sym++) h.count[lengths[sym]]++;
    h.symbol.assign(n, 0);
    if (h.count[0] == n) return true;
    int left = 1;
    for (int len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return false;
    }
    short offs[16];
    offs[0] = 0;
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = static_cast<short>(offs[len] + h.count[len]);
    for (int sym = 0; sym < n; sym++) if (lengths[sym]) h.symbol[offs[lengths[sym]]++] = static_cast<short>(sym);
    return true;
}

int decodeSym(BitIn& s, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        int b = s.bit();
        if (s.bad) return -1;
        code |= b;
        int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const short LEN_BASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const short LEN_EXTRA[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const short DIST_BASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const short DIST_EXTRA[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

bool inflateCodes(BitIn& s, Win& w, const Huff& lencode, const Huff& distcode, unsigned long long cap) {
    for (;;) {
        int sym = decodeSym(s, lencode);
        if (sym < 0) return false;
        if (sym < 256) {
            w.put(static_cast<unsigned char>(sym));
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            size_t len = LEN_BASE[sym] + static_cast<size_t>(s.bits(LEN_EXTRA[sym]));
            int dsym = decodeSym(s, distcode);
            if (dsym < 0 || dsym >= 30) return false;
            size_t dist = DIST_BASE[dsym] + static_cast<size_t>(s.bits(DIST_EXTRA[dsym]));
            if (dist == 0 || dist > WIN_SIZE) return false;
            if (w.emitted + len > cap) return false;
            for (size_t i = 0; i < len; i++) w.put(w.back(dist));
        }
        if (s.bad || w.bad) return false;
    }
}

bool inflateStored(BitIn& s, Win& w, unsigned long long cap) {
    s.alignByte();
    int b0 = s.rawByte(), b1 = s.rawByte(), n0 = s.rawByte(), n1 = s.rawByte();
    if (b0 < 0 || b1 < 0 || n0 < 0 || n1 < 0) return false;
    unsigned len = static_cast<unsigned>(b0) | (static_cast<unsigned>(b1) << 8);
    unsigned nlen = static_cast<unsigned>(n0) | (static_cast<unsigned>(n1) << 8);
    if ((len ^ 0xFFFFu) != nlen) return false;
    if (w.emitted + len > cap) return false;
    for (unsigned i = 0; i < len; i++) {
        int c = s.rawByte();
        if (c < 0) return false;
        w.put(static_cast<unsigned char>(c));
    }
    return !w.bad;
}

bool inflateFixed(BitIn& s, Win& w, unsigned long long cap) {
    short lengths[288];
    int i = 0;
    for (; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    Huff lencode, distcode;
    if (!buildHuff(lencode, lengths, 288)) return false;
    for (i = 0; i < 30; i++) lengths[i] = 5;
    if (!buildHuff(distcode, lengths, 30)) return false;
    return inflateCodes(s, w, lencode, distcode, cap);
}

bool inflateDynamic(BitIn& s, Win& w, unsigned long long cap) {
    static const short ORDER[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    int nlen = s.bits(5) + 257;
    int ndist = s.bits(5) + 1;
    int ncode = s.bits(4) + 4;
    if (s.bad || nlen > 286 || ndist > 30) return false;

    short lengths[320];
    for (int i = 0; i < 19; i++) lengths[i] = 0;
    for (int i = 0; i < ncode; i++) lengths[ORDER[i]] = static_cast<short>(s.bits(3));

    Huff clen;
    if (!buildHuff(clen, lengths, 19)) return false;

    int index = 0;
    while (index < nlen + ndist) {
        int sym = decodeSym(s, clen);
        if (sym < 0) return false;
        if (sym < 16) {
            lengths[index++] = static_cast<short>(sym);
            continue;
        }
        int repeat = 0, filler = 0;
        if (sym == 16) {
            if (index == 0) return false;
            filler = lengths[index - 1];
            repeat = 3 + s.bits(2);
        } else if (sym == 17) {
            repeat = 3 + s.bits(3);
        } else {
            repeat = 11 + s.bits(7);
        }
        if (s.bad || index + repeat > nlen + ndist) return false;
        while (repeat-- > 0) lengths[index++] = static_cast<short>(filler);
    }
    if (lengths[256] == 0) return false;

    Huff lencode, distcode;
    if (!buildHuff(lencode, lengths, nlen)) return false;
    if (!buildHuff(distcode, lengths + nlen, ndist)) return false;
    return inflateCodes(s, w, lencode, distcode, cap);
}

bool inflateDeflate(HANDLE zip, unsigned long long dataOff, unsigned long long compSize,
                    unsigned long long uncompSize, HANDLE out) {
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(dataOff);
    if (!SetFilePointerEx(zip, li, nullptr, FILE_BEGIN)) return false;

    BitIn s;
    s.h = zip;
    s.left = compSize;
    Win w;
    w.h = out;

    for (;;) {
        int last = s.bit();
        int type = s.bits(2);
        if (s.bad) return false;
        bool ok;
        if (type == 0) ok = inflateStored(s, w, uncompSize);
        else if (type == 1) ok = inflateFixed(s, w, uncompSize);
        else if (type == 2) ok = inflateDynamic(s, w, uncompSize);
        else return false;
        if (!ok) return false;
        if (last) break;
    }

    w.flush();
    return !w.bad && w.emitted == uncompSize;
}

bool copyStored(HANDLE zip, unsigned long long off, unsigned long long size, HANDLE out) {
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(off);
    if (!SetFilePointerEx(zip, li, nullptr, FILE_BEGIN)) return false;
    unsigned char buf[1 << 16];
    unsigned long long left = size;
    while (left) {
        DWORD want = static_cast<DWORD>((left < sizeof(buf)) ? left : static_cast<unsigned long long>(sizeof(buf)));
        DWORD got = 0;
        if (!ReadFile(zip, buf, want, &got, nullptr) || got == 0) return false;
        DWORD wr = 0;
        if (!WriteFile(out, buf, got, &wr, nullptr) || wr != got) return false;
        left -= got;
    }
    return true;
}

bool readAt(HANDLE h, unsigned long long off, void* dst, size_t n) {
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(off);
    if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) return false;
    auto* d = static_cast<unsigned char*>(dst);
    size_t got = 0;
    while (got < n) {
        DWORD r = 0;
        if (!ReadFile(h, d + got, static_cast<DWORD>(n - got), &r, nullptr) || r == 0) return false;
        got += r;
    }
    return true;
}

struct CentralEntry {
    std::string name;
    unsigned method = 0;
    unsigned long long compSize = 0;
    unsigned long long uncompSize = 0;
    unsigned long long localOffset = 0;
};

inline unsigned rd16(const unsigned char* p) { return static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8); }
inline unsigned long rd32(const unsigned char* p) {
    return static_cast<unsigned long>(p[0]) | (static_cast<unsigned long>(p[1]) << 8) |
           (static_cast<unsigned long>(p[2]) << 16) | (static_cast<unsigned long>(p[3]) << 24);
}
inline unsigned long long rd64(const unsigned char* p) {
    return static_cast<unsigned long long>(rd32(p)) | (static_cast<unsigned long long>(rd32(p + 4)) << 32);
}

bool safeRelativePath(const std::string& rawName, std::string& outRel, bool& isDir) {
    std::string rel;
    rel.reserve(rawName.size());
    for (char c : rawName) rel.push_back((c == '\\') ? '/' : c);

    isDir = !rel.empty() && rel.back() == '/';
    if (rel.empty() || rel.front() == '/') return false;
    if (rel.size() >= 2 && rel[1] == ':') return false;

    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i <= rel.size(); i++) {
        char c = (i < rel.size()) ? rel[i] : '/';
        if (c == '/') {
            if (cur == "..") return false;
            if (!cur.empty() && cur != ".") parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (parts.empty()) return false;

    outRel.clear();
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) outRel += "/";
        outRel += parts[i];
    }
    return true;
}

} // namespace zipx

bool ZipInflate::extractZip(const std::string& zipPath, const std::string& destDir, ProgressCallback onProgress) {
    using namespace zipx;
    std::error_code ec;
    if (!fs::exists(fs::u8path(zipPath), ec) || !fs::is_directory(fs::u8path(destDir), ec)) return false;

    HANDLE zip = CreateFileW(utf8ToWstring(zipPath).c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (zip == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fileSizeLi;
    if (!GetFileSizeEx(zip, &fileSizeLi)) { CloseHandle(zip); return false; }
    const unsigned long long fileSize = static_cast<unsigned long long>(fileSizeLi.QuadPart);
    if (fileSize < 22) { CloseHandle(zip); return false; }

    const unsigned long long tailStart = (fileSize > 66000ULL) ? (fileSize - 66000ULL) : 0ULL;
    const size_t tailLen = static_cast<size_t>(fileSize - tailStart);
    std::vector<unsigned char> tail(tailLen);
    if (!readAt(zip, tailStart, tail.data(), tailLen)) { CloseHandle(zip); return false; }

    long eocd = -1;
    for (size_t i = tailLen - 21; i-- > 0;) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4b && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            if (i + 22 + rd16(&tail[i + 20]) == tailLen) { eocd = static_cast<long>(i); break; }
        }
    }
    if (eocd < 0) { CloseHandle(zip); return false; }

    unsigned long long entryCount = rd16(&tail[eocd + 10]);
    unsigned long long cdOffset = rd32(&tail[eocd + 16]);

    if (entryCount == 0xFFFFULL || cdOffset == 0xFFFFFFFFULL) {
        if (static_cast<size_t>(eocd) < 20) { CloseHandle(zip); return false; }
        const unsigned char* loc = &tail[eocd - 20];
        if (!(loc[0] == 0x50 && loc[1] == 0x4b && loc[2] == 0x06 && loc[3] == 0x07)) {
            CloseHandle(zip); return false;
        }
        unsigned char hdr[56];
        if (!readAt(zip, rd64(loc + 8), hdr, 56)) { CloseHandle(zip); return false; }
        if (!(hdr[0] == 0x50 && hdr[1] == 0x4b && hdr[2] == 0x06 && hdr[3] == 0x06)) {
            CloseHandle(zip); return false;
        }
        entryCount = rd64(hdr + 32);
        cdOffset = rd64(hdr + 48);
    }

    std::vector<CentralEntry> entries;
    unsigned long long p = cdOffset;
    for (unsigned long long i = 0; i < entryCount; i++) {
        unsigned char h[46];
        if (!readAt(zip, p, h, 46)) break;
        if (h[0] != 0x50 || h[1] != 0x4b || h[2] != 0x01 || h[3] != 0x02) break;

        CentralEntry e;
        e.method = rd16(h + 10);
        e.compSize = rd32(h + 20);
        e.uncompSize = rd32(h + 24);
        const unsigned nameLen = rd16(h + 28);
        const unsigned extraLen = rd16(h + 30);
        const unsigned commLen = rd16(h + 32);
        e.localOffset = rd32(h + 42);

        std::vector<unsigned char> var(static_cast<size_t>(nameLen) + extraLen + commLen);
        if (!var.empty() && !readAt(zip, p + 46, var.data(), var.size())) break;
        e.name.assign(reinterpret_cast<const char*>(var.data()), nameLen);

        size_t ex = nameLen;
        while (ex + 4 <= static_cast<size_t>(nameLen) + extraLen) {
            const unsigned hid = rd16(&var[ex]);
            const unsigned hsz = rd16(&var[ex + 2]);
            if (ex + 4 + hsz > static_cast<size_t>(nameLen) + extraLen) break;
            if (hid == 0x0001) {
                size_t q = ex + 4;
                const size_t end = ex + 4 + hsz;
                if (e.uncompSize == 0xFFFFFFFFULL && q + 8 <= end) { e.uncompSize = rd64(&var[q]); q += 8; }
                if (e.compSize == 0xFFFFFFFFULL && q + 8 <= end) { e.compSize = rd64(&var[q]); q += 8; }
                if (e.localOffset == 0xFFFFFFFFULL && q + 8 <= end) { e.localOffset = rd64(&var[q]); }
            }
            ex += 4 + hsz;
        }

        entries.push_back(e);
        p += 46 + nameLen + extraLen + commLen;
    }

    bool ok = !entries.empty();
    unsigned long long totalOut = 0;
    if (ok) {
        for (const auto& e : entries) totalOut += e.uncompSize;
        unsigned long long done = 0;

        for (const auto& e : entries) {
            std::string rel;
            bool isDir = false;
            if (!safeRelativePath(e.name, rel, isDir)) continue;

            fs::path outPath = fs::u8path(destDir) / fs::u8path(rel);
            std::error_code ecDir;
            if (isDir) {
                fs::create_directories(outPath, ecDir);
                continue;
            }
            if (outPath.has_parent_path()) fs::create_directories(outPath.parent_path(), ecDir);
            if (ecDir) continue;

            unsigned char lh[30];
            if (!readAt(zip, e.localOffset, lh, 30)) { ok = false; break; }
            if (lh[0] != 0x50 || lh[1] != 0x4b || lh[2] != 0x03 || lh[3] != 0x04) { ok = false; break; }
            const unsigned long long dataOff = e.localOffset + 30 + rd16(lh + 26) + rd16(lh + 28);

            HANDLE out = CreateFileW(outPath.wstring().c_str(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (out == INVALID_HANDLE_VALUE) continue;

            bool entryOk = false;
            if (e.method == 0) {
                entryOk = copyStored(zip, dataOff, e.compSize, out);
            } else if (e.method == 8) {
                entryOk = inflateDeflate(zip, dataOff, e.compSize, e.uncompSize, out);
            }
            CloseHandle(out);

            if (!entryOk) { ok = false; break; }

            done += e.uncompSize;
            if (totalOut && onProgress) {
                char info[64];
                snprintf(info, sizeof(info), "(%.1f / %.1f MB)",
                         static_cast<double>(done) / 1048576.0,
                         static_cast<double>(totalOut) / 1048576.0);
                onProgress(static_cast<double>(done) * 100.0 / static_cast<double>(totalOut), info);
            }
        }
    }

    CloseHandle(zip);
    return ok;
}

} // namespace core
} // namespace mr
