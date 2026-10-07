#!/usr/bin/env node
/**
 * verify_qr.js - QR 编码器交叉验证
 *
 * 对照标准实现：将 bililive-cpp 自研 QR 编码器（--qr-dump 调试模式）的输出
 * 与 npm 成熟库 `qrcode` 生成的矩阵逐模块比对。
 *
 * 验证策略（三层）：
 *  1. 版本选择：库以 Byte 模式（与自研实现相同的编码模式）自动选版本，必须一致。
 *  2. 矩阵一致性：库使用与自研相同的 version + mask 强制生成，矩阵必须逐位一致
 *     （证明数据编码、RS 纠错、交织、功能图形、格式位、掩码应用全部正确）。
 *  3. 掩码选择：auto 模式下与库的评分择优结果对比，仅作 INFO 记录
 *     （两库对规范 N3/N4 penalty 的实现细则不同，属合法差异，不影响解码）。
 *
 * 用法: node tools/verify_qr.js
 * 退出码: 0 = 全部一致, 1 = 存在不一致
 */
'use strict';

const { execFileSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const QRCode = require('qrcode');

// 可选依赖：jsQR 解码器（存在时追加端到端解码验证）
let jsQR = null;
try {
    jsQR = require('jsqr');
} catch {
    jsQR = null;
}

const root = path.resolve(__dirname, '..');
const exe = path.join(root, 'build', 'BiliLiveCpp.exe');
// 某些环境（如沙箱）会把 TEMP 指向不存在的目录，先确保其存在
const tmpBase = os.tmpdir();
fs.mkdirSync(tmpBase, { recursive: true });
const tmpDir = fs.mkdtempSync(path.join(tmpBase, 'biliqr-'));

let passCount = 0;
let failCount = 0;
let infoCount = 0;

function runDump(text, ecc, mask) {
    const out = path.join(tmpDir, `dump_${Date.now()}_${Math.random().toString(36).slice(2)}.txt`);
    execFileSync(exe, ['--qr-dump', out, ecc, mask, text], { stdio: 'ignore' });
    const content = fs.readFileSync(out, 'utf8');
    fs.unlinkSync(out);
    const lines = content.split(/\r?\n/).filter((l) => l.length > 0);
    const m = /^version=(\d+) mask=(-?\d+) size=(\d+)$/.exec(lines[0]);
    if (!m) throw new Error(`输出头格式异常: ${lines[0]}`);
    const version = Number(m[1]);
    const maskOut = Number(m[2]);
    const size = Number(m[3]);
    const matrix = lines.slice(1).map((row) => row.split('').map((c) => c === '1'));
    if (matrix.length !== size) throw new Error(`矩阵行数 ${matrix.length} != size ${size}`);
    return { version, mask: maskOut, size, matrix };
}

// 强制 Byte 模式生成参考矩阵（与自研实现相同的编码模式）
function expected(text, ecc, forceMask, forceVersion) {
    const opts = { errorCorrectionLevel: ecc };
    if (forceMask !== undefined) opts.maskPattern = forceMask;
    if (forceVersion !== undefined) opts.version = forceVersion;
    const qr = QRCode.create([{ data: text, mode: 'byte' }], opts);
    const size = qr.modules.size;
    const data = qr.modules.data;
    const matrix = [];
    for (let y = 0; y < size; y++) {
        const row = [];
        for (let x = 0; x < size; x++) row.push(data[y * size + x] === 1);
        matrix.push(row);
    }
    return { version: qr.version, mask: qr.maskPattern, size, matrix };
}

// 渲染矩阵为 RGBA 位图（供解码器使用，模拟扫码器输入）
function renderRGBA(matrix, scale = 8, quiet = 4) {
    const n = matrix.length;
    const dim = (n + quiet * 2) * scale;
    const data = new Uint8ClampedArray(dim * dim * 4);
    for (let y = 0; y < dim; y++) {
        for (let x = 0; x < dim; x++) {
            const mx = Math.floor(x / scale) - quiet;
            const my = Math.floor(y / scale) - quiet;
            const dark = mx >= 0 && mx < n && my >= 0 && my < n && matrix[my][mx];
            const v = dark ? 0 : 255;
            const idx = (y * dim + x) * 4;
            data[idx] = v;
            data[idx + 1] = v;
            data[idx + 2] = v;
            data[idx + 3] = 255;
        }
    }
    return { data, width: dim, height: dim };
}

function compare(name, text, ecc, maskStr) {
    const problems = [];
    const notes = [];
    try {
        const act = runDump(text, ecc, maskStr);

        // 1) 版本选择一致性（库以 Byte 模式自动选择）
        const ref = expected(text, ecc, undefined, undefined);
        if (act.version !== ref.version) {
            problems.push(`version: 自研=${act.version} 库(byte)=${ref.version}`);
        } else {
            // 2) 矩阵逐位比对（库强制使用与自研相同的 version + mask）
            const exp = expected(text, ecc, act.mask, act.version);
            if (exp.size !== act.size) {
                problems.push(`size: 自研=${act.size} 库=${exp.size}`);
            } else {
                let diff = 0;
                let first = null;
                for (let y = 0; y < exp.size; y++) {
                    for (let x = 0; x < exp.size; x++) {
                        if (act.matrix[y][x] !== exp.matrix[y][x]) {
                            diff++;
                            if (!first) first = `(${y},${x})`;
                        }
                    }
                }
                if (diff > 0) problems.push(`矩阵 ${diff} 处不一致，首个位置 ${first}`);
            }
            // 2b) 端到端解码（jsQR 可用时，模拟扫码器实际识别）
            if (jsQR) {
                const img = renderRGBA(act.matrix);
                const res = jsQR(img.data, img.width, img.height);
                if (!res || res.data !== text) {
                    problems.push(`解码验证: ${res ? `内容不一致（${JSON.stringify(res.data.slice(0, 40))}...）` : '解码失败'}`);
                }
            }
            // 3) auto 掩码选择差异（N3/N4 penalty 实现细则不同，合法差异，仅记录）
            if (maskStr === 'auto' && act.mask !== ref.mask) {
                notes.push(`mask 选择: 自研=${act.mask} 库=${ref.mask}（penalty 实现差异，两矩阵均合法可解码）`);
            }
        }
    } catch (e) {
        problems.push(`执行异常: ${e.message}`);
    }

    if (problems.length === 0) {
        passCount++;
        console.log(`PASS: ${name}${notes.length ? `  [INFO] ${notes.join('; ')}` : ''}`);
        infoCount += notes.length;
    } else {
        failCount++;
        console.log(`FAIL: ${name}`);
        for (const p of problems) console.log(`      - ${p}`);
    }
}

// ---------------- 测试用例 ----------------
if (!fs.existsSync(exe)) {
    console.error(`未找到 ${exe}，请先运行 build.bat 构建。`);
    process.exit(1);
}

// 1. 版本 1 全部 8 种固定掩码
for (let m = 0; m <= 7; m++) compare(`v1-fixed-mask${m}`, 'HELLO WORLD', 'M', String(m));

// 2. auto 掩码（版本选择 + 数据编码 + 掩码评估）
compare('auto-hello-M', 'HELLO WORLD', 'M', 'auto');
compare('auto-scan-url-M', 'https://passport.bilibili.com/h5-app/passport/login/scan?qrcode_key=00000000000000000000000000000000&navhide=1', 'M', 'auto');
compare('auto-scan-url-L', 'https://passport.bilibili.com/h5-app/passport/login/scan?qrcode_key=f3c2a1b0d4e5f6a7b8c9d0e1f2a3b4c5&navhide=1', 'L', 'auto');
compare('auto-cn-Q', '中文测试数据：扫码登录', 'Q', 'auto');
compare('cn-fixed-mask3', '中文测试数据：扫码登录', 'Q', '3');
compare('auto-emoji-M', '😀🎉 BiliLive', 'M', 'auto');
compare('auto-digits-M', '12345678901234567890', 'M', 'auto');
compare('auto-single-H', 'A', 'H', 'auto');
compare('auto-mix-M', 'Bilibili Live console QR test 1234567890 中文混合内容 for cross validation', 'M', 'auto');
compare('auto-long-H', 'x'.repeat(180), 'H', 'auto');
compare('auto-long-L', 'The quick brown fox jumps over the lazy dog. '.repeat(10), 'L', 'auto');
compare('auto-url-Q', 'https://b23.tv/abcdefg', 'Q', 'auto');
// 覆盖高版本（含版本信息位 drawVersion，v7+）
compare('auto-hi-M', 'q'.repeat(1500), 'M', 'auto');
compare('auto-hi-L', 'z'.repeat(2400), 'L', 'auto');

try { fs.rmSync(tmpDir, { recursive: true, force: true }); } catch { /* ignore */ }

console.log('');
if (!jsQR) {
    console.log('提示: 未安装 jsqr，已跳过端到端解码验证（可运行 npm i jsqr 后重跑本脚本）。');
}
console.log(failCount === 0
    ? `RESULT: ALL ${passCount} CASES PASS` + (infoCount ? ` (${infoCount} INFO)` : '')
    : `RESULT: ${passCount} PASS, ${failCount} FAIL`);
process.exit(failCount === 0 ? 0 : 1);
