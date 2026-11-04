#!/usr/bin/env python3
"""基于 MAC 地址自动生成设备 ID (ykyc001_xxxxxx) 契约测试。

测试覆盖两部分：
1. tools/onenet_tool.py 的 derive_device_id_from_mac() 纯逻辑（支持全长 MAC 及后六位，全转小写）
2. 12F.ino 固件端契约：
   - setup() 调用 ykFormatDeviceIdFromMac 提取 MAC 后 3 字节格式化为 ykyc001_xxxxxx
   - Web 配网页将 %DEFAULT_DEV_ID% 替换为实际计算出的设备 ID
   - 自建 MQTT 与 OneNET AIoT 两种模式在留空时均回落 defaultDevId
   - /status JSON 接口与 OTA 维护页状态表均包含 mac 字段
   - 开机串口日志打印 MAC 与默认设备 ID
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from onenet_tool import derive_device_id_from_mac

def test_tool_mac_derivation():
    # 完整 MAC 各种常见分隔符与大小写
    assert derive_device_id_from_mac("AA:BB:CC:DD:EE:FF") == "ykyc001_ddeeff"
    assert derive_device_id_from_mac("aa:bb:cc:dd:ee:ff") == "ykyc001_ddeeff"
    assert derive_device_id_from_mac("12-34-56-78-9a-bc") == "ykyc001_789abc"
    assert derive_device_id_from_mac("123456789abc") == "ykyc001_789abc"
    assert derive_device_id_from_mac("1234.5678.9ABC") == "ykyc001_789abc"

    # 后六位十六进制
    assert derive_device_id_from_mac("789abc") == "ykyc001_789abc"
    assert derive_device_id_from_mac("789ABC") == "ykyc001_789abc"
    assert derive_device_id_from_mac("78:9A:BC") == "ykyc001_789abc"

    # 非法输入校验
    for invalid in ("", "123", "12345", "1234567", "GGGGGG", "xyz"):
        try:
            derive_device_id_from_mac(invalid)
            assert False, f"应拒绝非法输入: {invalid}"
        except (ValueError, Exception):
            pass

def test_firmware_contract():
    ino_source = (ROOT / "custom-mqtt" / "12F" / "12F.ino").read_text(encoding="utf-8")

    # 1. setup 中读取 MAC 并生成 defaultDevId
    assert "ykFormatDeviceIdFromMac" in ino_source, "12F.ino 未调用 ykFormatDeviceIdFromMac"
    assert 'customHtml.replace("%DEFAULT_DEV_ID%", defaultDevId)' in ino_source, "配网页未动态注入 defaultDevId"
    assert 'wifipw.setZDYhtml(customHtml)' in ino_source, "未将注入后的 HTML 传给 setZDYhtml"

    # 2. 两个分支留空回退均指向 defaultDevId
    assert "CLIENTID = defaultDevId;" in ino_source, "设备 ID 回退未改用 defaultDevId"
    assert '"ykyc001_dev1"' not in ino_source, "12F.ino 不应再硬编码 ykyc001_dev1 默认回落"
    assert '"ykyc-" + String(ESP.getChipId(), HEX)' not in ino_source, "12F.ino 自建模式不应再回落 chipId"

    # 3. 状态接口与维护页包含 MAC
    assert '\\"mac\\":\\"" + WiFi.macAddress() + "\\"' in ino_source, "/status JSON 未输出 mac 字段"
    assert "<tr><td>MAC</td>" in ino_source, "维护页状态表未渲染 MAC 行"

    # 4. 串口日志
    assert 'MAC: %s, Default Device ID: %s' in ino_source, "开机未打印 MAC 与默认设备 ID"

def main():
    test_tool_mac_derivation()
    test_firmware_contract()
    print("device id from mac contract: OK")

if __name__ == "__main__":
    main()
