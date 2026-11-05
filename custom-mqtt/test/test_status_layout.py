#!/usr/bin/env python3
"""维护页分区布局显示契约（2026-11-05 用户要求）。

ESP8266+eboot 没有 ESP32 式双执行分区：应用恒在 0x1000 运行，OTA
镜像先写暂存窗、重启时由 eboot 拷回。状态页应如实展示布局：
运行固件大小、OTA 暂存窗（起点与可用空间）、Flash 总量与文件
系统起点，口径与 otaVerifyStaged 的暂存地址公式一致。
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = (ROOT / "custom-mqtt" / "12F" / "12F.ino").read_text(encoding="utf-8")


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i)]


def main():
    status = body(SRC, "void handleStatus()", "\n}\n")
    for key in ("sketch_bytes", "flash_bytes", "ota_space", "ota_offset", "fs_offset"):
        assert f'\\"{key}\\"' in status, f"/status 缺少字段 {key}"
    for call in (
        "ESP.getSketchSize()",
        "ESP.getFreeSketchSpace()",
        "ESP.getFlashChipRealSize()",
    ):
        assert call in status, f"handleStatus 应调用 {call}"
    assert "FLASH_SECTOR_SIZE - 1" in status and "& ~(FLASH_SECTOR_SIZE - 1)" in status, (
        "暂存窗起点须用与回读校验一致的扇区取整公式"
    )

    page = body(SRC, "function fetchStatus()", "fetchStatus();")
    for label in ("运行固件", "OTA 暂存", "Flash"):
        assert label in page, f"状态表缺少「{label}」行"
    for ref in ("s.sketch_bytes", "s.ota_offset", "s.ota_space", "s.flash_bytes", "s.fs_offset"):
        assert ref in page, f"状态表应渲染 {ref}"
    print("status layout contract: OK")


if __name__ == "__main__":
    main()
