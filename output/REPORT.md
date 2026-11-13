# KHR/EXT_meshopt_compression 解码支持 — 验收报告

## 1. 需求与实现映射

| 需求 | 实现 |
|---|---|
| 识别扩展声明并按压缩数据还原属性 | `cgltf_decode_meshopt_views()`（cgltf.h 实现区）在 `cgltf_load_buffers` 末尾统一解码所有带 `has_meshopt_compression` 的 bufferView，覆盖 `ATTRIBUTES / TRIANGLES / INDICES` 三种模式与 `NONE / OCTAHEDRAL / QUATERNION / EXPONENTIAL / COLOR` 五种过滤器，EXT 与 KHR 两个扩展名共用同一路径 |
| 不支持/数据损坏给可区分错误码，不静默送错顶点 | 新增 `cgltf_result_unsupported_extension`(=10) 与 `cgltf_result_decode_error`(=11)。位流魔数/版本不符、越界、尾部字节不精确消耗 → 11；未知 meshopt 家族必需扩展、未知 mode/filter → 10。解码缓冲零初始化且失败即释放，失败时 `load_buffers` 直接返回错误码，不产出任何顶点 |
| 未使用扩展的文件逐字节一致 | 解码结果写入既有的 `cgltf_buffer_view::data`，全部取值仍走唯一入口 `cgltf_buffer_view_data()`；唯一改动的既有函数 `cgltf_calc_index_bound` 也改走该入口。基线（HEAD）与改动版对真实样例模型 dump 逐字节相同（见第 3 节） |
| 不复制现有访问器解析逻辑，抽公共层、多处小改 | 新增一个内聚的 meshopt 公共层（常量/位流编解码/过滤器/调度），既有访问器代码零复制；对既有文件仅 1 行实质改动（共享取值入口）+ 2 个错误码枚举 + load_buffers 末尾 1 个 hook + 文档 |

## 2. 架构要点

- **单一取值入口**：解码后的字节放进 `buffer_view->data`，`cgltf_buffer_view_data()` 优先返回它；
  所有 `cgltf_accessor_read_float / unpack_floats / unpack_indices / sparse / calc_index_bound`
  天然读到解码后数据，没有任何并行的“压缩专用”读取路径。
- **C99 标量移植**：按 meshoptimizer 权威库（vertexcodec/indexcodec/vertexfilter）逐函数移植标量路径，
  无 SIMD、无第三方依赖。位流解码返回区分性负值（-1 魔数/版本，-2 截断，-3 尾部不匹配），调度层统一映射为 11。
- **零外部链接**：过滤器需要的 `sqrtf/fabsf` 用整数位分解 + double Newton-Raphson 自包含实现
  （`cgltf_meshopt_sqrtf`），经 1.5 亿随机浮点 + 5000 万工作域数值与 glibc `sqrtf` 逐位比对 0 失配，
  因此单头库不需要新增 `-lm`，仓库现有 CMake/ASan 构建无需改动即可链接。
- **内存**：解码缓冲经 options 的 alloc/free 回调分配，`cgltf_free` 既有逻辑释放 `buffer_view->data`；
  零初始化保证任何中途失败都不留陈旧顶点。

## 3. 验证（全部使用仓库自带入口 `cgltf_parse_file → cgltf_load_buffers → cgltf_validate`）

### 3.1 还原顶点与未压缩参考一致
用 meshoptimizer 官方**编码器**生成 10 个视图（位置/交错/两种 OCTAHEDRAL/QUATERNION/
EXPONENTIAL/两种 COLOR/TRIANGLES/INDICES），参考基准由官方**解码器**从同一压缩流还原
（三角模式会按格式设计旋转顶点绕序，故基准取解码输出而非编码输入）。
经 `cgltf_buffer_view_data` 原始字节与 `cgltf_accessor_unpack_floats` 双通道 dump：

```
Result: 0 / Result: 0
BYTE-EXACT MATCH across all 10 views (all modes + all filters)
```

与默认（SIMD）参考构建相比仅 OCT/QUAT 有 7/38252 字节为 ±1 的舍入边界差异——这是参考库自身
标量 vs SIMD 两条过滤器实现的已知差异；本移植与其**标量**参考实现逐字节一致。

### 3.2 错误场景可诊断（test_errors.py，全 PASS）
| 场景 | 返回码 |
|---|---|
| 顶点流魔数损坏 | 11 decode_error |
| 三角索引流魔数损坏 | 11 decode_error |
| 各流结构尾部/块控制字节损坏（.gltf 与内嵌 .glb） | 11 decode_error |
| 磁盘 bin 截断 | 7 io_error（加载器尺寸校验；GLB 内嵌截断走 11） |
| 未知 meshopt 家族必需扩展 | 10 unsupported_extension |
| KHR 扩展名变体 | 0 success（与 EXT 同路径） |
| 干净 reference/compressed | 0 success |

### 3.3 旧文件零回归
对 Khronos 真实未压缩样例（Box、BoxTextured、TriangleWithoutIndices 的 .gltf/.glb），
用 HEAD 基线头与改动后头分别构建 harness 并 dump：**5/5 IDENTICAL**，返回码一致。

### 3.4 健壮性
- 仓库 CMake 全部 6 个目标在 `-Wall -Wextra -pedantic -Werror` + ASan 下构建、运行通过；
  另过 C99 / C++11 / C++17 严格编译。
- 压缩 GLB 变异 fuzz（ASan/UBSan）：**10000 次迭代 0 崩溃**，全部以正常错误码收敛。

## 4. 产物
- `patches/meshopt-decode.patch` — cgltf.h + README.md 的完整改动
- `harness/` — 验证/错误/fuzz/GLB 入口（C 与 Python，均非交互）
- `gen/generate.cpp` — 基于官方编码器的权威数据生成器
- `data/` — 参考/压缩 glTF+bin、manifest、两份逐字节相等的 dump
- `recording.cast` — 7 段带标题、段间停顿的 asciinema 录屏（`asciinema play recording.cast`）
- `verification_full.log` — 含 10000 次 fuzz 的完整端到端日志
