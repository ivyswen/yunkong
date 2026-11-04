# OneNET AIoT RESTful HTTP API 使用指南

本文档介绍如何通过中国移动 **OneNET AIoT（物联网融合平台）** 的官方 RESTful HTTP 接口，对 **YKYC-001 智能插座** 进行云端设备管理（新增/查询设备）与远程开关控制（物模型属性下发与查询）。

---

## 一、 接口基础说明

- **接口基础域名 (Base URL)**：`https://iot-api.heclouds.com`
- **数据协议**：JSON（`Content-Type: application/json`）
- **通用产品 ID (PID)**：`MiXfbNnY6r`（插座对应 PID，也可替换为你自己的产品 ID）
- **安全规范**：本仓库为公开开源项目，**严禁在代码、提交记录或文档中硬编码敏感的 AccessKey**。建议通过环境变量 `ONENET_ACCESS_KEY` 传入密钥。

---

## 二、 鉴权认证 Header（Authorization）

OneNET 开放平台所有 HTTP API 均要求在 HTTP Header 中携带鉴权参数：

```http
authorization: version=2022-05-01&res=products%2F<PID>&et=<过期时间戳>&method=sha1&sign=<签名>
```

### 1. Header 字段说明

| 字段 | 说明 | 取值示例 |
|---|---|---|
| `version` | API 鉴权版本（HTTP API 固定使用该版本） | `2022-05-01` |
| `res` | 授权资源路径（需 URL 编码） | `products/MiXfbNnY6r` $\to$ `products%2FMiXfbNnY6r` |
| `et` | Token 过期截止时间（Unix 时间戳，单位秒） | 例如当前时间 + 3600 秒：`1730520000` |
| `method` | 签名算法，支持 `sha1`、`sha256`、`md5` | 推荐 `sha1` |
| `sign` | 基于 AccessKey 对明文字符串进行 HMAC 签名的结果（需 Base64 编码并 URL 编码） | 计算得出 |

### 2. 签名计算公式

1. **待签名源字符串**：
   $$\text{StringForSignature} = et + \text{"\textbackslash n"} + method + \text{"\textbackslash n"} + res + \text{"\textbackslash n"} + version$$
2. **签名计算**：
   $$\text{Sign} = \text{Base64}\Big(\text{HMAC\_SHA1}\big(\text{Base64\_Decode}(AccessKey), \text{StringForSignature}\big)\Big)$$
3. **URL 编码**：对 $res$ 和 $Sign$ 进行标准 URL 编码（保留字符转义，如 `/` $\to$ `%2F`，`+` $\to$ `%2B`，`=` $\to$ `%3D`）。

---

## 三、 设备管理接口

### 1. 创建 / 添加新设备

向产品下注册一台新插座，获取该设备的专属设备 ID (`did`) 和设备密钥 (`sec_key`)。

- **请求方式**：`POST`
- **接口路径**：`/device/create`
- **请求头**：
  ```http
  Content-Type: application/json
  authorization: <计算出的鉴权串>
  ```
- **请求体 (Body)**：
  ```json
  {
    "product_id": "MiXfbNnY6r",
    "device_name": "ykyc001_dev2",
    "desc": "客厅空调插座"
  }
  ```
- **响应示例**：
  ```json
  {
    "code": 0,
    "msg": "succ",
    "data": {
      "did": 1234567890,
      "device_name": "ykyc001_dev2",
      "sec_key": "YOUR_DEVICE_SEC_KEY..."
    }
  }
  ```

---

### 2. 查询设备列表与在线状态

批量查询产品下已注册的设备及其连接状态。

- **请求方式**：`GET`
- **接口路径**：`/device/list?product_id=MiXfbNnY6r&page=1&limit=20`
- **请求头**：
  ```http
  authorization: <计算出的鉴权串>
  ```
- **响应示例**：
  ```json
  {
    "code": 0,
    "msg": "succ",
    "data": {
      "total": 1,
      "list": [
        {
          "did": 1234567890,
          "name": "ykyc001_dev1",
          "status": 1,
          "create_time": "2026-11-02T10:00:00.000+08:00",
          "last_time": "2026-11-02T11:00:00.000+08:00"
        }
      ]
    }
  }
  ```
  > `status` 状态码说明：`0` = 离线，`1` = 在线，`2` = 未激活。

---

## 四、 设备控制与物模型接口

YKYC-001 采用中国移动官方 **OneJson** 物模型协议，支持 `switch1` 与 `total_switch` 属性控制。

### 1. 远程控制开关（设置设备属性）

云端实时向在线插座下发开关指令。

- **请求方式**：`POST`
- **接口路径**：`/thingmodel/set-device-property`
- **请求头**：
  ```http
  Content-Type: application/json
  authorization: <计算出的鉴权串>
  ```
- **请求体 (Body)**：
  ```json
  {
    "product_id": "MiXfbNnY6r",
    "device_name": "ykyc001_dev1",
    "params": {
      "switch1": true,
      "total_switch": true
    }
  }
  ```
  > - **开插座**：`"switch1": true`
  > - **关插座**：`"switch1": false`

- **成功响应**：
  ```json
  {
    "code": 0,
    "msg": "succ",
    "data": {
      "id": "1730518800"
    }
  }
  ```

---

### 2. 查询设备最新属性状态

获取云端记录的设备最新开关状态。

- **请求方式**：`GET`
- **接口路径**：`/thingmodel/query-device-property?product_id=MiXfbNnY6r&device_name=ykyc001_dev1`
- **请求头**：
  ```http
  authorization: <计算出的鉴权串>
  ```
- **响应示例**：
  ```json
  {
    "code": 0,
    "msg": "succ",
    "data": [
      {
        "identifier": "switch1",
        "value": true,
        "time": 1730518800000
      },
      {
        "identifier": "total_switch",
        "value": true,
        "time": 1730518800000
      }
    ]
  }
  ```

---

## 五、 代码示例与快捷工具

### 1. 仓库内置 CLI 工具（推荐，一键执行）

仓库在 `tools/onenet_tool.py` 中已经封装了完整的签名计算和 API 请求，支持通过命令行或环境变量安全使用：

```bash
# 环境变量设置 AccessKey（避免明文留在历史记录中）
export ONENET_ACCESS_KEY="<你的AccessKey>"

# 1. 创建新设备（支持 --mac 自动推导 ykyc001_xxxxxx，也可直接指定 --name）
python tools/onenet_tool.py create --mac 1a2b3c --desc "二号插座"
# 或指定名称：python tools/onenet_tool.py create --name ykyc001_dev2 --desc "二号插座"

# 2. 查询设备列表与在线状态
python tools/onenet_tool.py list

# 3. 控制设备开关 (开 / 关)
python tools/onenet_tool.py set --dev ykyc001_dev1 --state on
python tools/onenet_tool.py set --dev ykyc001_dev1 --state off
```

---

### 2. 独立 Python 调用脚本（零依赖，标准库）

如需将 API 嵌入到您自己的独立后台程序，可直接复制以下函数：

```python
import time
import json
import base64
import hmac
import urllib.request
from urllib.parse import quote

PID = "MiXfbNnY6r"
ACCESS_KEY = "你的AccessKey"  # 建议从环境变量 os.environ.get("ONENET_ACCESS_KEY") 读取

def get_auth_header(pid: str, key_b64: str) -> str:
    """计算 OneNET API 鉴权 Header"""
    et = str(int(time.time()) + 3600)  # 1小时有效
    res = f"products/{pid}"
    method = "sha1"
    version = "2022-05-01"
    
    org_string = f"{et}\n{method}\n{res}\n{version}"
    raw_key = base64.b64decode(key_b64)
    sign = base64.b64encode(hmac.new(raw_key, org_string.encode('utf-8'), method).digest()).decode('utf-8')
    
    return f"version={version}&res={quote(res, safe='')}&et={et}&method={method}&sign={quote(sign, safe='')}"

def api_post(endpoint: str, body: dict) -> dict:
    url = f"https://iot-api.heclouds.com{endpoint}"
    headers = {
        "Content-Type": "application/json",
        "authorization": get_auth_header(PID, ACCESS_KEY)
    }
    data = json.dumps(body).encode('utf-8')
    req = urllib.request.Request(url, data=data, headers=headers, method="POST")
    with urllib.request.urlopen(req) as resp:
        return json.loads(resp.read().decode('utf-8'))

# 示例 1: 创建新设备
def create_device(dev_name: str, desc: str = "云控插座"):
    return api_post("/device/create", {
        "product_id": PID,
        "device_name": dev_name,
        "desc": desc
    })

# 示例 2: 下发控制开关 (state=True 开启, state=False 关闭)
def control_switch(dev_name: str, state: bool):
    return api_post("/thingmodel/set-device-property", {
        "product_id": PID,
        "device_name": dev_name,
        "params": {
            "switch1": state,
            "total_switch": state
        }
    })

if __name__ == "__main__":
    # 测试控制开
    res = control_switch("ykyc001_dev1", True)
    print("下发结果:", res)
```

---

## 六、 常见状态码与故障排查

| 状态码 (`code`) | 错误信息 (`msg`) | 原因排查与解决办法 |
|---|---|---|
| `0` | `succ` | 请求成功。 |
| `10411` | `属性设置失败:dev not subscribed` | **设备未在线或未订阅控制主题**。请检查插座是否通电联网、是否成功连接到 MQTTS 8883 并订阅了 `$sys/{pid}/{devName}/thing/property/set`。 |
| `10403` | `auth fail` / `signature error` | 鉴权失败。检查 AccessKey 是否正确、待签名字符串中各换行符与参数顺序是否严格一致。 |
| `20003` | `device not found` | 设备不存在。请确认填写的 `device_name` 是否已在当前 `product_id` 下完成创建。 |
| `10002` | `param error` | 请求参数格式错误。检查 JSON 字段命名（如 `product_id`、`device_name`、`params`）是否拼写正确。 |
