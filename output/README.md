# KHR_mesh_quantization 实现产物

## 需求与实现对应

| 需求 | 实现 |
| --- | --- |
| 识别量化扩展 | `cgltf.h` 新增 `cgltf_mesh_quantization` 结构与 accessor 上的 `has_mesh_quantization`；`cgltf_parse_json_mesh_quantization` 挂在 accessor 既有的 `extensions` 解析分支内 |
| 按声明的规范化方式还原 | 量化反解作为一个参数并入既有公共函数 `cgltf_component_read_float` / `cgltf_element_read_float`，`cgltf_accessor_read_float` 与 `cgltf_accessor_unpack_floats` 沿同一路径传参，**没有第二套解析分支** |
| 写出时保留扩展 | `cgltf_write.h` 的 `cgltf_write_accessor` 内联序列化 `KHR_mesh_quantization`，并经 `CGLTF_EXTENSION_FLAG_MESH_QUANTIZATION` 登记到 `extensionsUsed` |
| 量化参数非法报错 | `cgltf_validate` 校验：quantizedBits 取值、FLOAT/整数存储搭配、宽度一致、矩阵不可量化、扩展须在 extensionsUsed 声明、srgb 仅限 COLOR 属性；非法返回 `cgltf_result_invalid_gltf`(4) |
| 未声明量化的文件零变化 | 无扩展时 `quantized_bits == invalid`，公共函数走到与改动前完全相同的分支 |

## 目录

- `recordings/` — 六段分标题 asciinema 录屏（每段独立 `.cast`，`full_demo.cast` 为带 3 秒段间停顿的合并版），录制全程无键盘输入
  - `segment1_design.cast` 设计：单一读路径合并
  - `segment2_validate.cast` 解析与非法参数报错
  - `segment3_dequant.cast` 反量化误差有界
  - `segment4_roundtrip.cast` 写出往返（gltf + glb）
  - `segment5_regression.cast` 旧文件零回归
  - `segment6_acceptance.cast` 完整验收
- `roundtrip/` — 验收测试实际写出的往返 gltf/glb
- `acceptance_logs/` — 各入口的固化运行日志
- 测试源码在仓库 `test/`：`test_quantization.cpp`（验收程序）、`gen_quantization_assets.py`（资产生成器）、`test/data/quantization/`（生成的 gltf/glb 资产）

## 复现方式（仓库自带入口）

```sh
cd test
python3 -I gen_quantization_assets.py data/quantization      # 生成量化资产
cmake -S . -B build && cmake --build build -j4               # 含新 test_quantization
ASAN_OPTIONS=detect_leaks=0 build/test_quantization \
    data/quantization ../output/roundtrip                    # 误差/往返/非法/零回归
```

## 验收结论（见 acceptance_logs/）

- 反量化最大绝对误差：i8/i16/u16 为 0，u8 为 2.98e-08（float32 求值），均在有界容差内
- 写出往返：7 个有效资产（含 GLB、srgb COLOR）扩展块、quantizedBits、colorSpace 与反量化值完全一致
- 非法参数：6 类非法资产全部被 `cgltf_validate` 拒绝（返回 4）
- 零回归：官方样本子集 34 次解析/转换 + 17 次写出全部与基线一致；fuzz 集错误码 4/6 保持不变；test_math / test_strings 通过
