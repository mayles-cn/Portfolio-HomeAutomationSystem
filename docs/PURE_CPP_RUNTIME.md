# Pure C++ Gesture Runtime

更新时间：`2026-04-17`

## 1. 当前迁移结果

项目已完成“Python 推理运行时 -> C++ 推理运行时”迁移，当前手势识别核心链路为：

1. 加载 `resources/models/cpp_model.json`
2. 在 C++ 内执行 `StandardScaler + LogisticRegression + Softmax`
3. 执行门控状态机（阈值、连续帧、冷却、neutral reset、右滑方向保护；左滑禁用）

不再依赖 Python 进程来做推理。

## 2. 主要代码位置

- 模型与推理：
  - `modules/gesture/gesture_model.h`
  - `modules/gesture/gesture_model.cpp`
- 门控状态机：
  - `modules/gesture/gesture_gate.h`
  - `modules/gesture/gesture_gate.cpp`
- 时序拼接与整体管线：
  - `modules/gesture/gesture_pipeline.h`
  - `modules/gesture/gesture_pipeline.cpp`
- 类型定义：
  - `modules/gesture/gesture_types.h`

## 3. 构建与部署变化

- 新增模块：`modules/gesture/CMakeLists.txt`
- 顶层 CMake 已加入：`add_subdirectory(modules/gesture)`
- UI 目标已链接纯 C++ 识别模块：`HomeAutomationGesture`
- 应用构建后自动复制模型到：
  - `build/apps/<Config>/models/cpp_model.json`

## 4. UI 验证入口

`MainWidget` 已接入纯 C++ 推理冒烟测试：

- 启动时自动尝试加载模型
- 点击按钮 `运行 C++ 推理冒烟测试` 即可执行一次纯 C++ 推理
- UI 会显示预测标签、置信度和 margin

## 5. 接入 MediaPipe C++ 的下一步

当前管线已提供 `GestureFrameObservation`，你只需要把 MediaPipe C++ 输出的每帧手部关键点映射为：

- `keypoints[126]`
- `leftStatus`
- `rightStatus`
- `hasHand`
- `timestampMs`

然后循环调用：

`GesturePipeline::pushObservation(...)`

即可得到 `GesturePrediction` 与门控后的 `GestureEvent`。

## 6. 环境自检脚本

已提供：

- `scripts/check_msvc_env.ps1`

执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/check_msvc_env.ps1
```

## 7. 实时 MediaPipe 适配（已接入 UI）

当前 `MainWidget` 已支持通过桥接进程接入 MediaPipe C++ 实时关键点流：

- 新增桥接客户端：
  - `modules/gesture/mediapipe_stream_client.h`
  - `modules/gesture/mediapipe_stream_client.cpp`
- UI 新增按钮：
  - `启动实时手势识别`

桥接程序作用（`hand_landmarker_stream.exe`）：

1. 负责摄像头采集 + MediaPipe HandLandmarker 推理。
2. 负责把每帧结果转换成统一 JSON（`keypoints[126]`, `left_status`, `right_status`, `has_hand`, `timestamp_ms`）并输出到 stdout。
3. Qt 主程序只负责读取这条 JSON 流，再走本项目的 C++ 分类器与门控；主程序本身不直接链接 MediaPipe Bazel 产物。

桥接程序目标（在 `D:\SomeCppProjects\mediapipe` 构建）：

- `//mediapipe/examples/desktop/hand_tracking:hand_landmarker_stream`

快捷构建脚本：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_mediapipe_bridge.ps1
```

脚本会在构建成功后自动同步以下文件到当前项目：

- `tools/hand_landmarker_stream.exe`
- `tools/opencv_world4110.dll`

`HomeAutomationApp` 构建时会再复制到：

- `build/apps/<Config>/tools/hand_landmarker_stream.exe`
- `build/apps/<Config>/tools/opencv_world4110.dll`

运行时查找顺序：

1. 环境变量 `HOME_AUTOMATION_MEDIAPIPE_BRIDGE`
2. `build/apps/<Config>/tools/hand_landmarker_stream.exe`
3. `tools/hand_landmarker_stream.exe`
4. 固定路径 `D:/SomeCppProjects/mediapipe/bazel-bin/mediapipe/examples/desktop/hand_tracking/hand_landmarker_stream.exe`（仅 fallback）

`hand_landmarker.task` 查找顺序：

1. 环境变量 `HOME_AUTOMATION_HAND_TASK`
2. `build/apps/<Config>/models/hand_landmarker.task`（由 CMake 自动复制）
3. `resources/models/hand_landmarker.task`

摄像头索引：

1. 可通过环境变量 `HOME_AUTOMATION_CAMERA_INDEX` 指定（例如 `1`）。
2. 若未指定，程序会自动探测索引 `0~5` 并使用首个可用摄像头。

## 8. 调试面板（F9）

主界面支持独立调试面板，按 `F9` 切换显示，默认吸附在主窗口右侧（无缝贴合）。
主界面按 `Esc` 可直接退出程序（主界面已移除独立退出按钮）。

- 面板中包含：
  - `启动/停止实时识别` 按钮
  - 模型和跟踪状态文本
  - 骨架画面区域
  - `绘制骨架` 开关
  - 摄像头索引输入与应用按钮
  - 手势屏蔽列表

UI 样式统一为直角风格（`border-radius: 0`）。

## 9. 项目级配置外置（新增）

已新增运行时配置文件：

- `config/setting.json`（主配置）
- `resources/config/setting.json`（同步副本）

`HomeAutomationApp` 构建后会自动复制到：

- `build/apps/<Config>/config/setting.json`

当前 `MainWidget` 以下参数已由配置驱动：

1. 窗口/面板尺寸、布局间距、相机探测范围与等待时长
2. 调试面板热键（默认 `F9`）
3. 主界面序列帧播放器（宽高比例、手动切帧）
   - 使用 `0~100` 滑动条手动定位帧，不自动播放
   - 支持左右切换按钮与键盘 `←/→` 在动画子文件夹间切换
   - 尺寸由 `sequence_player_width_ratio` / `sequence_player_height_ratio` 控制
   - 动画顺序由 `runtime.paths.image_sequence_folder_order` 控制
4. UI 文案（按钮、状态文本、标题）
5. 样式（颜色、字号、字体、边框、圆角、骨架颜色/线宽/点大小）
6. 运行时路径候选（模型、桥接程序、task 模型、日志默认路径、序列帧根目录候选）

说明：

- 可通过 `HOME_AUTOMATION_SETTING_JSON` 指定配置文件路径。
- 可通过 `HOME_AUTOMATION_IMAGE_SEQUENCE_ROOT` 覆盖序列帧根目录。
- 若配置缺失或解析失败，会回退到代码内默认值，不影响程序启动。
