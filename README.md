# HomeAutomationSystem

基于手势识别的智能家居控制系统。通过摄像头实时捕捉手部动作，驱动 3D 家居场景中的设备操作与相机切换。

## 技术栈

- **语言**：C++17
- **框架**：Qt 6 (Widgets + OpenGL)
- **手部追踪**：MediaPipe Hand Landmarker
- **手势分类**：自训练轻量级 C++ 推理模型
- **构建系统**：CMake 3.16+

## 功能概览

| 功能 | 说明 |
|------|------|
| 3D 家居场景 | 基于 OpenGL 渲染 OBJ 模型，支持方向光 / 面积光 / 网格地面 |
| 手势识别 | 8 种手势：空闲、张开、握拳、左滑、右滑、左指、右指、茄子 |
| 设备动画 | 帧序列动画播放（空调、洗衣机、油烟机、风扇、音响、窗帘、咖啡机、钥匙） |
| 相机机位 | 9 个预设机位，手势 / 键盘均可循环切换 |
| 场景灯光 | 茄子手势切换面积光开关 |
| 调试面板 | F9 打开，实时查看骨架、预测结果、手势屏蔽等 |

## 手势绑定

| 手势 | 操作 |
|------|------|
| 右滑 (`swipe_right`) | 切换到下一个场景 |
| 左指 (`point_left`) | 切换到上一个场景 |
| 右指 (`point_right`) | 切换到下一个场景 |
| 茄子 (`cheese`) | 切换场景灯光 |
| 张开 (`open`) | 启动当前设备动画播放 |
| 握拳 (`close`) | 停止当前设备动画播放 |

## 机位映射

| 机位 | 场景 | 设备动画序列 |
|------|------|-------------|
| 01 | 默认首页 | 无 |
| 02 | 空调 | AirConditioner |
| 03 | 洗衣机 | WashMachine |
| 04 | 油烟机 | KitchenHood |
| 05 | 风扇 | ElectricFan |
| 06 | 音响 | Sounder |
| 07 | 窗帘 | Curtain |
| 08 | 咖啡机 | CoffeeMarker |
| 09 | 钥匙 | HomeKey |

## 构建

```bash
cmake --preset default
cmake --build build --config Release
```

## 运行

```bash
./build/apps/Release/HomeAutomationApp.exe
```

启动后自动开始手势识别（需连接摄像头）。按 `F9` 打开调试面板，按 `Esc` 退出。

## 目录结构

```
├── apps/                   # 应用入口
├── modules/
│   ├── gesture/            # 手势识别管道（模型推理、事件门控）
│   └── ui/                 # UI 模块（主窗口、OpenGL、动画覆盖层）
│       └── widgets/        # 子控件（相机系统、帧序列、模型渲染）
├── resources/
│   ├── config/             # 配置文件
│   ├── images/             # 设备帧序列图片
│   ├── models/             # OBJ 模型 + 手势模型 + MediaPipe 任务
│   └── videos/             # 设备视频素材
├── tools/                  # MediaPipe 桥接程序 + 验证脚本
└── scripts/                # 构建 / 转换脚本
```

## 依赖

- Qt 6.x（Widgets, OpenGL, OpenGLWidgets）
- MediaPipe Hand Landmarker 桥接程序（`tools/hand_landmarker_stream.exe`）
- OpenCV 4.11（随桥接程序分发）
