#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OneNET AIoT (物联网融合平台) 调试与管理工具
支持 TLS 8883 (MQTTS) 与 TCP 1883，支持 OneJSON 物模型交互。
安全提示：请通过环境变量 ONENET_ACCESS_KEY 或命令行参数 --key 传入密钥，严禁在代码中硬编码敏感信息。
"""

import sys
import os
import time
import json
import base64
import hmac
import ssl
import argparse
from urllib.parse import quote
import urllib.request
import urllib.error

# 从环境变量读取，避免硬编码泄露
ENV_PID = os.environ.get("ONENET_PID", "")
ENV_ACCESS_KEY = os.environ.get("ONENET_ACCESS_KEY", "")

def calc_token(res, key_b64, et=None, method="sha256", version="2018-10-31"):
    """计算 OneNET 安全鉴权 Token (依据官方文档 1486)"""
    if not key_b64:
        raise ValueError("AccessKey 不能为空，请通过 --key 或环境变量 ONENET_ACCESS_KEY 传入。")
    if et is None:
        # 默认 10 年有效期
        et = str(int(time.time()) + 3600 * 24 * 365 * 10)
    else:
        et = str(et)
    
    org_string = f"{et}\n{method}\n{res}\n{version}"
    raw_key = base64.b64decode(key_b64)
    
    if method == "sha256":
        digestmod = "sha256"
    elif method == "sha1":
        digestmod = "sha1"
    elif method == "md5":
        digestmod = "md5"
    else:
        raise ValueError(f"Unsupported method: {method}")
        
    sign_bytes = hmac.new(raw_key, org_string.encode('utf-8'), digestmod).digest()
    sign_b64 = base64.b64encode(sign_bytes).decode('utf-8')
    
    res_encoded = quote(res, safe='')
    sign_encoded = quote(sign_b64, safe='')
    
    return f"version={version}&res={res_encoded}&et={et}&method={method}&sign={sign_encoded}"

def calc_api_auth(res, key_b64, version="2022-05-01", method="sha1"):
    """计算 OneNET 开放平台 HTTP API Header 的 authorization"""
    if not key_b64:
        raise ValueError("AccessKey 不能为空，请通过 --key 或环境变量 ONENET_ACCESS_KEY 传入。")
    et = str(int(time.time()) + 3600)
    org = f"{et}\n{method}\n{res}\n{version}"
    key = base64.b64decode(key_b64)
    sign_b = hmac.new(key=key, msg=org.encode('utf-8'), digestmod=method).digest()
    sign = quote(base64.b64encode(sign_b).decode(), safe='')
    res_enc = quote(res, safe='')
    return f"version={version}&res={res_enc}&et={et}&method={method}&sign={sign}"

def api_request(method, endpoint, params=None, body_dict=None, pid="", key=""):
    if not pid or not key:
        return {"code": -1, "msg": "缺少产品 ID 或 AccessKey，请通过 --pid / --key 传入。"}
    res_path = f"products/{pid}"
    auth = calc_api_auth(res_path, key)
    url = f"https://iot-api.heclouds.com{endpoint}"
    if params:
        url += "?" + urllib.parse.urlencode(params)
    
    data = None
    headers = {
        "authorization": auth,
        "User-Agent": "Mozilla/5.0"
    }
    if body_dict is not None:
        data = json.dumps(body_dict).encode('utf-8')
        headers["Content-Type"] = "application/json"
        
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req) as resp:
            return json.loads(resp.read().decode('utf-8'))
    except urllib.error.HTTPError as e:
        return {"code": -1, "http_err": e.code, "msg": e.read().decode('utf-8')}
    except Exception as e:
        return {"code": -1, "err": str(e)}

def cmd_token(args):
    print("=" * 60)
    print("OneNET MQTT 鉴权 Token 计算器")
    print("=" * 60)
    print(f"产品 ID (PID)      : {args.pid}")
    if args.dev:
        res = f"products/{args.pid}/devices/{args.dev}"
        print(f"模式              : 设备级鉴权 (一机一密)")
        print(f"设备名称 (devName): {args.dev}")
    else:
        res = f"products/{args.pid}"
        print(f"模式              : 产品级鉴权 (一型一密，产品下所有设备共用)")
    
    print(f"资源路径 (res)    : {res}")
    print(f"签名算法 (method) : {args.method}")
    token = calc_token(res, args.key, et=args.et, method=args.method)
    print("\n生成的 Token (用于 MQTT 连接的 Password):")
    print("-" * 60)
    print(token)
    print("-" * 60)
    print("\nMQTT 连接三要素建议：")
    print(f"  TLS Broker   : {args.pid}.mqttstls.acc.cmcconenet.cn:8883")
    print(f"  Client ID    : {args.dev if args.dev else '<你的设备名>'}")
    print(f"  Username     : {args.pid}")
    print(f"  Password     : {token}")

def cmd_list(args):
    resp = api_request("GET", "/device/list", params={"product_id": args.pid, "page": 1, "limit": 20}, pid=args.pid, key=args.key)
    print("=" * 60)
    print(f"产品 {args.pid} 下的设备列表:")
    print("=" * 60)
    if resp.get("code") == 0:
        devs = resp.get("data", {}).get("list", [])
        if not devs:
            print("当前产品下暂无设备。可使用 'python tools/onenet_tool.py create --name <dev_name>' 创建。")
        else:
            for i, d in enumerate(devs, 1):
                st_map = {0: "离线", 1: "在线", 2: "未激活"}
                st = st_map.get(d.get("status"), str(d.get("status")))
                print(f"[{i}] 设备名称: {d.get('name')}")
                print(f"    状态: {st} | 设备ID: {d.get('did')}")
                print(f"    创建时间: {d.get('create_time')} | 最近上线: {d.get('last_time')}")
                print("-" * 40)
    else:
        print("查询失败:", resp)

def cmd_create(args):
    body = {
        "product_id": args.pid,
        "device_name": args.name,
        "desc": args.desc or "YKYC-001 智能插座"
    }
    resp = api_request("POST", "/device/create", body_dict=body, pid=args.pid, key=args.key)
    if resp.get("code") == 0:
        data = resp.get("data", {})
        print(f"✅ 设备 '{args.name}' 创建成功！")
        print(f"   DID    : {data.get('did')}")
        print(f"   SecKey : {data.get('sec_key')}")
    else:
        print("❌ 创建失败:", resp)

def cmd_set(args):
    val = (args.state.lower() in ("1", "on", "true"))
    body = {
        "product_id": args.pid,
        "device_name": args.dev,
        "params": {
            "switch1": val,
            "total_switch": val
        }
    }
    print(f"正在向云端下发控制指令: 设备={args.dev}, switch1={val}...")
    resp = api_request("POST", "/thingmodel/set-device-property", body_dict=body, pid=args.pid, key=args.key)
    if resp.get("code") == 0:
        print(f"✅ 下发成功: {resp.get('data')}")
    else:
        print(f"❌ 下发失败: {resp}")

def cmd_test_mqtt(args):
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        print("提示: 需要安装 paho-mqtt 库才能运行 MQTT 模拟测试。")
        print("可使用: uv run --with paho-mqtt python tools/onenet_tool.py test-mqtt ...")
        return
        
    dev_name = args.dev
    res = f"products/{args.pid}/devices/{dev_name}"
    token = calc_token(res, args.key, method="sha256")
    
    use_tls = args.tls
    if use_tls:
        host = f"{args.pid}.mqttstls.acc.cmcconenet.cn"
        port = 8883
    else:
        host = f"{args.pid}.mqtts.acc.cmcconenet.cn"
        port = 1883
    
    print("=" * 60)
    print(f"正在启动 OneNET MQTT 模拟设备连接 (模式: {'TLS 8883' if use_tls else 'TCP 1883'})...")
    print(f"Host     : {host}:{port}")
    print(f"ClientID : {dev_name}")
    print(f"Username : {args.pid}")
    print("=" * 60)
    
    set_topic = f"$sys/{args.pid}/{dev_name}/thing/property/set"
    set_reply = f"$sys/{args.pid}/{dev_name}/thing/property/set_reply"
    post_topic = f"$sys/{args.pid}/{dev_name}/thing/property/post"
    post_reply = f"$sys/{args.pid}/{dev_name}/thing/property/post/reply"
    
    def on_connect(client, userdata, flags, rc, properties=None):
        print(f"✅ 连接成功 (rc={rc})！")
        client.subscribe(set_topic)
        client.subscribe(post_reply)
        print(f"已订阅控制主题: {set_topic}")
        print(f"已订阅上报确认: {post_reply}")
        
        # 上报一次开关状态
        report = {
            "id": "1001",
            "version": "1.0",
            "params": {
                "switch1": {"value": True},
                "total_switch": {"value": True}
            }
        }
        print(f"\n[上报] 正在发布开关状态到 {post_topic}: {json.dumps(report)}")
        client.publish(post_topic, json.dumps(report))
        
    def on_message(client, userdata, msg):
        payload_str = msg.payload.decode('utf-8')
        print(f"\n[接收] 主题: {msg.topic}")
        print(f"       内容: {payload_str}")
        if msg.topic == set_topic:
            try:
                data = json.loads(payload_str)
                msg_id = data.get("id", "1")
                reply = {"id": msg_id, "code": 200, "msg": "success"}
                client.publish(set_reply, json.dumps(reply))
                print(f"[回复] 已向 {set_reply} 回复确认: {json.dumps(reply)}")
            except Exception as e:
                print("解析回复异常:", e)
                
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=dev_name)
    client.username_pw_set(username=args.pid, password=token)
    if use_tls:
        # 与固件同一立场：用随仓的 OneNET 官方根证书做真校验（onenet_root_ca.pem 与固件内置证书同源）
        ca_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "onenet_root_ca.pem")
        client.tls_set(ca_certs=ca_path, cert_reqs=ssl.CERT_REQUIRED)
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        client.connect(host, port, 60)
        client.loop_start()
        print("监听中 (按 Ctrl+C 退出)...")
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n已退出监听。")
    finally:
        client.loop_stop()
        client.disconnect()

def main():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--pid", default=ENV_PID or "MiXfbNnY6r", help="产品 ID (PID)，默认: MiXfbNnY6r")
    common.add_argument("--key", default=ENV_ACCESS_KEY, help="产品 AccessKey，或设置环境变量 ONENET_ACCESS_KEY")

    parser = argparse.ArgumentParser(description="OneNET AIoT (物联网融合平台) 调试与管理工具", parents=[common])
    subparsers = parser.add_subparsers(dest="cmd")
    
    # 1. token
    p_token = subparsers.add_parser("token", help="计算鉴权 Token", parents=[common])
    p_token.add_argument("--dev", default=None, help="设备名称 (若留空则生成产品级Token)")
    p_token.add_argument("--method", default="sha256", choices=["sha256", "sha1", "md5"], help="签名算法 (默认 sha256)")
    p_token.add_argument("--et", default=None, help="过期 Unix 时间戳 (秒)")
    
    # 2. list
    subparsers.add_parser("list", help="列出产品下所有设备", parents=[common])
    
    # 3. create
    p_create = subparsers.add_parser("create", help="创建新设备", parents=[common])
    p_create.add_argument("--name", required=True, help="设备名称 (例如: ykyc001_dev1)")
    p_create.add_argument("--desc", default="YKYC-001 智能插座", help="设备描述")
    
    # 4. set
    p_set = subparsers.add_parser("set", help="下发云端开关控制", parents=[common])
    p_set.add_argument("--dev", required=True, help="设备名称 (例如: ykyc001_dev1)")
    p_set.add_argument("--state", required=True, choices=["on", "off", "1", "0"], help="开关状态: on 或 off")
    
    # 5. test-mqtt
    p_mqtt = subparsers.add_parser("test-mqtt", help="模拟设备连接 MQTT 并监听控制", parents=[common])
    p_mqtt.add_argument("--dev", required=True, help="设备名称 (例如: ykyc001_dev1)")
    p_mqtt.add_argument("--tls", action="store_true", help="启用 TLS 8883 加密连接 (默认: 明文 1883)")
    
    args = parser.parse_args()
    if not args.cmd:
        parser.print_help()
        return

    if not args.key:
        print("错误: 缺少 AccessKey，请通过 --key 参数或环境变量 ONENET_ACCESS_KEY 提供。")
        return
        
    if args.cmd == "token":
        cmd_token(args)
    elif args.cmd == "list":
        cmd_list(args)
    elif args.cmd == "create":
        cmd_create(args)
    elif args.cmd == "set":
        cmd_set(args)
    elif args.cmd == "test-mqtt":
        cmd_test_mqtt(args)

if __name__ == "__main__":
    main()
