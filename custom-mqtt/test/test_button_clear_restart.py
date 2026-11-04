#!/usr/bin/env python3
"""按键清配网必须走共享清配网函数的契约测试。

背景（2026-11-04 真机命中）：按键长按 >3s 此前只调用库的 clearWiFi()
（只删文件、不重启），设备永远进不了配网。修复后该分支统一改用
clearProvisioningAndRestart()——删文件+回读校验+擦 SDK 凭据+重启，
其内部步骤由 test_clear_provisioning.py 锁定。本测试只断言按键分支
确实调用了这个共享函数，防止以后又退回只删不重启的老路。
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "custom-mqtt" / "12F" / "12F.ino"


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    start = source.index("void button_hok()")
    func = source[start:source.index("\n}\n", start)]
    branch = func[func.index("reset config"):]
    assert "clearProvisioningAndRestart()" in branch, (
        "按键 >3s 清配网分支必须调用 clearProvisioningAndRestart()，"
        "否则清完配置不会重启进配网"
    )
    assert "clearWiFi()" not in branch, "按键分支不应再直接调用库 clearWiFi()"
    print("button clear-config delegation contract: OK")


if __name__ == "__main__":
    main()
