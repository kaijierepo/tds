# TDS 项目记忆

## 用户偏好

- **编译**：改完代码即可，不要自动编译，除非用户明确要求。
- **Git 提交**：
  - **`.workflow` 下的流水线 yml 文件**：改好后**自动提交并推送**（用户 2026-08-19 明确要求，方便流水线尽快拉到最新配置），无需等用户发话。
  - **一般代码**：不要自动提交，除非用户明确要求。
- **SVN 提交**：不要自动提交，除非用户明确要求。PowerShell 下传中文参数给 svn 会丢失编码，不要用 `svn commit -m "中文" --encoding UTF-8`。正确做法：
  1. 将提交消息写入 UTF-8 文件：`[System.IO.File]::WriteAllText("msg.txt", "消息", [System.Text.UTF8Encoding]::new($false))`
  2. 从文件提交：`svn commit -F msg.txt --encoding UTF-8`
  3. 提交后检查是否乱码，若乱码需用 `svn propset --revprop` 修正（需服务端支持）

## 项目约定

### 前端 UI 颜色风格（以 `app/stream` 为准）

所有 TDS Web 界面统一使用以下深蓝系配色方案：

**CSS 变量定义**：
```css
--bg: #0a1628;              /* 页面背景 */
--bg-card: #0d1b33;          /* 卡片/表格/弹窗背景 */
--bg-card-header: #132038;   /* 卡片头部/表头/弹窗头部背景 */
--bg-input: #0c1a32;         /* 输入框背景 */
--text: #c8d6e5;             /* 正文颜色 */
--text-dim: #6b7f99;         /* 辅助文字/表头文字 */
--border: #1e3150;           /* 普通边框（深） */
--primary: #2980e0;          /* 主色 */
--primary-hover: #348de8;    /* 主色 hover */
--danger: #f5365c;           /* 危险/删除色 */
--danger-hover: #f76c82;     /* 危险色 hover */
--success: #22c55e;          /* 成功色 */
--warning: #f5a623;          /* 警告色 */
```

**关键渐变和特效色**：
- 标题强调色：`#e8edf5`
- 占位符色：`#4a5f7a`
- 输入框 border：`rgba(64, 134, 241, 0.2)`
- 输入框 focus border：`#4a9eff` + `box-shadow: 0 0 0 3px rgba(74, 158, 255, 0.12)`
- 分割线/header border：`rgba(64, 134, 241, 0.1)` ~ `0.12`
- 弹窗 border：`rgba(64, 134, 241, 0.15)`
- 弹窗 overlay：`rgba(5, 12, 25, 0.75)` + `backdrop-filter: blur(6px)`
- 表格行 hover：`rgba(45, 140, 240, 0.06)`

**主按钮 (btn-primary)**：
```css
background: linear-gradient(135deg, #1a5dc7 0%, #2980e0 100%);
border: none;
color: #fff;
/* hover: */
background: linear-gradient(135deg, #1e6cdc 0%, #348de8 100%);
box-shadow: 0 4px 12px rgba(42, 128, 224, 0.25);
```

**次要按钮 (btn-secondary)**：
```css
background: transparent;
color: #8a9bb5;
border: 1px solid rgba(64, 134, 241, 0.25);
/* hover: */
color: #c8d6e5;
border-color: rgba(64, 134, 241, 0.4);
background: rgba(64, 134, 241, 0.06);
```

**圆角**：`6px`（卡片/输入框/按钮），`10px`~`12px`（弹窗/大卡片）

**字体**：`-apple-system, BlinkMacSystemFont, "Segoe UI", "Microsoft YaHei", sans-serif`

**滚动条**：
```css
scrollbar-width: 6px;
scrollbar-thumb: rgba(64, 134, 241, 0.2);
scrollbar-thumb-hover: rgba(64, 134, 241, 0.35);
```

**状态色**：
- 在线/成功：`#22c55e` / `#2dce89`
- 告警：`#f5a623`
- 错误/离线：`#f5365c`
- 加载/进行中：`#3b82f6`
- 待机：`#64748b`

### 单例模式

- 使用**全局变量 + `extern` 声明**，不使用 `instance()` 静态函数。
- 模式：`.h` 文件中 `extern ClassName g_varName;`，`.cpp` 文件中 `ClassName g_varName;`

### 头文件 include guard（重要，2026-08-19 起）

- 项目启用 GCC 预编译头（`src/pch.h.gch`，由 build.sh 自动生成）。
- **所有头文件一律只用 include guard（`#ifndef X` / `#define X` / `#endif`），不用 `#pragma once`**（2026-08-19 已全量删除所有 `#pragma once`，含 json.hpp/miniz.h 等）。
- guard 命名统一：`TDS_路径_文件名_H`（如 common/hmac.h → `TDS_COMMON_HMAC_H`；文件名含连字符的替换为下划线；`.hpp` 去扩展名）。
- 原因：GCC 的 PCH 传递 include guard 的宏定义，但**不恢复 `#pragma once` 的文件状态**，仅靠 `#pragma once` 的头在 PCH 下会 redefinition。
- **例外（故意无 guard，禁止加）**：`common/crypto/library/mbedtls_config_check_*.h`、`common/crypto/psa/core/tf_psa_crypto_config_check_*.h`、`everest/.../wasmsupport.h`、`script/libunicode-table.h`、`script/quickjs-opcode.h`、`script/quickjs-atom.h`、`script/libregexp-opcode.h`、`script/unicode_gen_def.h`（quickjs 表文件是**重复 include 生成代码**，加 guard 会破坏）。
- **机制差异**：MSVC 的 PCH（/Yu）是完整编译器状态快照，会保存 `#pragma once` 的"文件已包含"状态，所以 VS 工程（msvc/tds.vcxproj 用 `<PrecompiledHeader>Use</PrecompiledHeader>`）不补 guard 也能正常编译。GCC 的 .gch 只保存宏+AST，不保存 `#pragma once` 状态，所以必须靠 include guard。

### JSON 库迁移

- **`json.hpp`（nlohmann）不再使用**，统一使用 `yyjson.h`。
- 每次修改涉及 JSON 的代码时，**逐步重构**：将 `json.hpp` 替换为 `yyjson.h`。
- yyjson 路径：`src/common/yyjson.h`
- yyjson 读 API：`yyjson_read()` → `yyjson_doc_get_root()` → `yyjson_arr_foreach()` / `yyjson_obj_get()` → `yyjson_get_str()` / `yyjson_get_int()` → `yyjson_doc_free()`
- yyjson 写 API：`yyjson_mut_doc_new()` → `yyjson_mut_obj_add_*` / `yyjson_mut_arr_append_*` → `yyjson_mut_write()` → `yyjson_mut_doc_free()`
- **doc vs val API 严格区分**（编译期会报错 C2664）：
  - 操作整个文档：`yyjson_read(doc)` / `yyjson_write(doc)` / `yyjson_doc_free(doc)`
  - 操作单个 val：`yyjson_val_write(val)` / `yyjson_val_mut_copy(doc, val)` / `yyjson_val_mut_imut_copy(imut_doc, val)`
  - mut 文档中按 key 取值用 `yyjson_mut_obj_get(obj, key)`，普通文档用 `yyjson_obj_get(obj, key)`

### 流水线产物路径与 Release 命名

**文件**：`.workflow/linux-x86_64.yml`

- 构建产物默认生成在 `out/tds/tds-v*.tar.gz`。
- `.gitignore` 第 14 行忽略了 `out/`，CI 收集 artifacts 与文件断言通常遵循 `.gitignore`，导致 `out/` 下的文件匹配不到。
- 因此构建步骤末尾必须 `cp out/tds/tds-v*.tar.gz ./`，将包放到工作目录根（`tar.gz` 不在 `.gitignore` 忽略列表内）。
- `artifacts.path` 与 `assertFiles` 必须同时指向根目录的 `./tds-v*.tar.gz`，不能只改其中一个。
- Gitee Release 的 `releaseName` 和 `tagName` 统一使用带版本号的命名，例如 `tds-v1.0-linux-x86_64`（不要再使用 `tds-latest-linux-x86_64`）。
- 相关文件修改后需**自动提交并推送**，确保流水线尽快拉到最新配置。
