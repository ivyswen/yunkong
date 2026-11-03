#!/usr/bin/env python3
"""配网页静态契约测试。

配网页是嵌在固件里的 HTML，无法在主机侧启动设备验证。本测试只检查
表单字段名（BGWiFiConfig 的数据契约）和用户必须在页面上看到的关键说明，
防止后续改页面时误删字段或把两种模式的字段用途写反。
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "custom-mqtt" / "12F" / "12F.ino"


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    html = source.replace('\\"', '"')

    # 表单契约：action、method 和字段名必须与 BGWiFiConfig 处理端一致。
    for fragment in (
        'action="htmlresult"',
        'method="post"',
        'name="ssid"',
        'name="pwd"',
        'name="umsg1"',
        'name="umsg2"',
        'name="umsg3"',
        'name="umsg4"',
        'name="umsg5"',
        'name="umsg6"',
        'name="mode" value="1"',
    ):
        assert fragment in html, f"配网页缺少表单契约: {fragment}"

    # 页面必须直接说明模式判定和每个字段的关键约束/默认值。
    for fragment in (
        "服务模式由“服务器”字段决定",
        "只能连接 2.4GHz WiFi",
        "yunkong/设备ID/set、state、online",
        "只能使用 1–32 个英文字母、数字、下划线或连字符",
        "不要填写 AccessKey",
        "不要添加 mqtt://、端口号或路径",
        "留空时：OneNET 使用 8883，自建 MQTT 使用 1883",
        "留空自动使用 MiXfbNnY6r",
        "用户名留空时按匿名连接，密码不会单独发送",
        "BGWiFiConfig 的配网参数",
        "不是服务模式开关",
    ):
        assert fragment in html, f"配网页缺少字段说明: {fragment}"

    print("provisioning html contract: OK")


if __name__ == "__main__":
    main()
