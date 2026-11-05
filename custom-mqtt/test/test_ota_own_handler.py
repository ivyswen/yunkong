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

    # 第二轮根因证据（2026-11-04 19:29 对照实验）：小 15KB 的作者 bin
    # 同样在约 7.6 秒 Soft WDT 崩溃，epc 落在 ROM Wait_SPI_Idle——单次
    # Flash 擦/写操作本身阻塞超过软看门狗窗口，切片间喂狗救不了操作内
    # 部。因此上传全程关软狗（硬件狗仍在），并加进度/计时日志定位。
    assert "ESP.wdtDisable()" in upload, "上传开始应关软看门狗"
    assert upload.index("ESP.wdtDisable()") < upload.index("Update.begin("), (
        "关软狗应在 Update.begin 之前"
    )
    assert upload.count("ESP.wdtEnable(0)") >= 2, "结束与中止两路都应恢复软看门狗"
    assert "upload.totalSize" in upload, "进度日志应使用 upload.totalSize"
    assert "32 * 1024" in upload or "32768" in upload, "应每 32KB 打一行进度日志"
    assert "millis()" in upload, "进度与收尾日志应带时间戳以便定位阻塞点"

    # 第三轮根因（2026-11-04 20:20 真机坐实）：OTA 写出的镜像带静默
    # 坏块——同 bin 串口重刷（esptool 写后校验）后 Web 访问不再崩溃
    # 循环。Updater 的 MD5 只算内存缓冲、从不回读 Flash，故上传处理
    # 必须自带回读校验：接收时算 MD5，Update.end 后回读暂存区逐块
    # 比对，一致才置 otaUpdateOk（才回 OK 才重启）；不一致保持 FAIL。
    assert "MD5Builder" in source, "应使用 MD5Builder 做回读校验"
    assert "otaRxMd5.add(" in upload, "接收时应逐块累计 MD5"
    verify = function_body(source, "bool otaVerifyStaged(uint32_t len)")
    for fragment in (
        "ESP.flashRead(",
        "ESP.getSketchSize()",
        "ESP.getFreeSketchSpace()",
        "otaFileModeByte",
        "rb.toString()",
    ):
        assert fragment in verify, f"回读校验缺少片段: {fragment}"
    # 暂存地址必须按 Update.begin 实际收到的尺寸复算：begin 传的是
    # maxSketchSpace（整窗末端锚定），不是文件尺寸——首版用文件尺寸
    # 算地址读偏 0xA000、必然误报不符（2026-11-04 20:28 真机命中，
    # 安全失败未重启）。故 FILE_START 须把 maxSketchSpace 存入
    # otaMaxSpace，校验按它复算窗口起点。
    assert "otaMaxSpace = maxSketchSpace" in upload, "应保存 begin 的窗口尺寸"
    assert "Update.begin(otaMaxSpace)" in upload, "begin 应使用保存的窗口尺寸变量"
    assert "otaMaxSpace" in verify, "回读地址应按 begin 的窗口尺寸复算"
    assert "(len + FLASH_SECTOR_SIZE" not in verify, "回读地址不得再按文件尺寸复算"
    end_branch = upload[upload.index("UPLOAD_FILE_END"):]
    assert "otaVerifyStaged(upload.totalSize" in end_branch, "收尾应回读校验暂存镜像"
    assert end_branch.index("otaVerifyStaged(upload.totalSize") < end_branch.index(
        "otaUpdateOk = true"
    ), "只有回读校验通过后才能置成功标志"

    # 第四轮根因（2026-11-05，两台真机均 verify FAILED）：WebServer 在
    # WRITE 回调返回后才把本块计入 upload.totalSize，首块回调时
    # totalSize=0，原判据 totalSize-currentSize==0 命中的是第二块，存下
    # 文件第 2050 字节当模式字节，回读比对必然不符。须用自计字节数判首块。
    write_branch = upload[upload.index("UPLOAD_FILE_WRITE"):upload.index("UPLOAD_FILE_END")]
    assert "upload.totalSize - upload.currentSize == 0" not in upload, (
        "不得以 totalSize-currentSize 判首块（totalSize 在回调后才累加）"
    )
    assert "otaRxBytes == 0" in write_branch, "首块判定应基于自计的 otaRxBytes"
    assert write_branch.index("otaRxBytes == 0") < write_branch.index("otaRxBytes += m"), (
        "应先判首块再累加字节数"
    )
    start_branch = upload[upload.index("UPLOAD_FILE_START"):upload.index("UPLOAD_FILE_WRITE")]
    assert "otaRxBytes = 0" in start_branch, "每次上传开始应清零 otaRxBytes"
    assert "otaModeByteSaved && otaRxBytes == upload.totalSize" in end_branch, (
        "收尾须确认已存模式字节且自计字节数与 totalSize 一致"
    )

    print("own OTA handler (sliced write + wdt feed) contract: OK")


if __name__ == "__main__":
    main()
