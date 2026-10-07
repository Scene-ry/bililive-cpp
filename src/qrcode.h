// qrcode.h - 内置 QR 编码器（Byte 模式，零第三方依赖）
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace qrc {

enum class Ecc : int { L = 0, M = 1, Q = 2, H = 3 };

// 将文本编码为 QR 二值矩阵。
//  modules: [行][列]，true = 深色模块；尺寸 = 4*version + 17
//  versionOut: 实际选用的版本（1..40）
//  forceMask: -1 = 自动评估最佳掩码；0..7 = 指定掩码（测试用）
//  maskOut: 可选出参，返回实际使用的掩码
// 返回 false：文本过长或参数非法
bool EncodeText(const std::string& text, Ecc ecc, std::vector<std::vector<bool>>& modules,
                int& versionOut, int forceMask = -1, int* maskOut = nullptr);

}  // namespace qrc
