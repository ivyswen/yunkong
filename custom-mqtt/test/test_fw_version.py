#!/usr/bin/env python3
"""固件版本注入契约测试。

需求：OTA 后要能从 Web 页面区分新旧固件。约定如下，本测试逐项锁定：

- 源码用 FW_GIT_HASH / FW_BUILD_TIME 两个宏表示版本，未注入时分别回落
  "dev" 与 __DATE__/__TIME__，保证裸 arduino-cli 命令也能编译；
- custom-mqtt/build.sh 在编译时用 git 短哈希（工作区脏时加 -dirty）和
  编译时间自动注入这两个宏；
- 版本显示在三处：维护页 /status JSON 的 fw_version、build_time 字段
  （且状态表 JS 要渲染它）、配网页页脚、开机串口日志。
"""

import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "custom-mqtt" / "12F" / "12F.ino"
BUILD_SH = ROOT / "custom-mqtt" / "build.sh"


def provisioning_block(source: str) -> str:
    lines = source.splitlines()
    start = next(
        i for i, line in enumerate(lines) if "provisioningHtml[] PROGMEM" in line
    )
    block = []
    for line in lines[start:]:
        block.append(line)
        if line.rstrip().endswith('";'):
            break
    return "\n".join(block)


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")

    # 宏与回落值
    assert "#ifndef FW_GIT_HASH" in source, "缺少 FW_GIT_HASH 注入点"
    assert '#define FW_GIT_HASH "dev"' in source, "FW_GIT_HASH 未回落 dev"
    assert "#ifndef FW_BUILD_TIME" in source, "缺少 FW_BUILD_TIME 注入点"
    assert "__DATE__" in source and "__TIME__" in source, "FW_BUILD_TIME 未回落编译日期宏"

    # 配网页页脚：要有版本标签，且版本来自宏（不是写死字符串）
    block = provisioning_block(source)
    assert "FW_GIT_HASH" in block, "配网页没有引用 FW_GIT_HASH"
    literals = re.findall(r'"((?:\\.|[^"\\])*)"', block)
    page = "".join(ast.literal_eval('"' + p + '"') for p in literals)
    assert "固件版本" in page, "配网页页脚缺少固件版本标签"
    assert "编译时间" in page, "配网页页脚缺少编译时间标签"

    # 维护页：/status JSON 字段 + 状态表渲染 + 串口日志
    assert '\\"fw_version\\"' in source, "/status JSON 缺少 fw_version 字段"
    assert '\\"build_time\\"' in source, "/status JSON 缺少 build_time 字段"
    assert "s.fw_version" in source, "维护页状态表没有渲染 fw_version"
    assert "FW version" in source, "开机串口日志没有打印固件版本"

    # 构建脚本：自动注入
    assert BUILD_SH.exists(), "缺少 custom-mqtt/build.sh"
    script = BUILD_SH.read_text(encoding="utf-8")
    assert "rev-parse --short HEAD" in script, "build.sh 没有取 git 短哈希"
    assert "FW_GIT_HASH" in script and "FW_BUILD_TIME" in script, "build.sh 没有注入版本宏"
    assert "-dirty" in script, "build.sh 没有处理工作区脏标记"

    print("fw version injection contract: OK")


if __name__ == "__main__":
    main()
