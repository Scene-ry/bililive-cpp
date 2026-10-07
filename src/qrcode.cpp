// qrcode.cpp - QR 编码器实现
//
// 结构参考 Nayuki 的经典实现思路（MIT）重写：
//  Byte 模式编码 -> 自动版本 -> ECC 交织 -> 功能图形 -> 数据放置 -> 8 种掩码评估选优。
//  编码正确性通过与 npm `qrcode` 库逐矩阵对照验证（tools/verify_qr.js）。
#include "qrcode.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

namespace qrc {

namespace {

using std::vector;
using std::array;

// ---- 每版本每纠错级别的 ECC 码字数 / 纠错块数（QR 规范表） ----
const signed char kEccCodewordsPerBlock[4][41] = {
    // 0,   1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40
    {-1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // L
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},  // M
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // Q
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // H
};

const signed char kNumErrorCorrectionBlocks[4][41] = {
    // 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40
    {-1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  4,  4,  4,  4,  4,  6,  6,  6,  6,  7,  8,  8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},  // L
    {-1,  1,  1,  1,  2,  2,  4,  4,  4,  5,  5,  5,  8,  9,  9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},  // M
    {-1,  1,  1,  2,  2,  4,  4,  6,  6,  8,  8,  8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},  // Q
    {-1,  1,  1,  2,  4,  4,  4,  5,  6,  8,  8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},  // H
};

// 版本 1..40 的原始数据模块位数
int GetNumRawDataModules(int ver) {
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7) result -= 36;
    }
    return result;
}

// 版本 1..40、指定纠错级别的数据码字数
int GetNumDataCodewords(int ver, int ecl) {
    return GetNumRawDataModules(ver) / 8 -
           kEccCodewordsPerBlock[ecl][ver] * kNumErrorCorrectionBlocks[ecl][ver];
}

// ---- GF(256) 运算（本原多项式 0x11D） ----
int RsMultiply(int x, int y) {
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return z & 0xFF;
}

vector<uint8_t> RsComputeDivisor(int degree) {
    vector<uint8_t> result((size_t)degree, 0);
    result[(size_t)degree - 1] = 1;  // 从单项式 x^0 开始
    uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            result[(size_t)j] = (uint8_t)RsMultiply(result[(size_t)j], root);
            if (j + 1 < degree) result[(size_t)j] ^= result[(size_t)j + 1];
        }
        root = (uint8_t)RsMultiply(root, 0x02);
    }
    return result;
}

vector<uint8_t> RsComputeRemainder(const vector<uint8_t>& data, const vector<uint8_t>& divisor) {
    vector<uint8_t> result(divisor.size(), 0);
    for (uint8_t b : data) {
        int factor = b ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (size_t i = 0; i < result.size(); ++i) {
            result[i] ^= (uint8_t)RsMultiply(divisor[i], factor);
        }
    }
    return result;
}

// ---- 位缓冲 ----
struct BitBuffer {
    vector<bool> bits;
    void appendBits(uint32_t val, int len) {
        for (int i = len - 1; i >= 0; --i) bits.push_back(((val >> i) & 1) != 0);
    }
};

// ---- ECC 与交织（返回最终码字序列） ----
vector<uint8_t> AddEccAndInterleave(const vector<uint8_t>& data, int ver, int ecl) {
    int numBlocks = kNumErrorCorrectionBlocks[ecl][ver];
    int blockEccLen = kEccCodewordsPerBlock[ecl][ver];
    int rawCodewords = GetNumRawDataModules(ver) / 8;
    int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    int shortBlockLen = rawCodewords / numBlocks;

    vector<vector<uint8_t>> blocks;
    const vector<uint8_t> rsDiv = RsComputeDivisor(blockEccLen);
    for (int i = 0, k = 0; i < numBlocks; ++i) {
        size_t datLen = (size_t)(shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1));
        vector<uint8_t> dat(data.begin() + k, data.begin() + k + (ptrdiff_t)datLen);
        k += (int)dat.size();
        vector<uint8_t> ecc = RsComputeRemainder(dat, rsDiv);
        if (i < numShortBlocks) dat.push_back(0);  // 短块占位（交织时跳过）
        dat.insert(dat.end(), ecc.begin(), ecc.end());
        blocks.push_back(std::move(dat));
    }

    vector<uint8_t> result;
    result.reserve((size_t)rawCodewords);
    for (size_t i = 0; i < blocks[0].size(); ++i) {
        for (size_t j = 0; j < blocks.size(); ++j) {
            if (i != (size_t)(shortBlockLen - blockEccLen) || j >= (size_t)numShortBlocks) {
                result.push_back(blocks[j][i]);
            }
        }
    }
    return result;
}

// ---- 矩阵构建 ----
struct QrMatrix {
    int version;
    int size;
    int ecl;
    int mask;
    vector<vector<bool>> modules;
    vector<vector<bool>> isFunction;

    QrMatrix(int ver, int ecl_) : version(ver), size(ver * 4 + 17), ecl(ecl_), mask(0) {
        modules.assign((size_t)size, vector<bool>((size_t)size, false));
        isFunction.assign((size_t)size, vector<bool>((size_t)size, false));
    }

    void setFunctionModule(int x, int y, bool isDark) {
        modules[(size_t)y][(size_t)x] = isDark;
        isFunction[(size_t)y][(size_t)x] = true;
    }

    static bool getBit(uint32_t x, int i) { return ((x >> i) & 1) != 0; }

    void drawFinderPattern(int x, int y) {
        for (int dy = -4; dy <= 4; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                int dist = std::max(std::abs(dx), std::abs(dy));
                int xx = x + dx, yy = y + dy;
                if (xx >= 0 && xx < size && yy >= 0 && yy < size) {
                    setFunctionModule(xx, yy, dist != 2 && dist != 4);
                }
            }
        }
    }

    void drawAlignmentPattern(int x, int y) {
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                setFunctionModule(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
            }
        }
    }

    vector<int> getAlignmentPatternPositions() const {
        vector<int> result;
        if (version == 1) return result;
        int numAlign = version / 7 + 2;
        int step = (version == 32) ? 26 : (version * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
        for (int i = 0, pos = size - 7; i < numAlign - 1; ++i, pos -= step) {
            result.insert(result.begin(), pos);
        }
        result.insert(result.begin(), 6);
        return result;
    }

    void drawFormatBits(int msk) {
        // 纠错级别格式位：L=1, M=0, Q=3, H=2
        static const int kFormatBits[4] = {1, 0, 3, 2};
        int data = kFormatBits[ecl] << 3 | msk;
        int rem = data;
        for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        int bits = (data << 10 | rem) ^ 0x5412;

        for (int i = 0; i <= 5; ++i) setFunctionModule(8, i, getBit((uint32_t)bits, i));
        setFunctionModule(8, 7, getBit((uint32_t)bits, 6));
        setFunctionModule(8, 8, getBit((uint32_t)bits, 7));
        setFunctionModule(7, 8, getBit((uint32_t)bits, 8));
        for (int i = 9; i < 15; ++i) setFunctionModule(14 - i, 8, getBit((uint32_t)bits, i));

        for (int i = 0; i < 8; ++i) setFunctionModule(size - 1 - i, 8, getBit((uint32_t)bits, i));
        for (int i = 8; i < 15; ++i) setFunctionModule(8, size - 15 + i, getBit((uint32_t)bits, i));
        setFunctionModule(8, size - 8, true);  // 固定深色模块
    }

    void drawVersion() {
        if (version < 7) return;
        int rem = version;
        for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        uint32_t bits = (uint32_t)version << 12 | (uint32_t)rem;
        for (int i = 0; i < 18; ++i) {
            bool bit = getBit(bits, i);
            int a = size - 11 + i % 3;
            int b = i / 3;
            setFunctionModule(a, b, bit);
            setFunctionModule(b, a, bit);
        }
    }

    void drawFunctionPatterns() {
        // 时序图形
        for (int i = 0; i < size; ++i) {
            setFunctionModule(6, i, i % 2 == 0);
            setFunctionModule(i, 6, i % 2 == 0);
        }
        // 三个定位图形
        drawFinderPattern(3, 3);
        drawFinderPattern(size - 4, 3);
        drawFinderPattern(3, size - 4);
        // 校正图形
        vector<int> alignPos = getAlignmentPatternPositions();
        int n = (int)alignPos.size();
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                if (!((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))) {
                    drawAlignmentPattern(alignPos[(size_t)i], alignPos[(size_t)j]);
                }
            }
        }
        drawFormatBits(0);
        drawVersion();
    }

    void drawCodewords(const vector<uint8_t>& data) {
        size_t i = 0;
        for (int right = size - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;
            for (int vert = 0; vert < size; ++vert) {
                for (int j = 0; j < 2; ++j) {
                    int x = right - j;
                    bool upward = ((right + 1) & 2) == 0;
                    int y = upward ? size - 1 - vert : vert;
                    if (!isFunction[(size_t)y][(size_t)x] && i < data.size() * 8) {
                        modules[(size_t)y][(size_t)x] = getBit(data[i >> 3], 7 - (int)(i & 7));
                        ++i;
                    }
                    // 剩余模块保持浅色，稍后由掩码应用
                }
            }
        }
    }

    void applyMask(int msk) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                bool invert;
                switch (msk) {
                    case 0: invert = (x + y) % 2 == 0; break;
                    case 1: invert = y % 2 == 0; break;
                    case 2: invert = x % 3 == 0; break;
                    case 3: invert = (x + y) % 3 == 0; break;
                    case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                    case 5: invert = x * y % 2 + x * y % 3 == 0; break;
                    case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
                    case 7: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                    default: return;
                }
                if (invert && !isFunction[(size_t)y][(size_t)x]) {
                    modules[(size_t)y][(size_t)x] = !modules[(size_t)y][(size_t)x];
                }
            }
        }
    }

    // ---- 掩码惩罚分（QR 规范 4 条规则） ----
    static const int kPenaltyN1 = 3;
    static const int kPenaltyN2 = 3;
    static const int kPenaltyN3 = 40;
    static const int kPenaltyN4 = 10;

    void finderPenaltyAddHistory(int currentRunLength, array<int, 7>& runHistory) const {
        if (runHistory[0] == 0) currentRunLength += size;  // 加浅色边界
        std::copy_backward(runHistory.begin(), runHistory.end() - 1, runHistory.end());
        runHistory[0] = currentRunLength;
    }

    static bool finderPenaltyCountPatterns(const array<int, 7>& runHistory) {
        int n = runHistory[1];
        bool core = n > 0 && runHistory[2] == n && runHistory[3] == n * 3 && runHistory[4] == n &&
                    runHistory[5] == n;
        return (core && runHistory[0] >= n * 4 && runHistory[6] >= n) ||
               (core && runHistory[6] >= n * 4 && runHistory[0] >= n);
    }

    int finderPenaltyTerminateAndCount(bool currentRunColor, int currentRunLength,
                                       array<int, 7>& runHistory) const {
        if (currentRunColor) {
            finderPenaltyAddHistory(currentRunLength, runHistory);
            currentRunLength = 0;
        }
        currentRunLength += size;  // 加浅色边界
        finderPenaltyAddHistory(currentRunLength, runHistory);
        return finderPenaltyCountPatterns(runHistory) ? 1 : 0;
    }

    long getPenaltyScore() const {
        long result = 0;

        // 行方向：连续同色 + 类似定位图形
        for (int y = 0; y < size; ++y) {
            bool runColor = false;
            int runX = 0;
            array<int, 7> runHistory = {};
            for (int x = 0; x < size; ++x) {
                if (modules[(size_t)y][(size_t)x] == runColor) {
                    ++runX;
                    if (runX == 5) result += kPenaltyN1;
                    else if (runX > 5) ++result;
                } else {
                    finderPenaltyAddHistory(runX, runHistory);
                    if (!runColor) {
                        result += (finderPenaltyCountPatterns(runHistory) ? 1 : 0) * kPenaltyN3;
                    }
                    runColor = modules[(size_t)y][(size_t)x];
                    runX = 1;
                }
            }
            result += finderPenaltyTerminateAndCount(runColor, runX, runHistory) * kPenaltyN3;
        }
        // 列方向
        for (int x = 0; x < size; ++x) {
            bool runColor = false;
            int runY = 0;
            array<int, 7> runHistory = {};
            for (int y = 0; y < size; ++y) {
                if (modules[(size_t)y][(size_t)x] == runColor) {
                    ++runY;
                    if (runY == 5) result += kPenaltyN1;
                    else if (runY > 5) ++result;
                } else {
                    finderPenaltyAddHistory(runY, runHistory);
                    if (!runColor) {
                        result += (finderPenaltyCountPatterns(runHistory) ? 1 : 0) * kPenaltyN3;
                    }
                    runColor = modules[(size_t)y][(size_t)x];
                    runY = 1;
                }
            }
            result += finderPenaltyTerminateAndCount(runColor, runY, runHistory) * kPenaltyN3;
        }

        // 2x2 同色块
        for (int y = 0; y < size - 1; ++y) {
            for (int x = 0; x < size - 1; ++x) {
                bool c = modules[(size_t)y][(size_t)x];
                if (c == modules[(size_t)y][(size_t)x + 1] && c == modules[(size_t)y + 1][(size_t)x] &&
                    c == modules[(size_t)y + 1][(size_t)x + 1]) {
                    result += kPenaltyN2;
                }
            }
        }

        // 深色比例平衡
        int dark = 0;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                if (modules[(size_t)y][(size_t)x]) ++dark;
            }
        }
        int total = size * size;
        int k = (std::abs(dark * 20 - total * 10) + total - 1) / total - 1;
        result += (long)k * kPenaltyN4;
        return result;
    }
};

// 计算数据码字（含填充）
vector<uint8_t> BuildDataCodewords(const std::string& text, int ver, int ecl) {
    BitBuffer bb;
    // Byte 模式
    bb.appendBits(0x4, 4);
    int ccBits = (ver <= 9) ? 8 : 16;
    bb.appendBits((uint32_t)text.size(), ccBits);
    for (unsigned char c : text) bb.appendBits(c, 8);

    int dataCapacityBits = GetNumDataCodewords(ver, ecl) * 8;
    // 终止符
    int terminator = std::min(4, dataCapacityBits - (int)bb.bits.size());
    bb.appendBits(0, terminator);
    // 补齐到字节边界
    bb.appendBits(0, (8 - (int)bb.bits.size() % 8) % 8);

    vector<uint8_t> data;
    data.reserve((size_t)(dataCapacityBits / 8));
    for (size_t i = 0; i < bb.bits.size(); i += 8) {
        uint8_t b = 0;
        for (int j = 0; j < 8; ++j) b = (uint8_t)((b << 1) | (bb.bits[i + (size_t)j] ? 1 : 0));
        data.push_back(b);
    }
    // 填充码字
    for (uint8_t pad = 0xEC; data.size() * 8 < (size_t)dataCapacityBits; pad ^= 0xEC ^ 0x11) {
        data.push_back(pad);
    }
    return data;
}

}  // namespace

bool EncodeText(const std::string& text, Ecc ecc, std::vector<std::vector<bool>>& modules,
                int& versionOut, int forceMask, int* maskOut) {
    int ecl = (int)ecc;
    if (ecl < 0 || ecl > 3) return false;
    if (forceMask < -1 || forceMask > 7) return false;

    // 自动选择最小可容纳版本
    int version = 0;
    for (int ver = 1; ver <= 40; ++ver) {
        int ccBits = (ver <= 9) ? 8 : 16;
        if ((int)text.size() >= (1 << ccBits)) continue;
        int usedBits = 4 + ccBits + (int)text.size() * 8;
        if (usedBits <= GetNumDataCodewords(ver, ecl) * 8) {
            version = ver;
            break;
        }
    }
    if (version == 0) return false;

    vector<uint8_t> dataCodewords = BuildDataCodewords(text, version, ecl);
    vector<uint8_t> allCodewords = AddEccAndInterleave(dataCodewords, version, ecl);

    QrMatrix qr(version, ecl);
    qr.drawFunctionPatterns();
    qr.drawCodewords(allCodewords);

    int mask;
    if (forceMask >= 0) {
        mask = forceMask;
        qr.applyMask(mask);
        qr.drawFormatBits(mask);
    } else {
        mask = 0;
        long minPenalty = -1;
        for (int m = 0; m < 8; ++m) {
            qr.applyMask(m);
            qr.drawFormatBits(m);
            long penalty = qr.getPenaltyScore();
            if (minPenalty < 0 || penalty < minPenalty) {
                minPenalty = penalty;
                mask = m;
            }
            qr.applyMask(m);  // 撤销
        }
        qr.applyMask(mask);
        qr.drawFormatBits(mask);
    }

    modules = std::move(qr.modules);
    versionOut = version;
    if (maskOut) *maskOut = mask;
    return true;
}

}  // namespace qrc
