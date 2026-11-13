# KHR_mesh_quantization 支持 — 验证产物

## 目录结构

- `scripts/verify_avocado.cpp` — 真实 Khronos Avocado 资产的端到端验证程序
  （量化版 vs 浮点参考：还原误差有界 + 写出往返一致）。
- `scripts/run_verification.sh` — 非交互一键验证（构建 → 单测 → 仓库全量入口 → 真实资产），
  全程不需要键盘输入。
- `scripts/record_demo.sh` — 录屏驱动：6 个带标题分段、段间停顿、无键盘等待。
- `casts/quantization_demo.cast` — asciinema 录屏（约 25 秒，6 段）。
- `data/float/`、`data/quant/` — Avocado 浮点参考版与 KHR_mesh_quantization 版（保留原始文件名）。
- `reference/` — 最初下载的量化样例（gltfpack 输出，展示 extensionsUsed + extensionsRequired）。

## 实现要点（库代码在仓库根的 cgltf.h / cgltf_write.h）

- 量化还原**不引入第二套解析分支**：整型属性的归一化/整数转换本就由唯一的
  `cgltf_component_read_float` → `cgltf_element_read_float` → `cgltf_accessor_read_float/unpack`
  公共路径按 glTF 规范公式完成（BYTE `c/127`、UBYTE `c/255`、SHORT `c/32767`、USHORT `c/65535`）。
- 新增公共层：
  - `cgltf_data_has_extension()` — 读路径与写路径共用的扩展查询。
  - `cgltf_string_array_contains()` — 字符串数组查找。
  - `cgltf_quantized_attribute_is_valid()` — 合并核心规范 3.7.2.1/3.7.2.2 表与扩展新增格式的
    单一合法性谓词，primitive 与 morph target 共用。
  - 所有新校验均以 `has_mesh_quantization` 为门控；未声明该扩展的文件行为完全不变。
- 写出：新增一个 `CGLTF_EXTENSION_FLAG_MESH_QUANTIZATION`，在 `cgltf_write` 起始处根据解析出的
  extensionsUsed/Required 统一置位；accessor 的 componentType/normalized 由既有 accessor 写出路径
  天然保留，因此扩展与量化数据可往返。

## 非法参数报错（cgltf_validate 返回 cgltf_result_invalid_gltf）

- 声明 used 但未声明 required；
- NORMAL/TANGENT 未规范化或分量类型错误；
- 无符号 TEXCOORD 用于 morph 时未规范化；normalized JOINTS；
- morph POSITION 使用无符号类型；primitive COLOR 使用有符号类型；
- 8/16 位整型向量元素未按 4 字节对齐。

## 验收结果（见 casts/ 录屏）

- 单测 `test_quantization`：合法还原、误差有界、8 类非法参数报错、写出往返、无扩展零回归，全部通过。
- 仓库自带入口 `test/test_all.py`：**Tested files: 726, Errors: 0**（含同时使用
  KHR_mesh_quantization + EXT_meshopt_compression 的 BrainStem/DragonAttenuation）。
- 真实 Avocado（406 顶点，最近邻、与顶点序无关的点云比较，已校正 gltfpack 烘焙的 180° Y 旋转）：
  - POSITION 最大误差 2.92e-06（模型跨度 ~0.04，16 位量化）；
  - NORMAL 6.42e-03、TANGENT 6.21e-03（8 位单位向量单步界 2/127≈1.58e-02）；
  - TEXCOORD 1.66e-04；
  - 写出往返：扩展 used/required 保留、buffer 字节一致、还原值逐位相同。

## 复现方式

```bash
bash output/scripts/run_verification.sh          # 非交互全量验证
asciinema play output/casts/quantization_demo.cast  # 回放录屏
```
