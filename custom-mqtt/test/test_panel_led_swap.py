#!/usr/bin/env python3
"""面板 ESP-M4 变体 LED 引脚对调契约。

真机证据（2026-11-05）：面板版状态页显示「关（红灯）」时实际亮的
是蓝灯——灯色与固件语义相反。本固件 led() 的驱动恒为一脚高、
一脚低（选中灯亮/闪、另一灯灭），故「对调两个 LED 引脚」与
「整体反相驱动」在现象上严格等价，任一根因（引脚对调或有效
电平相反）下对调都能修正。继电器（FLAG）方向未经实测，
不得凭灯的结论推翻——本测试同时锁定 FLAG/BUTTON 不被误动。
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
SKETCH = (ROOT / "custom-mqtt" / "12F" / "12F.ino").read_text(encoding="utf-8")
BUILD = (ROOT / "custom-mqtt" / "build.sh").read_text(encoding="utf-8")


def block(text, start_marker, end_marker):
    i = text.index(start_marker)
    j = text.index(end_marker, i)
    return text[i:j]


def main():
    # build.sh：esp8285 分支传变体宏，默认分支不带
    esp_case = block(BUILD, "esp8285)", ";;")
    assert "BOARD_ESP8285_PANEL" in esp_case, "esp8285 变体应定义 BOARD_ESP8285_PANEL"
    assert "EXTRA_DEFINE" in BUILD, "build.sh 应有变体附加宏变量"
    assert "compiler.cpp.extra_flags=" in BUILD and "${EXTRA_DEFINE}" in BUILD, (
        "附加宏应注入 extra_flags"
    )
    plug_case = block(BUILD, "12f)", ";;")
    assert "BOARD_ESP8285_PANEL" not in plug_case, "插头版不得带面板宏"

    # 草图：宏内对调、宏外原样
    panel = block(SKETCH, "#ifdef BOARD_ESP8285_PANEL", "#else")
    assert "#define LED_B 16" in panel, "面板变体蓝灯应为 GPIO16"
    assert "#define LED_RED 12" in panel, "面板变体红灯应为 GPIO12"
    default = block(SKETCH, "#else", "#endif")
    assert "#define LED_B 12" in default, "默认蓝灯应保持 GPIO12"
    assert "#define LED_RED 16" in default, "默认红灯应保持 GPIO16"

    # 继电器与按键：全局各只有一处定义、值不变（防误动）
    assert SKETCH.count("#define FLAG 14") == 1, "FLAG 必须唯一且仍为 GPIO14"
    assert SKETCH.count("#define BUTTON 13") == 1, "BUTTON 必须唯一且仍为 GPIO13"
    assert SKETCH.count("#define LED_B") == 2, "LED_B 应恰两处（变体+默认）"
    assert SKETCH.count("#define LED_RED") == 2, "LED_RED 应恰两处（变体+默认）"
    print("panel led swap contract: OK")


if __name__ == "__main__":
    main()
