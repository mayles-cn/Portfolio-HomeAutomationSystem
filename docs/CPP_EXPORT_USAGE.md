# HomeAutomationSystem 交接文档（当前工程状态）

更新时间：`2026-04-15`

## 1. 文档目的
本交接文档以当前仓库实际内容为准，说明：

- 已交付并可直接使用的内容
- 当前缺失或未接通的模块
- 基于 `resources/models/cpp_model.json` 的后续接入方式

说明：当前仓库中不存在 `scripts/export_model_for_cpp.py` 等 Python 导出脚本，因此不再使用“仓库内重新导出模型”的流程说明。

## 2. 当前可用交付物

- 模型文件：`resources/models/cpp_model.json`（有效，已导出）
- 文档：`docs/CPP_EXPORT_USAGE.md`（本文件）
- UI 组件：`modules/ui/widgets/model_opengl_widget.cpp/.h`（OpenGL 模型显示与视角控制）
- UI 子模块构建：`modules/ui/CMakeLists.txt`（定义 `HomeAutomationUI` 静态库）

## 3. 当前工程现状（关键事实）

- `CMakeLists.txt`（根目录）为空，工程尚未具备完整顶层构建入口。
- `apps/CMakeLists.txt` 为空，应用入口目标未定义。
- `apps/main.cpp` 为空，程序主函数未落地。
- `modules/ui/mainwindow.cpp` 与 `modules/ui/mainwindow.h` 为空，主窗口壳未实现。
- 运行时配置已提供：`config/setting.json`（主配置）与 `resources/config/setting.json`（同步副本）。
- `modules/ui/widgets/model_opengl_widget.cpp` 当前加载路径写死为 `resources/models/something.obj`，但仓库中不存在该 OBJ 文件。

结论：当前仓库属于“模型资产已到位，应用框架仍在搭建中”的阶段，不是可直接运行的完整版本。

## 4. `cpp_model.json` 模型契约（按现有文件）

### 4.1 基本信息

- `schema_version`: `gesture_cpp_export_v1`
- `exported_at_utc`: `2026-04-15T11:56:15+00:00`
- 分类器：`LogisticRegression`（`standard_scaler_plus_linear_classifier`）
- `n_features`: `2560`
- `n_classes`: `8`
- 数值类型：`float32`

### 4.2 类别定义

- `class_labels`:
  - `idle`
  - `open`
  - `close`
  - `swipe_left`
  - `swipe_right`
  - `point_left`
  - `point_right`
  - `cheese`
- `display_names`（中文）:
  - `idle -> 无意义`
  - `open -> 张开`
  - `close -> 关闭`
  - `swipe_left -> 左滑(已禁用)`
  - `swipe_right -> 右滑`
  - `point_left -> 左指`
  - `point_right -> 右指`
  - `cheese -> 茄子`

### 4.3 特征布局（必须严格一致）

- `sequence_frames = 20`
- `frame_keypoint_dim = 126`
- `include_status = true`
- `total_feature_dim = 2560`
- `flatten_order = keypoints(T*D) + left_status(T) + right_status(T)`

即：输入向量长度必须为 `2560`，并遵循上述拼接顺序。

### 4.4 推理参数位置

- 标准化参数：
  - `parameters.scaler.mean`
  - `parameters.scaler.scale`
- 分类器参数：
  - `parameters.classifier.coef`
  - `parameters.classifier.intercept`

推理公式：

1. `x_norm = (x - mean) / scale`
2. `logits = coef * x_norm + intercept`
3. `prob = softmax(logits)`
4. `pred = argmax(prob)`

### 4.5 门控参数（当前默认）

- `confidence_threshold = 0.6`
- `margin_threshold = 0.2`
- `consecutive_frames = 1`
- `cooldown_ms = 450`
- `one_shot_per_appearance = false`
- `hand_disappear_reset_frames = 4`
- `require_neutral_reset = false`
- `neutral_reset_frames = 2`
- `enable_swipe_direction_guard = true`
- `swipe_direction_margin = 0.15`
- `swipe_commit_on_hand_disappear = false`
- `swipe_left_label = ""`（左滑事件逻辑禁用，仅保留右滑）
- `neutral_labels = ["idle"]`
- `hide_neutral_predictions = true`

类别专属阈值：

- `close`: `confidence >= 0.8`, `margin >= 0.32`
- `cheese`: `confidence >= 0.75`, `margin >= 0.28`
- `swipe_right`: `confidence >= 0.70`, `margin >= 0.18`

## 5. 当前未完成项与接入优先级

建议按以下顺序补齐：

1. 完成顶层构建和应用入口
2. 新增 C++ 模型运行时模块（JSON 读取 + 预处理 + softmax 分类）
3. 新增门控状态机模块（对齐 `gating` 参数语义）
4. 将推理结果接入 UI（主窗口/交互逻辑）
5. 修复或替换 OBJ 路径（`resources/models/something.obj`）

## 6. 对接时的最低检查清单

每次集成后至少验证以下项目：

1. `class_labels.size() == n_classes`（应为 `8`）
2. `mean/scale` 长度等于 `n_features`（应为 `2560`）
3. `coef` 形状为 `n_classes x n_features`（`8 x 2560`）
4. 输入特征顺序严格匹配 `flatten_order`
5. 门控阈值读取正确，且事件抑制逻辑生效（连发、冷却、右滑方向保护；左滑禁用）

## 7. 训练质量参考（来自模型文件）

- `accuracy = 0.9518072289156626`
- `macro_f1 = 0.9581485775173206`
- `weighted_f1 = 0.9520476711398351`

以上指标仅作为导出模型的历史参考，不能替代当前 C++ 端联调结果。
