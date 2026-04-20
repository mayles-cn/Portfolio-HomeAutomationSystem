from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import sys
import time
from typing import Any

from collections import deque

import cv2
import joblib
import numpy as np
import tkinter as tk
from PIL import Image, ImageDraw, ImageFont, ImageTk
from tkinter import ttk

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src.collector.landmark_processor import HAND_VECTOR_LENGTH, LandmarkPipeline, status_to_code  # noqa: E402
from src.config import load_settings  # noqa: E402

DEFAULT_LABEL_DISPLAY_NAMES_ZH = {
    "idle": "无意义",
    "open": "张开",
    "close": "关闭",
    "swipe_left": "左滑(已禁用)",
    "swipe_right": "右滑",
    "point_left": "左指",
    "point_right": "右指",
    "cheese": "茄子",
}


@dataclass
class ModelBundle:
    model: Any
    model_dir: Path
    labels: list[str]
    include_status: bool
    sequence_frames: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Realtime gesture validation UI.")
    parser.add_argument(
        "--settings",
        default=str(ROOT / "setting.json"),
        help="Path to setting.json",
    )
    parser.add_argument(
        "--model-dir",
        default=None,
        help="Model run directory containing model.joblib. If omitted, use latest under models/.",
    )
    parser.add_argument(
        "--feature-mode",
        choices=["auto", "keypoints", "keypoints_status"],
        default="auto",
        help="Feature mode used for inference. 'auto' reads from report.json if available.",
    )
    parser.add_argument(
        "--sequence-frames",
        type=int,
        default=None,
        help="Override window frames. If omitted, infer from model or report.",
    )
    parser.add_argument(
        "--confidence-threshold",
        type=float,
        default=None,
        help=(
            "Minimum confidence threshold. "
            "If omitted, read from setting.json validation.confidence_threshold (default 0.60)."
        ),
    )
    parser.add_argument(
        "--consecutive-frames",
        type=int,
        default=None,
        help=(
            "Required consecutive valid frames to lock stable output. "
            "If omitted, read from setting.json validation.consecutive_frames (default 3)."
        ),
    )
    parser.add_argument(
        "--margin-threshold",
        type=float,
        default=None,
        help=(
            "Minimum top1-top2 confidence margin. "
            "If omitted, read from setting.json validation.margin_threshold (default 0.20)."
        ),
    )
    parser.add_argument(
        "--draw-landmarks",
        action="store_true",
        help="Force draw landmarks on preview frame.",
    )
    return parser.parse_args()


def resolve_model_dir(model_dir_arg: str | None) -> Path:
    if model_dir_arg:
        model_dir = Path(model_dir_arg)
        if not model_dir.is_absolute():
            model_dir = ROOT / model_dir
        model_dir = model_dir.resolve()
    else:
        models_root = ROOT / "models"
        if not models_root.exists():
            raise FileNotFoundError("models/ directory not found. Please train a model first.")
        candidates = [p for p in models_root.iterdir() if p.is_dir() and (p / "model.joblib").exists()]
        if not candidates:
            raise FileNotFoundError("No model.joblib found under models/. Please train a model first.")
        model_dir = max(candidates, key=lambda p: p.stat().st_mtime)
    if not (model_dir / "model.joblib").exists():
        raise FileNotFoundError(f"model.joblib not found in {model_dir}")
    return model_dir


def load_labels(model_dir: Path, settings: dict[str, Any]) -> list[str]:
    label_map_path = model_dir / "label_to_id.json"
    if label_map_path.exists():
        label_to_id = json.loads(label_map_path.read_text(encoding="utf-8"))
        id_label_pairs = sorted(((int(idx), str(name)) for name, idx in label_to_id.items()), key=lambda x: x[0])
        return [label for _, label in id_label_pairs]
    return [str(label) for label in settings.get("labels", [])]


def load_report_data(model_dir: Path) -> dict[str, Any]:
    report_path = model_dir / "report.json"
    if not report_path.exists():
        return {}
    try:
        return json.loads(report_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return {}


def infer_include_status(feature_mode: str, report_data: dict[str, Any]) -> bool:
    if feature_mode == "keypoints":
        return False
    if feature_mode == "keypoints_status":
        return True
    return bool(report_data.get("data", {}).get("include_status", False))


def infer_sequence_frames(
    requested_frames: int | None,
    report_data: dict[str, Any],
    model: Any,
    include_status: bool,
    frame_vector_dim: int,
    settings_default_frames: int,
) -> int:
    if requested_frames is not None:
        if requested_frames <= 0:
            raise ValueError("--sequence-frames must be > 0")
        return requested_frames

    expected_dim = int(getattr(model, "n_features_in_", 0))
    status_extra = 2 if include_status else 0
    denominator = frame_vector_dim + status_extra
    if expected_dim > 0 and denominator > 0 and expected_dim % denominator == 0:
        return expected_dim // denominator

    report_frames = report_data.get("data", {}).get("sequence_frames")
    if report_frames is not None:
        return int(report_frames)

    return int(settings_default_frames)


def load_model_bundle(args: argparse.Namespace, settings: dict[str, Any]) -> ModelBundle:
    model_dir = resolve_model_dir(args.model_dir)
    model = joblib.load(model_dir / "model.joblib")

    report_data = load_report_data(model_dir)
    labels = load_labels(model_dir, settings)
    if not labels:
        raise RuntimeError("No labels available from model artifacts or settings.")

    frame_vector_dim = HAND_VECTOR_LENGTH * 2
    include_status = infer_include_status(args.feature_mode, report_data)
    sequence_frames = infer_sequence_frames(
        requested_frames=args.sequence_frames,
        report_data=report_data,
        model=model,
        include_status=include_status,
        frame_vector_dim=frame_vector_dim,
        settings_default_frames=int(settings.get("sample_frames", 40)),
    )

    return ModelBundle(
        model=model,
        model_dir=model_dir,
        labels=labels,
        include_status=include_status,
        sequence_frames=sequence_frames,
    )


def softmax(scores: np.ndarray) -> np.ndarray:
    stable = scores - np.max(scores)
    exp = np.exp(stable)
    total = float(np.sum(exp))
    if total <= 0:
        return np.full_like(exp, 1.0 / len(exp), dtype=np.float32)
    return (exp / total).astype(np.float32)


class RealtimeValidatorApp:
    def __init__(self, settings_path: str | Path, model_bundle: ModelBundle, args: argparse.Namespace) -> None:
        self.settings = load_settings(settings_path)
        self.model_bundle = model_bundle
        self.args = args

        self.labels = model_bundle.labels
        self.display_name_by_label = self._resolve_display_name_map()
        self.model = model_bundle.model
        self.sequence_frames = int(model_bundle.sequence_frames)
        self.include_status = bool(model_bundle.include_status)
        self.frame_vector_dim = HAND_VECTOR_LENGTH * 2
        self.expected_feature_dim = int(getattr(self.model, "n_features_in_", 0))
        self.class_ids = self._resolve_model_class_ids()

        validation_cfg = self.settings.get("validation", {})
        if not isinstance(validation_cfg, dict):
            validation_cfg = {}
        default_confidence = float(validation_cfg.get("confidence_threshold", 0.6))
        default_margin = float(validation_cfg.get("margin_threshold", 0.2))
        default_consecutive = int(validation_cfg.get("consecutive_frames", 3))

        self.confidence_threshold = (
            float(args.confidence_threshold) if args.confidence_threshold is not None else default_confidence
        )
        self.margin_threshold = float(args.margin_threshold) if args.margin_threshold is not None else default_margin
        consecutive_requested = int(args.consecutive_frames) if args.consecutive_frames is not None else default_consecutive
        self.consecutive_frames = max(1, int(consecutive_requested))
        self.label_confidence_thresholds = self._resolve_label_thresholds(
            validation_cfg.get("label_confidence_thresholds", {})
        )
        self.label_margin_thresholds = self._resolve_label_thresholds(
            validation_cfg.get("label_margin_thresholds", {})
        )
        self.neutral_labels = self._resolve_label_set(validation_cfg.get("neutral_labels", ["idle"]))
        self.hide_neutral_predictions = bool(validation_cfg.get("hide_neutral_predictions", True))
        self.cooldown_ms = max(0, int(validation_cfg.get("cooldown_ms", 1200)))
        self.enable_swipe_direction_guard = bool(validation_cfg.get("enable_swipe_direction_guard", True))
        self.swipe_direction_margin = max(0.0, float(validation_cfg.get("swipe_direction_margin", 0.18)))
        self.swipe_commit_on_hand_disappear = bool(validation_cfg.get("swipe_commit_on_hand_disappear", True))
        self.disable_swipe_left = True
        self.swipe_left_label = "swipe_left"
        self.swipe_right_label = "swipe_right"
        self.swipe_debug_log = bool(validation_cfg.get("swipe_debug_log", False))
        raw_swipe_log_path = str(validation_cfg.get("swipe_debug_log_path", "reports/swipe_direction_debug.log")).strip()
        swipe_log_path = Path(raw_swipe_log_path) if raw_swipe_log_path else Path("reports/swipe_direction_debug.log")
        if not swipe_log_path.is_absolute():
            swipe_log_path = ROOT / swipe_log_path
        self.swipe_debug_log_path = swipe_log_path.resolve()
        if self.swipe_debug_log:
            self.swipe_debug_log_path.parent.mkdir(parents=True, exist_ok=True)
        self.one_shot_per_appearance = bool(validation_cfg.get("one_shot_per_appearance", True))
        self.hand_disappear_reset_frames = max(1, int(validation_cfg.get("hand_disappear_reset_frames", 2)))
        self.require_neutral_reset = bool(validation_cfg.get("require_neutral_reset", True))
        self.neutral_reset_frames = max(1, int(validation_cfg.get("neutral_reset_frames", 4)))
        self.draw_landmarks = bool(args.draw_landmarks or self.settings.get("ui", {}).get("draw_landmarks", True))

        self.pipeline = LandmarkPipeline(self.settings)
        self.cap = self._open_camera()

        self.vector_buffer: deque[np.ndarray] = deque(maxlen=self.sequence_frames)
        self.left_status_buffer: deque[int] = deque(maxlen=self.sequence_frames)
        self.right_status_buffer: deque[int] = deque(maxlen=self.sequence_frames)

        self.last_candidate_label = "-"
        self.last_candidate_conf = 0.0
        self.last_candidate_margin = 0.0
        self.last_candidate_valid = False
        self.last_candidate_required_conf = self.confidence_threshold
        self.last_candidate_required_margin = self.margin_threshold
        self.last_candidate_hidden_neutral = False
        self.stable_label = "-"
        self.stable_hits = 0
        self.stability_counter: dict[str, int] = {}
        self.last_trigger_label = "-"
        self.last_trigger_conf = 0.0
        self.last_trigger_margin = 0.0
        self.last_trigger_time_ms = 0
        self.cooldown_until_ms = 0
        self.ready_for_next_event = True
        self.neutral_hits = 0
        self.waiting_hand_disappear_reset = False
        self.both_hands_missing_hits = 0
        self.pending_swipe_label = "-"
        self.pending_swipe_conf = 0.0
        self.pending_swipe_margin = 0.0
        self.pending_swipe_updated_ms = 0
        self.last_topk: list[tuple[str, float]] = []
        self.feature_error: str | None = None

        self.frame_interval_ms = int(self.settings.get("ui", {}).get("frame_interval_ms", 16))
        self.hotkeys = self._normalized_hotkeys()

        self.root = tk.Tk()
        self.root.title("Gesture 验证 UI")
        self.root.geometry("1380x840")
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.root.bind("<Key>", self._on_keypress)
        self.overlay_font = self._load_overlay_font(size=28)
        self.overlay_small_font = self._load_overlay_font(size=24)

        self.status_var = tk.StringVar(value="准备中...")
        self.model_var = tk.StringVar(value="")
        self.window_var = tk.StringVar(value="")
        self.runtime_var = tk.StringVar(value="左手:缺失 右手:缺失")
        self.pred_var = tk.StringVar(value="当前候选: 未开始")
        self.stable_var = tk.StringVar(value="确认输出: 无")
        self.topk_var = tk.StringVar(value="Top-3: -")
        self.dimension_var = tk.StringVar(value="")
        self.threshold_var = tk.StringVar(value="")

        self.video_label: ttk.Label | None = None
        self._build_layout()
        self._refresh_static_text()

    def _resolve_display_name_map(self) -> dict[str, str]:
        configured = self.settings.get("label_display_names", {})
        if not isinstance(configured, dict):
            configured = {}
        mapping: dict[str, str] = {}
        for label in self.labels:
            if label in configured and str(configured[label]).strip():
                mapping[label] = str(configured[label]).strip()
            elif label in DEFAULT_LABEL_DISPLAY_NAMES_ZH:
                mapping[label] = DEFAULT_LABEL_DISPLAY_NAMES_ZH[label]
            else:
                mapping[label] = label
        return mapping

    def _resolve_model_class_ids(self) -> np.ndarray:
        class_ids = getattr(self.model, "classes_", None)
        if class_ids is None and hasattr(self.model, "named_steps"):
            clf = self.model.named_steps.get("clf")
            class_ids = getattr(clf, "classes_", None)

        if class_ids is None:
            return np.arange(len(self.labels), dtype=np.int64)
        class_ids = np.asarray(class_ids)
        if class_ids.ndim != 1:
            return np.arange(len(self.labels), dtype=np.int64)
        return class_ids.astype(np.int64)

    def _resolve_label_thresholds(self, raw_mapping: Any) -> dict[str, float]:
        if not isinstance(raw_mapping, dict):
            return {}
        resolved: dict[str, float] = {}
        for label, value in raw_mapping.items():
            name = str(label).strip()
            if not name or name not in self.labels:
                continue
            try:
                numeric_value = float(value)
            except (TypeError, ValueError):
                continue
            if numeric_value < 0:
                continue
            resolved[name] = numeric_value
        return resolved

    def _resolve_label_set(self, raw_labels: Any) -> set[str]:
        if not isinstance(raw_labels, (list, tuple, set)):
            return set()
        result: set[str] = set()
        for label in raw_labels:
            name = str(label).strip()
            if name and name in self.labels:
                result.add(name)
        return result

    def _required_thresholds_for_label(self, label: str) -> tuple[float, float]:
        required_conf = self.label_confidence_thresholds.get(label, self.confidence_threshold)
        required_margin = self.label_margin_thresholds.get(label, self.margin_threshold)
        return required_conf, required_margin

    def _is_neutral_label(self, label: str) -> bool:
        return label in self.neutral_labels

    def _cooldown_remaining_ms(self) -> int:
        return max(0, self.cooldown_until_ms - int(time.monotonic() * 1000))

    def _log_swipe_debug(self, text: str) -> None:
        if not self.swipe_debug_log:
            return
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
        with self.swipe_debug_log_path.open("a", encoding="utf-8") as f:
            f.write(f"[{timestamp}] {text}\n")

    def _update_hand_disappear_gate(self, statuses: dict[str, str]) -> tuple[bool, bool]:
        both_missing = statuses.get("left", "missing") == "missing" and statuses.get("right", "missing") == "missing"
        if both_missing:
            self.both_hands_missing_hits = min(self.both_hands_missing_hits + 1, self.hand_disappear_reset_frames)
        else:
            self.both_hands_missing_hits = 0
        disappear_edge = both_missing and self.both_hands_missing_hits == self.hand_disappear_reset_frames

        if not self.one_shot_per_appearance:
            return False, disappear_edge

        if self.waiting_hand_disappear_reset and self.both_hands_missing_hits >= self.hand_disappear_reset_frames:
            self.waiting_hand_disappear_reset = False
            # Rearm only after disappearance; when neutral reset is enabled,
            # require a fresh neutral segment before allowing the next trigger.
            self.ready_for_next_event = not self.require_neutral_reset
            self.neutral_hits = 0
            return True, disappear_edge
        return False, disappear_edge

    def _normalized_hotkeys(self) -> dict[str, str]:
        defaults = {
            "reset_tracker": "r",
            "reset_buffer": "x",
            "quit": "escape",
        }
        settings_hotkeys = self.settings.get("ui", {}).get("hotkeys", {})
        if "reset_tracker" in settings_hotkeys:
            defaults["reset_tracker"] = str(settings_hotkeys["reset_tracker"]).lower()
        if "quit" in settings_hotkeys:
            defaults["quit"] = str(settings_hotkeys["quit"]).lower()
        return defaults

    def _open_camera(self) -> cv2.VideoCapture:
        camera_cfg = self.settings.get("camera", {})
        cap = cv2.VideoCapture(int(camera_cfg.get("device_index", 0)))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, int(camera_cfg.get("width", 1280)))
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, int(camera_cfg.get("height", 720)))
        if not cap.isOpened():
            raise RuntimeError("无法打开摄像头，请检查 setting.json 中 camera.device_index。")
        return cap

    def _load_overlay_font(self, size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
        ui_cfg = self.settings.get("ui", {})
        configured_path = str(ui_cfg.get("font_path", "")).strip()
        candidate_paths: list[Path] = []

        if configured_path:
            custom_path = Path(configured_path)
            if not custom_path.is_absolute():
                custom_path = ROOT / custom_path
            candidate_paths.append(custom_path.resolve())

        candidate_paths.extend(
            [
                Path(r"C:\Windows\Fonts\msyh.ttc"),
                Path(r"C:\Windows\Fonts\msyhbd.ttc"),
                Path(r"C:\Windows\Fonts\simhei.ttf"),
                Path(r"C:\Windows\Fonts\simsun.ttc"),
            ]
        )

        for font_path in candidate_paths:
            if font_path.exists():
                try:
                    return ImageFont.truetype(str(font_path), size=size)
                except OSError:
                    continue

        # Fallback may not display Chinese perfectly, but keeps UI functional.
        return ImageFont.load_default()

    def _build_layout(self) -> None:
        font_family = self.settings.get("ui", {}).get("font_family", "Microsoft YaHei")
        container = ttk.Frame(self.root, padding=12)
        container.pack(fill=tk.BOTH, expand=True)

        left = ttk.Frame(container)
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        right = ttk.Frame(container, width=360)
        right.pack(side=tk.RIGHT, fill=tk.Y)

        self.video_label = ttk.Label(left)
        self.video_label.pack(fill=tk.BOTH, expand=True)

        ttk.Label(right, text="实时验证", font=(font_family, 13, "bold")).pack(anchor=tk.W, pady=(0, 10))
        ttk.Label(right, textvariable=self.status_var, wraplength=330).pack(anchor=tk.W, pady=(0, 8))
        ttk.Label(right, textvariable=self.model_var, wraplength=330).pack(anchor=tk.W, pady=(0, 8))
        ttk.Label(right, textvariable=self.dimension_var, wraplength=330).pack(anchor=tk.W, pady=(0, 8))
        ttk.Label(right, textvariable=self.threshold_var, wraplength=330).pack(anchor=tk.W, pady=(0, 8))
        ttk.Label(right, textvariable=self.window_var).pack(anchor=tk.W, pady=(0, 8))
        ttk.Label(right, textvariable=self.runtime_var).pack(anchor=tk.W, pady=(0, 8))
        ttk.Label(right, textvariable=self.pred_var, font=(font_family, 11, "bold")).pack(anchor=tk.W, pady=(4, 6))
        ttk.Label(right, textvariable=self.stable_var, font=(font_family, 11, "bold")).pack(anchor=tk.W, pady=(0, 6))
        ttk.Label(right, textvariable=self.topk_var, wraplength=330, justify=tk.LEFT).pack(anchor=tk.W, pady=(0, 10))

        hotkey_text = (
            "快捷键\n"
            f"{self.hotkeys['reset_tracker']}: 重置跟踪器\n"
            "x: 清空推理窗口\n"
            f"{self.hotkeys['quit']}: 退出"
        )
        ttk.Label(right, text=hotkey_text, justify=tk.LEFT, wraplength=330).pack(anchor=tk.W)

    def _refresh_static_text(self) -> None:
        model_name = self.model_bundle.model_dir.name
        mode_text = "keypoints+status" if self.include_status else "keypoints"
        self.model_var.set(f"模型: {model_name}\n特征模式: {mode_text}")
        expected = self.expected_feature_dim if self.expected_feature_dim > 0 else "unknown"
        self.dimension_var.set(f"窗口帧数: {self.sequence_frames} | 期望特征维度: {expected}")
        conf_overrides = ", ".join(
            f"{self._display_label(label)}:{threshold:.2f}"
            for label, threshold in self.label_confidence_thresholds.items()
        )
        margin_overrides = ", ".join(
            f"{self._display_label(label)}:{threshold:.2f}"
            for label, threshold in self.label_margin_thresholds.items()
        )
        neutral_text = ", ".join(self._display_label(label) for label in sorted(self.neutral_labels)) or "-"
        conf_suffix = f" | 类别覆盖: {conf_overrides}" if conf_overrides else ""
        margin_suffix = f" | 类别覆盖: {margin_overrides}" if margin_overrides else ""
        gate_text = "开启" if self.require_neutral_reset else "关闭"
        one_shot_text = "开启" if self.one_shot_per_appearance else "关闭"
        swipe_guard_text = "开启" if self.enable_swipe_direction_guard else "关闭"
        swipe_commit_text = "开启" if self.swipe_commit_on_hand_disappear else "关闭"
        self.threshold_var.set(
            f"阈值: conf>={self.confidence_threshold:.2f}{conf_suffix}\n"
            f"      margin>={self.margin_threshold:.2f}{margin_suffix} | 连续帧:{self.consecutive_frames}\n"
            f"中性标签: {neutral_text} (隐藏: {'是' if self.hide_neutral_predictions else '否'}) | "
            f"冷却:{self.cooldown_ms}ms | 回中门控:{gate_text}/{self.neutral_reset_frames}帧 | "
            f"单次触发:{one_shot_text}/{self.hand_disappear_reset_frames}帧双手缺失重置 | "
            f"右滑防抖:{swipe_guard_text}/(right-left)>={self.swipe_direction_margin:.2f} | "
            f"右滑末帧确认:{swipe_commit_text} | 左滑:禁用"
        )
        self.window_var.set(f"窗口进度: {len(self.vector_buffer)} / {self.sequence_frames}")

    def _on_keypress(self, event: tk.Event) -> None:
        key = (event.keysym or "").lower()
        if key == self.hotkeys["reset_tracker"]:
            self.pipeline.reset_tracker()
            self.status_var.set("已重置跟踪器状态。")
        elif key == "x":
            self._reset_inference_window()
            self.status_var.set("已清空推理窗口。")
        elif key == self.hotkeys["quit"]:
            self.on_close()

    def _reset_inference_window(self) -> None:
        self.vector_buffer.clear()
        self.left_status_buffer.clear()
        self.right_status_buffer.clear()
        self.last_candidate_label = "-"
        self.last_candidate_conf = 0.0
        self.last_candidate_margin = 0.0
        self.last_candidate_valid = False
        self.last_candidate_required_conf = self.confidence_threshold
        self.last_candidate_required_margin = self.margin_threshold
        self.last_candidate_hidden_neutral = False
        self.stable_hits = 0
        self.stable_label = "-"
        self.stability_counter.clear()
        self.last_trigger_label = "-"
        self.last_trigger_conf = 0.0
        self.last_trigger_margin = 0.0
        self.last_trigger_time_ms = 0
        self.cooldown_until_ms = 0
        self.ready_for_next_event = True
        self.neutral_hits = 0
        self.waiting_hand_disappear_reset = False
        self.both_hands_missing_hits = 0
        self.pending_swipe_label = "-"
        self.pending_swipe_conf = 0.0
        self.pending_swipe_margin = 0.0
        self.pending_swipe_updated_ms = 0
        self.last_topk = []
        self.pred_var.set("当前候选: 未开始")
        self.stable_var.set("确认输出: 无")
        self.topk_var.set("Top-3: -")
        self._refresh_static_text()

    def _update_runtime_status(self, statuses: dict[str, str]) -> None:
        status_map = {"detected": "检测到", "filled": "补帧", "missing": "缺失"}
        left_status = status_map.get(statuses.get("left", "missing"), "缺失")
        right_status = status_map.get(statuses.get("right", "missing"), "缺失")
        self.runtime_var.set(f"左手:{left_status} 右手:{right_status}")

    def _build_feature_vector(self) -> np.ndarray | None:
        if len(self.vector_buffer) < self.sequence_frames:
            return None

        keypoint_seq = np.asarray(self.vector_buffer, dtype=np.float32)
        features = keypoint_seq.reshape(-1)

        if self.include_status:
            left_status = np.asarray(self.left_status_buffer, dtype=np.float32)
            right_status = np.asarray(self.right_status_buffer, dtype=np.float32)
            features = np.concatenate([features, left_status, right_status]).astype(np.float32)

        if self.expected_feature_dim > 0 and features.shape[0] != self.expected_feature_dim:
            self.feature_error = (
                "特征维度不匹配: "
                f"build={features.shape[0]}, model={self.expected_feature_dim}. "
                "请检查 feature-mode 或 sequence-frames 参数。"
            )
            return None

        self.feature_error = None
        return features

    def _label_from_class_id(self, class_id: int) -> str:
        if 0 <= class_id < len(self.labels):
            return self.labels[class_id]
        return f"class_{class_id}"

    def _display_label(self, label: str) -> str:
        return self.display_name_by_label.get(label, label)

    def _predict(
        self, features: np.ndarray
    ) -> tuple[str, float, float, list[tuple[str, float]], dict[str, float]]:
        x = features.reshape(1, -1)
        pred_class_id = int(self.model.predict(x)[0])

        raw_scores = self.model.decision_function(x)
        raw_scores = np.asarray(raw_scores, dtype=np.float32)
        if raw_scores.ndim == 2:
            logits = raw_scores[0]
        else:
            # Fallback for binary estimators returning a single margin.
            margin = float(raw_scores.ravel()[0])
            logits = np.array([-margin, margin], dtype=np.float32)

        probs = softmax(logits)
        pred_label = self._label_from_class_id(pred_class_id)
        probs_by_label: dict[str, float] = {}

        try:
            pred_score_idx = int(np.where(self.class_ids == pred_class_id)[0][0])
        except IndexError:
            pred_score_idx = int(np.argmax(probs))
        pred_conf = float(probs[pred_score_idx]) if pred_score_idx < len(probs) else float(np.max(probs))
        sorted_probs = np.sort(probs)[::-1]
        pred_margin = float(sorted_probs[0] - sorted_probs[1]) if len(sorted_probs) > 1 else float(sorted_probs[0])

        top_indices = np.argsort(probs)[::-1][: min(3, len(probs))]
        topk: list[tuple[str, float]] = []
        for score_idx in top_indices:
            class_id = int(self.class_ids[int(score_idx)]) if int(score_idx) < len(self.class_ids) else int(score_idx)
            topk.append((self._label_from_class_id(class_id), float(probs[int(score_idx)])))

        for score_idx, score in enumerate(probs):
            class_id = int(self.class_ids[int(score_idx)]) if int(score_idx) < len(self.class_ids) else int(score_idx)
            probs_by_label[self._label_from_class_id(class_id)] = float(score)

        return pred_label, pred_conf, pred_margin, topk, probs_by_label

    def _update_stable_output(self, label: str, is_valid: bool) -> bool:
        if not is_valid:
            self.stable_label = "-"
            self.stable_hits = 0
            self.stability_counter.clear()
            return False

        if label == self.stable_label:
            self.stable_hits = min(self.stable_hits + 1, self.consecutive_frames)
            return self.stable_hits >= self.consecutive_frames

        previous_count = self.stability_counter.get(label, 0) + 1
        self.stability_counter = {label: previous_count}
        if previous_count >= self.consecutive_frames:
            self.stable_label = label
            self.stable_hits = previous_count
        else:
            self.stable_hits = previous_count
        return previous_count >= self.consecutive_frames

    def _draw_overlay_pil(self, image: Image.Image) -> Image.Image:
        draw = ImageDraw.Draw(image)
        if self.last_candidate_label != "-":
            if self.last_candidate_valid:
                validity_text = "通过"
            else:
                validity_text = (
                    f"未达阈值(c>={self.last_candidate_required_conf:.2f}, "
                    f"m>={self.last_candidate_required_margin:.2f})"
                )
            pred_text = (
                f"当前候选: {self._display_label(self.last_candidate_label)} "
                f"(c={self.last_candidate_conf:.2f}, m={self.last_candidate_margin:.2f}, {validity_text})"
            )
        else:
            pred_text = "当前候选: -（无意义已隐藏）" if self.last_candidate_hidden_neutral else "当前候选: 未开始"
        cooldown_left_ms = self._cooldown_remaining_ms()
        if self.last_trigger_label != "-":
            stable_text = f"确认输出: {self._display_label(self.last_trigger_label)}"
            if cooldown_left_ms > 0:
                stable_text += f" | 冷却{cooldown_left_ms}ms"
        else:
            stable_text = "确认输出: 无"
        top_text = ""
        if self.last_topk:
            top_text = " | ".join(f"{self._display_label(name)}:{score:.2f}" for name, score in self.last_topk)
            top_text = f"候选: {top_text}"

        y0 = 16
        x0 = 16
        line_gap = 10
        p_bbox = draw.textbbox((x0, y0), pred_text, font=self.overlay_font)
        s_bbox = draw.textbbox((x0, y0), stable_text, font=self.overlay_small_font)
        widths = [p_bbox[2] - p_bbox[0], s_bbox[2] - s_bbox[0]]
        heights = [p_bbox[3] - p_bbox[1], s_bbox[3] - s_bbox[1]]
        if top_text:
            t_bbox = draw.textbbox((x0, y0), top_text, font=self.overlay_small_font)
            widths.append(t_bbox[2] - t_bbox[0])
            heights.append(t_bbox[3] - t_bbox[1])

        panel_w = max(widths) + 24
        panel_h = sum(heights) + line_gap * (len(heights) - 1) + 20
        draw.rectangle((x0 - 8, y0 - 8, x0 - 8 + panel_w, y0 - 8 + panel_h), fill=(0, 0, 0))

        y = y0
        draw.text((x0, y), pred_text, font=self.overlay_font, fill=(0, 255, 0))
        y += heights[0] + line_gap
        draw.text((x0, y), stable_text, font=self.overlay_small_font, fill=(255, 220, 80))
        if top_text:
            y += heights[1] + line_gap
            draw.text((x0, y), top_text, font=self.overlay_small_font, fill=(180, 220, 255))

        return image

    def _capture_loop(self) -> None:
        ok, frame = self.cap.read()
        if not ok:
            self.status_var.set("读取摄像头失败。")
            self.root.after(self.frame_interval_ms, self._capture_loop)
            return

        camera_cfg = self.settings.get("camera", {})
        if bool(camera_cfg.get("mirror_view", True)):
            frame = cv2.flip(frame, 1)

        output = self.pipeline.process(frame, draw_landmarks=self.draw_landmarks)
        self._update_runtime_status(output.statuses)
        hand_reset_triggered, hand_disappear_edge = self._update_hand_disappear_gate(output.statuses)

        self.vector_buffer.append(output.vector.copy())
        self.left_status_buffer.append(status_to_code(output.statuses.get("left", "missing")))
        self.right_status_buffer.append(status_to_code(output.statuses.get("right", "missing")))
        self._refresh_static_text()

        features = self._build_feature_vector()
        if features is not None:
            now_ms = int(time.monotonic() * 1000)
            pred_label, pred_conf, pred_margin, topk, probs_by_label = self._predict(features)
            is_neutral = self._is_neutral_label(pred_label)
            required_conf, required_margin = self._required_thresholds_for_label(pred_label)
            is_valid = pred_conf >= required_conf and pred_margin >= required_margin
            swipe_guard_reason = ""
            if self.disable_swipe_left and pred_label == self.swipe_left_label:
                is_valid = False
                swipe_guard_reason = "左滑已禁用，仅保留右滑"
            if (
                self.enable_swipe_direction_guard
                and pred_label == self.swipe_right_label
                and self.swipe_right_label in probs_by_label
            ):
                pred_swipe_prob = probs_by_label.get(self.swipe_right_label, pred_conf)
                opposite_swipe_prob = probs_by_label.get(self.swipe_left_label, 0.0)
                swipe_gap = float(pred_swipe_prob - opposite_swipe_prob)
                if swipe_gap < self.swipe_direction_margin:
                    is_valid = False
                    swipe_guard_reason = (
                        "右滑方向不明确 "
                        f"(gap={swipe_gap:.2f} < {self.swipe_direction_margin:.2f})"
                    )
                self._log_swipe_debug(
                    f"pred={pred_label} conf={pred_conf:.3f} margin={pred_margin:.3f} "
                    f"left={probs_by_label.get(self.swipe_left_label, 0.0):.3f} "
                    f"right={probs_by_label.get(self.swipe_right_label, 0.0):.3f} "
                    f"gap={swipe_gap:.3f} valid={is_valid}"
                )
            swipe_is_stable_candidate = (
                is_valid
                and pred_label == self.swipe_right_label
            )
            if self.swipe_commit_on_hand_disappear and swipe_is_stable_candidate:
                self.pending_swipe_label = pred_label
                self.pending_swipe_conf = pred_conf
                self.pending_swipe_margin = pred_margin
                self.pending_swipe_updated_ms = now_ms
                self._log_swipe_debug(
                    f"pending_swipe={pred_label} conf={pred_conf:.3f} margin={pred_margin:.3f}"
                )
                # Defer swipe trigger until hand-disappearance edge; keep latest direction.
                is_valid = False
                if not swipe_guard_reason:
                    swipe_guard_reason = "右滑已缓存，等待双手消失后确认"
            stable_ready = self._update_stable_output(pred_label, is_valid and not is_neutral)

            if is_valid and is_neutral:
                self.neutral_hits = min(self.neutral_hits + 1, self.neutral_reset_frames)
            else:
                self.neutral_hits = 0

            if self.require_neutral_reset and self.neutral_hits >= self.neutral_reset_frames:
                self.ready_for_next_event = True

            cooldown_left_ms = max(0, self.cooldown_until_ms - now_ms)
            in_cooldown = cooldown_left_ms > 0
            triggered = False
            blocked_reason = ""
            trigger_display_for_status = self._display_label(pred_label)

            if (
                self.swipe_commit_on_hand_disappear
                and hand_disappear_edge
                and self.pending_swipe_label != "-"
            ):
                pending_label = self.pending_swipe_label
                pending_conf = self.pending_swipe_conf
                pending_margin = self.pending_swipe_margin
                trigger_display_for_status = self._display_label(pending_label)
                if in_cooldown:
                    blocked_reason = f"冷却中 {cooldown_left_ms}ms"
                elif self.one_shot_per_appearance and self.waiting_hand_disappear_reset:
                    blocked_reason = (
                        "等待双手完全消失重置 "
                        f"{self.both_hands_missing_hits}/{self.hand_disappear_reset_frames} 帧"
                    )
                elif self.require_neutral_reset and not self.ready_for_next_event:
                    blocked_reason = f"等待回中 {self.neutral_hits}/{self.neutral_reset_frames} 帧"
                else:
                    self.last_trigger_label = pending_label
                    self.last_trigger_conf = pending_conf
                    self.last_trigger_margin = pending_margin
                    self.last_trigger_time_ms = now_ms
                    self.cooldown_until_ms = now_ms + self.cooldown_ms
                    if self.require_neutral_reset:
                        self.ready_for_next_event = False
                        self.neutral_hits = 0
                    # Swipe committed on disappearance boundary, no need to wait for disappearance again.
                    self.waiting_hand_disappear_reset = False
                    cooldown_left_ms = self.cooldown_ms
                    in_cooldown = self.cooldown_ms > 0
                    triggered = True
                    self._log_swipe_debug(
                        f"commit_pending_swipe={pending_label} conf={pending_conf:.3f} margin={pending_margin:.3f}"
                    )
                # Either committed or discarded on this disappear edge; clear stale pending swipe.
                self.pending_swipe_label = "-"
                self.pending_swipe_conf = 0.0
                self.pending_swipe_margin = 0.0
                self.pending_swipe_updated_ms = 0

            if (not triggered) and stable_ready:
                if in_cooldown:
                    blocked_reason = f"冷却中 {cooldown_left_ms}ms"
                elif self.one_shot_per_appearance and self.waiting_hand_disappear_reset:
                    blocked_reason = (
                        "等待双手完全消失重置 "
                        f"{self.both_hands_missing_hits}/{self.hand_disappear_reset_frames} 帧"
                    )
                elif self.require_neutral_reset and not self.ready_for_next_event:
                    blocked_reason = f"等待回中 {self.neutral_hits}/{self.neutral_reset_frames} 帧"
                else:
                    self.last_trigger_label = pred_label
                    self.last_trigger_conf = pred_conf
                    self.last_trigger_margin = pred_margin
                    self.last_trigger_time_ms = now_ms
                    self.cooldown_until_ms = now_ms + self.cooldown_ms
                    if self.require_neutral_reset:
                        self.ready_for_next_event = False
                        self.neutral_hits = 0
                    if self.one_shot_per_appearance:
                        self.waiting_hand_disappear_reset = True
                        self.both_hands_missing_hits = 0
                    cooldown_left_ms = self.cooldown_ms
                    in_cooldown = self.cooldown_ms > 0
                    triggered = True

            self.last_candidate_required_conf = required_conf
            self.last_candidate_required_margin = required_margin
            display_pred = self._display_label(pred_label)
            show_candidate = not (self.hide_neutral_predictions and is_neutral)
            if show_candidate:
                self.last_candidate_label = pred_label
                self.last_candidate_conf = pred_conf
                self.last_candidate_margin = pred_margin
                self.last_candidate_valid = is_valid
                self.last_candidate_hidden_neutral = False
                if is_valid:
                    self.pred_var.set(
                        f"当前候选: {display_pred} (c={pred_conf:.2f}, m={pred_margin:.2f})"
                    )
                else:
                    fail_reason = swipe_guard_reason if swipe_guard_reason else "未达阈值"
                    self.pred_var.set(
                        f"当前候选: {display_pred} (c={pred_conf:.2f}, m={pred_margin:.2f}) {fail_reason}"
                    )
            else:
                self.last_candidate_label = "-"
                self.last_candidate_conf = 0.0
                self.last_candidate_margin = 0.0
                self.last_candidate_valid = False
                self.last_candidate_hidden_neutral = True
                self.pred_var.set("当前候选: -（无意义已隐藏）")

            visible_topk = [
                (name, score)
                for name, score in topk
                if not (self.hide_neutral_predictions and self._is_neutral_label(name))
            ]
            self.last_topk = visible_topk
            if visible_topk:
                topk_text = " | ".join(f"{self._display_label(name)}:{score:.2f}" for name, score in visible_topk)
                self.topk_var.set(f"Top-3: {topk_text}")
            else:
                self.topk_var.set("Top-3: -")

            if self.last_trigger_label != "-":
                trigger_display = self._display_label(self.last_trigger_label)
                since_seconds = max(0.0, (now_ms - self.last_trigger_time_ms) / 1000.0)
                suffix_parts: list[str] = []
                if cooldown_left_ms > 0:
                    suffix_parts.append(f"冷却{cooldown_left_ms}ms")
                if self.one_shot_per_appearance and self.waiting_hand_disappear_reset:
                    suffix_parts.append(
                        f"等待双手消失 {self.both_hands_missing_hits}/{self.hand_disappear_reset_frames}"
                    )
                if self.require_neutral_reset and not self.ready_for_next_event:
                    suffix_parts.append(f"等待回中 {self.neutral_hits}/{self.neutral_reset_frames}")
                reset_suffix = f" | {' | '.join(suffix_parts)}" if suffix_parts else ""
                self.stable_var.set(
                    f"确认输出: {trigger_display} ({since_seconds:.1f}s 前){reset_suffix}"
                )
            else:
                self.stable_var.set("确认输出: 无")

            if triggered:
                self.status_var.set(
                    f"已触发手势: {trigger_display_for_status}，进入冷却 {self.cooldown_ms}ms。"
                )
            elif hand_reset_triggered:
                if self.require_neutral_reset:
                    self.status_var.set(
                        "检测到双手完全消失，已重置；需先回中后再触发下一次动作。"
                    )
                else:
                    self.status_var.set("检测到双手完全消失，已重置，可触发下一次动作。")
            elif blocked_reason:
                self.status_var.set(f"候选 {display_pred} 已稳定，但{blocked_reason}，暂不触发。")
            elif swipe_guard_reason:
                self.status_var.set(f"候选 {display_pred} 被抑制：{swipe_guard_reason}。")
            elif in_cooldown:
                self.status_var.set(f"冷却中 {cooldown_left_ms}ms。")
            elif self.one_shot_per_appearance and self.waiting_hand_disappear_reset:
                self.status_var.set(
                    "等待双手完全消失后重置："
                    f"{self.both_hands_missing_hits}/{self.hand_disappear_reset_frames} 帧。"
                )
            elif self.require_neutral_reset and not self.ready_for_next_event:
                self.status_var.set(f"等待回中：无意义需连续 {self.neutral_reset_frames} 帧。")
            elif is_neutral:
                self.status_var.set("当前为无意义段（已隐藏，不触发）。")
            else:
                self.status_var.set(
                    "实时推理中: "
                    f"{display_pred} 需 c>={required_conf:.2f}, m>={required_margin:.2f}; "
                    f"当前 c={pred_conf:.2f}, m={pred_margin:.2f}。"
                )
        elif self.feature_error:
            self.status_var.set(self.feature_error)
        else:
            needed = self.sequence_frames - len(self.vector_buffer)
            self.status_var.set(f"收集窗口中，还需 {max(0, needed)} 帧。")

        frame_rgb = cv2.cvtColor(output.annotated_frame, cv2.COLOR_BGR2RGB)
        image = Image.fromarray(frame_rgb)
        image = self._draw_overlay_pil(image)
        image = image.resize((980, 740))
        image_tk = ImageTk.PhotoImage(image=image)
        if self.video_label is not None:
            self.video_label.configure(image=image_tk)
            self.video_label.image = image_tk

        self.root.after(self.frame_interval_ms, self._capture_loop)

    def run(self) -> None:
        self.status_var.set("准备就绪，等待窗口填满后开始推理。")
        self._capture_loop()
        self.root.mainloop()

    def on_close(self) -> None:
        self.cap.release()
        self.pipeline.close()
        self.root.destroy()


def main() -> None:
    args = parse_args()
    settings = load_settings(args.settings)
    model_bundle = load_model_bundle(args=args, settings=settings)

    app = RealtimeValidatorApp(settings_path=args.settings, model_bundle=model_bundle, args=args)
    app.run()


if __name__ == "__main__":
    main()
