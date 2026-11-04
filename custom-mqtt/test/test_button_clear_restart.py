#!/usr/bin/env python3
"""按键清配网必须重启的契约测试。

背景（2026-11-04 真机命中）：BGWiFiConfig 的 clearWiFi() 只删配置文件
并断开 WiFi，不重启也不进配网模式；配网热点只由 begin() 在开机读不到
配置时开启。因此按键长按 >3s 的清配网分支必须在 clearWiFi() 之后调用
ESP.restart()，与维护页 handleOtaClearCfg 的处理保持一致，否则清完
配置设备仍按旧状态运行，只会反复出现恢复热点，永远进不了配网。

本测试截取 button_hok() 的 >3s 分支，断言其中 clearWiFi() 之后存在
ESP.restart()。
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "custom-mqtt" / "12F" / "12F.ino"


def button_function(source: str) -> str:
    start = source.index("void button_hok()")
    end = source.index("\n}\n", start)
    return source[start:end]


def main() -> None:
    func = button_function(SOURCE.read_text(encoding="utf-8"))
    branch_start = func.index("reset config")
    branch = func[branch_start:]
    assert "clearWiFi()" in branch, ">3s 分支应调用 clearWiFi()"
    clear_at = branch.index("clearWiFi()")
    restart_at = branch.find("ESP.restart()", clear_at)
    assert restart_at > clear_at, (
        ">3s 清配网分支在 clearWiFi() 之后必须 ESP.restart()，"
        "否则设备不会进入配网模式"
    )
    print("button clear-config restart contract: OK")


if __name__ == "__main__":
    main()
