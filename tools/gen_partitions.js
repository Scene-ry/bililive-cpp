/**
 * gen_partitions.js
 * 将 app/tools/bililive/room-partitions.json 裁剪为 C++ 头文件 src/partitions_data.h
 * 仅保留 {id, "分类名 - 分区名"}，与网页版下拉框展示完全一致。
 *
 * 用法：node tools/gen_partitions.js
 */
'use strict';

const fs = require('fs');
const path = require('path');

const SRC = path.join(__dirname, '..', '..', 'app', 'tools', 'bililive', 'room-partitions.json');
const OUT = path.join(__dirname, '..', 'src', 'partitions_data.h');

const raw = fs.readFileSync(SRC, 'utf8');
const json = JSON.parse(raw);

if (!json || !Array.isArray(json.data)) {
  console.error('unexpected partitions json structure');
  process.exit(1);
}

const items = [];
for (const cat of json.data) {
  if (!cat || !Array.isArray(cat.list)) continue;
  for (const item of cat.list) {
    const fullName = `${cat.name} - ${item.name}`;
    items.push({ id: String(item.id), name: fullName });
  }
}

// 确认默认分区存在（网页版默认 235）
if (!items.some((it) => it.id === '235')) {
  console.warn('warning: default partition id 235 not found');
}

const esc = (s) => s.replace(/\\/g, '\\\\').replace(/"/g, '\\"');

let out = '';
out += '// ============================================================\n';
out += '// 本文件由 tools/gen_partitions.js 自动生成，请勿手改。\n';
out += `// 数据来源: app/tools/bililive/room-partitions.json (${items.length} 个分区)\n`;
out += '// ============================================================\n';
out += '#pragma once\n\n';
out += 'struct PartitionEntry {\n';
out += '    const char* id;\n';
out += '    const char* name;   // UTF-8: "分类 - 分区"\n';
out += '};\n\n';
out += 'static const PartitionEntry kPartitions[] = {\n';
for (const it of items) {
  out += `    { "${esc(it.id)}", "${esc(it.name)}" },\n`;
}
out += '};\n\n';
out += `static const int kPartitionCount = ${items.length};\n`;
out += 'static const char* kDefaultPartitionId = "235";\n';

fs.writeFileSync(OUT, out, 'utf8');
console.log(`generated ${OUT}: ${items.length} partitions`);
