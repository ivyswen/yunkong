#!/usr/bin/env python3
"""配网页静态契约测试。

配网页是嵌在固件里的 HTML，无法在主机侧启动设备验证。本测试只检查
provisioningHtml 这一段页面本身（不扫整个 .ino，避免别处的同名字符串
让断言假绿），分两层：

1. 结构层：用 HTML 解析器核对表单契约——恰好一个 form 且
   action/method 正确；ssid、pwd、umsg1–umsg6 必须都在 form 内、
   各出现一次且为 text 输入；mode 必须是带 checked 的 radio 且值 1；
   另有一个 submit。字段被移出 form、同名重复、丢 type/checked 都会失败。
2. 文案层：用户必须在页面上看到的关键说明逐条存在，包括模式判定的
   真实规则（小写子串匹配、区分大小写）与端口非法输入的实际处理
   （回落默认、数字后混字母会被截断解析），防止文案与代码漂移。
"""

import ast
import re
from html.parser import HTMLParser
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "custom-mqtt" / "12F" / "12F.ino"

TEXT_FIELDS = ["ssid", "pwd", "umsg1", "umsg2", "umsg3", "umsg4", "umsg5", "umsg6"]


def extract_page(source: str) -> str:
    """只截取 provisioningHtml 声明（从声明行到以 \"; 结尾的行）。"""
    lines = source.splitlines()
    start = next(
        i for i, line in enumerate(lines) if "provisioningHtml[] PROGMEM" in line
    )
    block_lines = []
    for line in lines[start:]:
        block_lines.append(line)
        if line.rstrip().endswith('";'):
            break
    else:
        raise AssertionError("provisioningHtml 声明没有找到结束行")
    literals = re.findall(r'"((?:\\.|[^"\\])*)"', "\n".join(block_lines))
    return "".join(ast.literal_eval('"' + part + '"') for part in literals)


class FormProbe(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.forms = []  # [(attrs, [input attrs, ...]), ...]
        self._open_form = None

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "form":
            assert self._open_form is None, "form 不应嵌套"
            self._open_form = (attrs, [])
            self.forms.append(self._open_form)
        elif tag == "input" and self._open_form is not None:
            self._open_form[1].append(attrs)

    def handle_endtag(self, tag):
        if tag == "form":
            self._open_form = None


def main() -> None:
    page = extract_page(SOURCE.read_text(encoding="utf-8"))

    # ---- 结构层：表单契约 ----
    probe = FormProbe()
    probe.feed(page)
    probe.close()
    assert len(probe.forms) == 1, f"页面应恰好有一个 form，实际 {len(probe.forms)}"
    form_attrs, inputs = probe.forms[0]
    assert form_attrs.get("action") == "htmlresult", "form action 必须是 htmlresult"
    assert form_attrs.get("method") == "post", "form method 必须是 post"

    by_name = {}
    submits = []
    for attrs in inputs:
        name = attrs.get("name")
        if name is None:
            assert attrs.get("type") == "submit", "无 name 的 input 只能是提交按钮"
            submits.append(attrs)
        else:
            assert name not in by_name, f"字段重复出现: {name}"
            by_name[name] = attrs
    assert len(submits) == 1, "页面应恰好有一个提交按钮"

    for field in TEXT_FIELDS:
        assert field in by_name, f"form 内缺少字段: {field}"
        assert by_name[field].get("type") == "text", f"{field} 必须是 text 输入"

    assert "mode" in by_name, "form 内缺少 mode 字段"
    mode = by_name["mode"]
    assert mode.get("type") == "radio", "mode 必须是 radio"
    assert mode.get("value") == "1", "mode 的值必须是 1"
    assert "checked" in mode, "mode 必须默认 checked"

    # ---- 文案层：关键说明逐条存在 ----
    for fragment in (
        "服务模式由“服务器”字段决定",
        "只能连接 2.4GHz WiFi",
        "yunkong/设备ID/set、state、online",
        "%DEFAULT_DEV_ID%（留空自动使用）",
        "两种模式均自动使用当前设备默认 ID（%DEFAULT_DEV_ID%）",
        "只能使用 1–32 个英文字母、数字、下划线或连字符",
        "不要填写 AccessKey",
        # Nit 2：模式判定是区分大小写的小写子串匹配，文案必须说清
        "cmcconenet.cn",
        "heclouds.com",
        "区分大小写",
        "不要添加 mqtt://、端口号或路径",
        # Nit 3：端口留空默认值 + 非法输入的实际处理
        "留空时：OneNET 使用 8883，自建 MQTT 使用 1883",
        "超过 65535",
        "截断解析",
        "留空自动使用 MiXfbNnY6r",
        "用户名留空时按匿名连接，密码不会单独发送",
        "BGWiFiConfig 的配网参数",
        "不是服务模式开关",
    ):
        assert fragment in page, f"配网页缺少字段说明: {fragment}"

    print("provisioning html contract: OK")


if __name__ == "__main__":
    main()
