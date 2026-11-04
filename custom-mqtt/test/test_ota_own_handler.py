#!/usr/bin/env python3
"""自有 OTA 上传处理契约测试。

背景（2026-11-04 真机实锤）：网页 OTA 上传约 8 秒后 Soft WDT reset +
Exception (4) 崩溃，同源码 ELF 解码堆栈落在库上传路径的
UpdaterClass::write → spi_flash_write_page_break——BGWiFiConfig 的
上传回调把整块数据一次交给 Update.write，写 Flash 期间软看门狗被饿死。
且库的 OTAbegin() 仅在调用瞬间 WiFi 已连通才注册 "/" 与 "/otaupdate"
并 begin 服务器，慢速开机时整段运行期无 OTA（恢复热点根页 404）。

因此固件改为自有 OTA 处理，契约如下：
- setup() 不再调用库 OTAbegin()/setOTAhtml()/setOTATimeOut()；
- 已配网（STA 模式）时无条件注册 "/"、"/otaupdate"、"/status"、
  "/otaclearcfg" 并 OTAserver.begin()，与开机时 WiFi 连接快慢无关；
  配网 AP 模式下不启动维护服务器（与库原行为一致，避免端口冲突）；
- 上传回调把每块数据按 OTA_WRITE_SLICE 切片写 Flash，每片后
  ESP.wdtFeed() 并 yield()，不再整块直写；
- 成功与否经 otaUpdateOk 标志传给 POST 完成处理：仅成功才回 OK 并
  重启，失败回 FAIL 且不重启（可直接重试）。
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

    setup = function_body(source, "void setup()")
    for banned in ("wifipw.OTAbegin()", "wifipw.setOTAhtml(", "wifipw.setOTATimeOut("):
        assert banned not in setup, f"setup 不应再调用库 OTA 入口: {banned}"

    gate = "if (WiFi.getMode() == WIFI_STA)"
    assert gate in setup, "自有 OTA 注册须以 STA（已配网）为门控，配网 AP 下不启动"
    gate_at = setup.index(gate)
    for fragment in (
        'OTAserver.on("/", HTTP_GET, handleOtaRoot)',
        'OTAserver.on("/otaupdate", HTTP_POST, handleOtaUpdateDone, handleOtaUpdateUpload)',
        'OTAserver.on("/otaclearcfg", HTTP_POST, handleOtaClearCfg)',
        'OTAserver.on("/status", HTTP_GET, handleStatus)',
        "OTAserver.begin()",
    ):
        assert fragment in setup, f"setup 缺少自有 OTA 注册: {fragment}"
        assert setup.index(fragment) > gate_at, f"OTA 注册应在 STA 门控之内: {fragment}"
    root = function_body(source, "void handleOtaRoot()")
    assert "otaIndexHtml" in root, "根页处理应继续 serving 既有维护页 otaIndexHtml"
    assert 'send(200, "text/html; charset=utf-8"' in root, "根页应以 HTML 200 返回"

    upload = function_body(source, "void handleOtaUpdateUpload()")
    for fragment in (
        "UPLOAD_FILE_START",
        "ESP.getFreeSketchSpace()",
        "Update.begin(",
        "UPLOAD_FILE_WRITE",
        "OTA_WRITE_SLICE",
        "Update.write(",
        "ESP.wdtFeed()",
        "yield()",
        "UPLOAD_FILE_END",
        "Update.end(true)",
        "otaUpdateOk = true",
        "UPLOAD_FILE_ABORTED",
    ):
        assert fragment in upload, f"上传处理缺少片段: {fragment}"
    write_branch = upload[upload.index("UPLOAD_FILE_WRITE"):upload.index("UPLOAD_FILE_END")]
    assert write_branch.count("Update.write(") == 1, "写入分支应只有一个切片写入口（循环切片）"
    assert "while" in write_branch, "写入分支应以循环切片，而非整块直写"
    assert "Update.write(upload.buf, upload.currentSize)" not in upload, "库式整块直写必须移除"

    done = function_body(source, "void handleOtaUpdateDone()")
    assert "otaUpdateOk" in done, "完成处理应以 otaUpdateOk 判定成败"
    assert '"OK"' in done and '"FAIL"' in done, "完成处理应回 OK/FAIL（与库语义一致）"
    assert "ESP.restart()" in done, "成功后应重启运行新固件"

    print("own OTA handler (sliced write + wdt feed) contract: OK")


if __name__ == "__main__":
    main()
