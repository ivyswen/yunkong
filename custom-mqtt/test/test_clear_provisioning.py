#!/usr/bin/env python3
"""双保险清配网契约测试。

背景（2026-11-04 真机命中）：网页点「清除配网并重启」，设备重启了但
配网信息还在。库的 clearWiFi() 删文件不校验返回值、也不擦 ESP8266
SDK 自己持久化的 WiFi 凭据，两种失效模式都是静默的。因此固件改为
自行清配网，网页与按键两条路径共用 clearProvisioningAndRestart()：

- LittleFS 删除 wifiset.txt / umsg.txt，并用 exists() 回读确认删净；
- WiFi.disconnect(true) 并 ESP.eraseConfig() 擦除 SDK 凭据；
- 串口打印每步结果（失效不再静默）；
- 最后 ESP.restart()，靠开机读不到配置进入配网模式。

本测试锁定上述结构：共享函数存在且包含各步骤，两个调用点都改用它、
且不再直接调用库的 clearWiFi()。
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "custom-mqtt" / "12F" / "12F.ino"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.index("\n}\n", start)
    return source[start:end]


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")

    helper = function_body(source, "clearProvisioningAndRestart()")
    for fragment in (
        'LittleFS.remove("/bgwificonfig/wifiset.txt")',
        'LittleFS.remove("/bgwificonfig/umsg.txt")',
        'LittleFS.exists("/bgwificonfig/wifiset.txt")',
        'LittleFS.exists("/bgwificonfig/umsg.txt")',
        "WiFi.disconnect(true)",
        "ESP.eraseConfig()",
        "ESP.restart()",
    ):
        assert fragment in helper, f"共享清配网函数缺少步骤: {fragment}"

    web = function_body(source, "void handleOtaClearCfg()")
    assert "clearProvisioningAndRestart()" in web, "网页清配网没有改用共享函数"
    assert "clearWiFi()" not in web, "网页清配网不应再直接调用库 clearWiFi()"

    button = function_body(source, "void button_hok()")
    branch = button[button.index("reset config"):]
    assert "clearProvisioningAndRestart()" in branch, "按键清配网没有改用共享函数"
    assert "clearWiFi()" not in branch, "按键清配网不应再直接调用库 clearWiFi()"

    print("clear provisioning (dual-assurance) contract: OK")


if __name__ == "__main__":
    main()
