import argparse
import json
import socket
import struct
import threading
import time
from collections import deque
import urllib.parse
import urllib.request

import cv2
import numpy as np
from flask import Flask, Response, jsonify, request


MAGIC_FRAME = b"FRAM"
FMT_RGB565 = 0
FMT_JPEG   = 1

app = Flask(__name__)

state_lock = threading.Lock()

latest_jpeg = None
latest_seq = 0
latest_info = {
    "width": 0,
    "height": 0,
    "fps": 0.0,
    "connected": False,
    "last_ts": 0.0,
    "door_open": False,
    "face_state": "IDLE",
    "face_id": -1,
    "face_score_m": -1,
    "gesture_state": "IDLE",
    "gesture_palm_m": -1,
    "unlock_left_ms": 0,
    "alarm_state": "OFF",
    "board_ip": "",
    "led_mode": "auto",
    "alarm_enable": True,
    "buzzer_enable": False,
}

EVENTS_MAX = 120
latest_events = deque(maxlen=EVENTS_MAX)
event_seq = 0


def push_event_unlocked(level, kind, message):
    global event_seq
    event_seq += 1
    latest_events.append({
        "id": event_seq,
        "ts": time.strftime("%H:%M:%S", time.localtime()),
        "level": level,
        "kind": kind,
        "message": message,
    })

def recvall(sock, n):
    data = bytearray()
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            return None
        data.extend(chunk)
    return bytes(data)


def rgb565_to_bgr(frame, width, height):
    arr = np.frombuffer(frame, dtype=np.uint16).reshape((height, width))
    r = ((arr >> 11) & 0x1F) << 3
    g = ((arr >> 5) & 0x3F) << 2
    b = (arr & 0x1F) << 3
    bgr = np.dstack((b, g, r)).astype(np.uint8)
    return bgr


def update_frame(jpeg, width, height, fps):
    global latest_jpeg, latest_seq
    with state_lock:
        was_connected = bool(latest_info.get("connected", False))
        latest_jpeg = jpeg
        latest_seq += 1
        latest_info["width"] = width
        latest_info["height"] = height
        latest_info["fps"] = fps
        latest_info["connected"] = True
        latest_info["last_ts"] = time.time()
        if not was_connected:
            push_event_unlocked("ok", "stream", "设备上线，开始接收图传")


def set_connected(value):
    with state_lock:
        prev = bool(latest_info.get("connected", False))
        latest_info["connected"] = value
        if prev != bool(value):
            if value:
                push_event_unlocked("ok", "stream", "设备在线")
            else:
                push_event_unlocked("warn", "stream", "设备离线或图传中断")


def stream_server(data_port):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", data_port))
    server.listen(1)
    print(f"Stream server listening on 0.0.0.0:{data_port}")

    while True:
        conn, addr = server.accept()
        print(f"Board connected: {addr[0]}:{addr[1]}")
        with state_lock:
            latest_info["board_ip"] = addr[0]
        conn.settimeout(5.0)
        try:
            frame_count = 0
            t0 = time.time()
            logged_first = False

            while True:
                header = recvall(conn, 16)
                if not header:
                    print("No frame header received (timeout or disconnect)")
                    break

                magic, width, height, fmt, _reserved, frame_len = struct.unpack("<4sHHHHI", header)
                if magic != MAGIC_FRAME:
                    print(f"Bad frame magic: {magic}")
                    break
                
                frame = recvall(conn, frame_len)
                if not frame:
                    print("Incomplete frame body")
                    break

                if fmt == FMT_RGB565:
                    try:
                        img = rgb565_to_bgr(frame, width, height)
                        # Encode/Re-encode to JPEG for Web Stream
                        # Board is now sending JPEG, so we might receive RGB565 less often
                        ok, encoded = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 95])
                        if not ok:
                            print("JPEG encode failed")
                            continue
                    except Exception as exc:
                        print(f"Frame decode error: {exc}")
                        break
                elif fmt == FMT_JPEG:
                    # Received JPEG from board
                    try:
                        # Verify JPEG is valid by trying to decode it (optional, can be disabled for performance)
                        # frame_arr = np.frombuffer(frame, dtype=np.uint8)
                        # debug_img = cv2.imdecode(frame_arr, cv2.IMREAD_COLOR)
                        # if debug_img is None:
                        #     print("Warning: Received invalid JPEG data")
                        pass 
                    except:
                        pass
                    
                    # Use the JPEG data directly
                    update_frame(frame, width, height, 0.0) # FPS calc below will overwrite
                else:
                    print(f"Unsupported format: {fmt}")
                    break

                if fmt == FMT_RGB565:
                   # FPS calculation and update for RGB565 path
                   pass # handled in update_frame below
                
                frame_count += 1
                dt = time.time() - t0
                fps = frame_count / dt if dt > 0 else 0.0
                
                if fmt == FMT_RGB565:
                    update_frame(encoded.tobytes(), width, height, fps)
                else:
                    # Update FPS for JPEG path
                    update_frame(frame, width, height, fps)

                if (frame_count == 1) or (frame_count % 60 == 0):
                    print(f"Frames: {frame_count}, {width}x{height}, fps={fps:.1f}")

        except Exception as exc:
            print(f"Stream error: {exc}")
        finally:
            set_connected(False)
            conn.close()
            print("Board disconnected")


@app.route("/")
def index():
    return Response(HTML_PAGE, content_type="text/html; charset=utf-8")


@app.route("/stream.mjpg")
def stream_mjpeg():
    def gen():
        print("MJPEG client connected")
        last_seq = 0
        while True:
            with state_lock:
                seq = latest_seq
                jpeg = latest_jpeg
            if jpeg is None or seq == last_seq:
                time.sleep(0.02)
                continue
            last_seq = seq
            header = (
                b"--frame\r\n"
                b"Content-Type: image/jpeg\r\n"
                + f"Content-Length: {len(jpeg)}\r\n\r\n".encode("ascii")
            )
            yield header + jpeg + b"\r\n"

    return Response(gen(), mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/api/status")
def api_status():
    with state_lock:
        info = dict(latest_info)
    if info["last_ts"] > 0:
        info["age_ms"] = int((time.time() - info["last_ts"]) * 1000)
    else:
        info["age_ms"] = -1
    return jsonify(info)


@app.route("/api/events")
def api_events():
    try:
        limit = int(request.args.get("limit", "30"))
    except Exception:
        limit = 30
    if limit < 1:
        limit = 1
    if limit > EVENTS_MAX:
        limit = EVENTS_MAX
    with state_lock:
        events = list(latest_events)[-limit:]
    return jsonify({"events": events, "total": len(events)})


@app.route("/api/control", methods=["GET", "POST"])
def api_control():
    if request.method == "GET":
        with state_lock:
            info = {
                "led_mode": latest_info.get("led_mode", "auto"),
                "alarm_enable": bool(latest_info.get("alarm_enable", True)),
                "buzzer_enable": bool(latest_info.get("buzzer_enable", False)),
                "board_ip": latest_info.get("board_ip", ""),
            }
        return jsonify({"ok": True, "control": info})

    payload = request.get_json(silent=True) or {}
    led_mode = str(payload.get("led_mode", "auto")).lower()
    if led_mode not in ("auto", "on", "off"):
        led_mode = "auto"
    alarm_enable = bool(payload.get("alarm_enable", True))
    buzzer_enable = bool(payload.get("buzzer_enable", False))

    with state_lock:
        latest_info["led_mode"] = led_mode
        latest_info["alarm_enable"] = alarm_enable
        latest_info["buzzer_enable"] = buzzer_enable
        board_ip = latest_info.get("board_ip", "")
        push_event_unlocked("ok", "ctrl", f"控制更新 led={led_mode} alarm={'on' if alarm_enable else 'off'} buzzer={'on' if buzzer_enable else 'off'}")

    return jsonify({
        "ok": True,
        "board_ok": True,
        "board_msg": "queued",
        "control": {
            "led_mode": led_mode,
            "alarm_enable": alarm_enable,
            "buzzer_enable": buzzer_enable,
            "board_ip": board_ip,
        },
    })

@app.route("/api/control_pull")
def api_control_pull():
    with state_lock:
        led_mode = str(latest_info.get("led_mode", "auto"))
        alarm_enable = 1 if bool(latest_info.get("alarm_enable", True)) else 0
        buzzer_enable = 1 if bool(latest_info.get("buzzer_enable", False)) else 0
    body = f"led={led_mode}&alarm={alarm_enable}&buzzer={buzzer_enable}"
    return Response(body, content_type="text/plain; charset=utf-8")


@app.route("/api/runtime_push")
def api_runtime_push():
    with state_lock:
        prev_door_open = bool(latest_info.get("door_open", False))
        prev_alarm_state = str(latest_info.get("alarm_state", "OFF")).upper()
        prev_face_id = int(latest_info.get("face_id", -1))
        prev_face_score_m = int(latest_info.get("face_score_m", -1))
        prev_gesture_state = str(latest_info.get("gesture_state", "IDLE"))
        prev_palm_m = int(latest_info.get("gesture_palm_m", -1))

        if "door_open" in request.args:
            latest_info["door_open"] = (request.args.get("door_open") == "1")
        if "face_state" in request.args:
            latest_info["face_state"] = request.args.get("face_state", "IDLE")
        if "face_id" in request.args:
            try: latest_info["face_id"] = int(request.args.get("face_id", "-1"))
            except Exception: pass
        if "face_score_m" in request.args:
            try: latest_info["face_score_m"] = int(request.args.get("face_score_m", "-1"))
            except Exception: pass
        if "gesture_state" in request.args:
            latest_info["gesture_state"] = request.args.get("gesture_state", "IDLE")
        if "gesture_palm_m" in request.args:
            try: latest_info["gesture_palm_m"] = int(request.args.get("gesture_palm_m", "-1"))
            except Exception: pass
        if "unlock_left_ms" in request.args:
            try: latest_info["unlock_left_ms"] = int(request.args.get("unlock_left_ms", "0"))
            except Exception: pass
        if "alarm_state" in request.args:
            latest_info["alarm_state"] = request.args.get("alarm_state", "OFF")
        if "led_mode" in request.args:
            latest_info["led_mode"] = request.args.get("led_mode", "auto")
        if "alarm_enable" in request.args:
            latest_info["alarm_enable"] = (request.args.get("alarm_enable") == "1")
        if "buzzer_enable" in request.args:
            latest_info["buzzer_enable"] = (request.args.get("buzzer_enable") == "1")
        latest_info["last_ts"] = time.time()

        door_open = bool(latest_info.get("door_open", False))
        alarm_state = str(latest_info.get("alarm_state", "OFF")).upper()
        face_id = int(latest_info.get("face_id", -1))
        face_score_m = int(latest_info.get("face_score_m", -1))
        gesture_state = str(latest_info.get("gesture_state", "IDLE"))
        palm_m = int(latest_info.get("gesture_palm_m", -1))

        if door_open != prev_door_open:
            push_event_unlocked("ok" if door_open else "warn", "door", "DOOR OPEN" if door_open else "DOOR CLOSED")

        if alarm_state != prev_alarm_state:
            push_event_unlocked("danger" if alarm_state == "ON" else "ok", "alarm", "FACE FAIL ALARM ON" if alarm_state == "ON" else "ALARM CLEARED")

        if face_id >= 0 and (face_id != prev_face_id or face_score_m != prev_face_score_m):
            score = f"{face_score_m / 1000:.3f}" if face_score_m >= 0 else "--"
            push_event_unlocked("ok", "face", f"FACE PASS ID={face_id} score={score}")

        if gesture_state != prev_gesture_state or palm_m != prev_palm_m:
            if palm_m >= 0:
                push_event_unlocked("ok", "gesture", f"{gesture_state} palm={palm_m / 1000:.3f}")
            elif gesture_state != prev_gesture_state:
                push_event_unlocked("ok", "gesture", f"{gesture_state}")
    return Response("ok", content_type="text/plain; charset=utf-8")



HTML_PAGE = r"""
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>智能门禁监控台</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=ZCOOL+KuaiLe&family=Noto+Sans+SC:wght@300;400;500;700&display=swap');

    :root {
      --bg: #fde9d9;
      --bg-2: #fff5ed;
      --ink: #5b3b2b;
      --muted: #8a6a58;
      --accent: #ff9f6a;
      --accent-2: #ffb6c7;
      --cream: #fffaf4;
      --shadow-1: #e3bfae;
      --shadow-2: #fff6ee;
      --radius-lg: 28px;
      --radius-md: 20px;
      --radius-sm: 14px;
    }

    * { box-sizing: border-box; }
    html, body {
      margin: 0;
      padding: 0;
      width: 100%;
      overflow-x: hidden;
      color: var(--ink);
      font-family: "Noto Sans SC", sans-serif;
      background:
        radial-gradient(900px 460px at 12% 10%, rgba(255, 182, 199, 0.35), transparent 60%),
        radial-gradient(900px 500px at 90% 0%, rgba(255, 159, 106, 0.28), transparent 60%),
        linear-gradient(180deg, var(--bg), var(--bg-2));
    }

    .wrap {
      width: min(1100px, calc(100% - 48px));
      margin: 0 auto;
      padding: 28px 0 48px;
    }

    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      margin-bottom: 22px;
    }

    .brand {
      font-family: "ZCOOL KuaiLe", cursive;
      font-size: clamp(24px, 3.6vw, 34px);
      letter-spacing: 0.06em;
    }

    .status-pill {
      display: inline-flex;
      align-items: center;
      gap: 10px;
      padding: 10px 16px;
      border-radius: 999px;
      background: var(--cream);
      box-shadow: 6px 6px 16px var(--shadow-1), -6px -6px 16px var(--shadow-2);
      font-size: 12px;
      letter-spacing: 0.16em;
      text-transform: uppercase;
      color: var(--muted);
    }

    .dot {
      width: 10px;
      height: 10px;
      border-radius: 50%;
      background: #ef4444;
      box-shadow: 0 0 8px rgba(239, 68, 68, 0.6);
      transition: all 200ms ease;
    }

    .dot.live {
      background: #22c55e;
      box-shadow: 0 0 10px rgba(34, 197, 94, 0.8);
    }

    .layout {
      display: grid;
      grid-template-columns: minmax(0, 1.5fr) minmax(220px, 320px);
      gap: 20px;
    }

    .panel {
      background: var(--cream);
      border-radius: var(--radius-lg);
      padding: 18px;
      box-shadow: 14px 14px 28px var(--shadow-1), -14px -14px 28px var(--shadow-2);
    }

    .viewer-frame {
      position: relative;
      border-radius: var(--radius-md);
      overflow: hidden;
      background: #f3d7c6;
      box-shadow: inset 6px 6px 12px rgba(235, 200, 180, 0.55), inset -6px -6px 12px rgba(255,255,255,0.8);
    }

    .viewer-frame img {
      display: block;
      width: 100%;
      height: auto;
      object-fit: contain;
      max-width: 640px;
      margin: 0 auto;
    }

    .viewer-overlay {
      position: absolute;
      inset: 0;
      display: flex;
      align-items: center;
      justify-content: center;
      font-weight: 700;
      letter-spacing: 0.2em;
      color: rgba(91, 59, 43, 0.8);
      background: rgba(255, 250, 244, 0.75);
      opacity: 0;
      transition: opacity 200ms ease;
    }

    .viewer.is-offline .viewer-overlay {
      opacity: 1;
    }

    .actions {
      display: flex;
      gap: 12px;
      margin-top: 14px;
      flex-wrap: wrap;
    }

    .btn {
      border: none;
      border-radius: 999px;
      background: linear-gradient(135deg, var(--accent), var(--accent-2));
      color: #5a2a15;
      font-weight: 700;
      padding: 10px 16px;
      cursor: pointer;
      letter-spacing: 0.06em;
    }

    .btn.secondary {
      background: #ffe0cf;
      color: #a4583d;
      text-decoration: none;
      display: inline-flex;
      align-items: center;
    }

    .stats {
      display: grid;
      gap: 12px;
    }

    .stat {
      background: #fff6ee;
      border-radius: var(--radius-sm);
      padding: 12px 14px;
      box-shadow: inset 6px 6px 12px rgba(235, 200, 180, 0.5), inset -6px -6px 12px rgba(255,255,255,0.8);
    }

    .stat label {
      display: block;
      font-size: 12px;
      color: var(--muted);
      letter-spacing: 0.14em;
      text-transform: uppercase;
      margin-bottom: 6px;
    }

    .stat strong {
      font-family: "ZCOOL KuaiLe", cursive;
      font-size: 18px;
    }

    .note {
      margin-top: 10px;
      color: var(--muted);
      font-size: 12px;
    }

    .divider {
      margin: 8px 0 2px;
      height: 1px;
      background: linear-gradient(90deg, transparent, rgba(138, 106, 88, 0.25), transparent);
    }

    .state-grid {
      display: grid;
      gap: 12px;
      margin-top: 12px;
    }

    .state-badge {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      min-height: 34px;
      min-width: 96px;
      padding: 6px 12px;
      border-radius: 999px;
      font-size: 13px;
      font-weight: 700;
      letter-spacing: 0.06em;
      background: #ffe8da;
      color: #7d4a32;
    }

    .state-badge.ok {
      background: #dcfce7;
      color: #166534;
    }

    .state-badge.warn {
      background: #fef3c7;
      color: #92400e;
    }

    .state-badge.danger {
      background: #fee2e2;
      color: #991b1b;
    }

    .events-panel {
      margin-top: 18px;
    }

    .events-head {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 10px;
      gap: 12px;
    }

    .events-title {
      font-family: "ZCOOL KuaiLe", cursive;
      font-size: 20px;
      letter-spacing: 0.04em;
    }

    .events-list {
      display: grid;
      gap: 10px;
      max-height: 280px;
      overflow: auto;
      padding-right: 4px;
    }

    .event-item {
      display: grid;
      grid-template-columns: 72px 78px 1fr;
      align-items: center;
      gap: 10px;
      padding: 10px 12px;
      border-radius: var(--radius-sm);
      background: #fff6ee;
      box-shadow: inset 4px 4px 8px rgba(235, 200, 180, 0.45), inset -4px -4px 8px rgba(255,255,255,0.8);
      font-size: 13px;
    }

    .event-time {
      color: var(--muted);
      font-variant-numeric: tabular-nums;
    }

    .event-tag {
      display: inline-flex;
      justify-content: center;
      align-items: center;
      border-radius: 999px;
      padding: 4px 8px;
      font-size: 11px;
      font-weight: 700;
      letter-spacing: 0.06em;
      background: #ffe8da;
      color: #7d4a32;
      text-transform: uppercase;
    }

    .event-tag.ok { background: #dcfce7; color: #166534; }
    .event-tag.warn { background: #fef3c7; color: #92400e; }
    .event-tag.danger { background: #fee2e2; color: #991b1b; }

    .event-msg {
      color: var(--ink);
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }

    .ctrl-panel {
      margin-top: 18px;
    }

    .ctrl-grid {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 12px;
    }

    .ctrl-card {
      background: #fff6ee;
      border-radius: var(--radius-sm);
      padding: 12px;
      box-shadow: inset 4px 4px 8px rgba(235, 200, 180, 0.45), inset -4px -4px 8px rgba(255,255,255,0.8);
    }

    .ctrl-title {
      font-size: 12px;
      color: var(--muted);
      letter-spacing: 0.12em;
      text-transform: uppercase;
      margin-bottom: 8px;
      display: block;
    }

    .ctrl-row {
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
    }

    .chip {
      border: none;
      border-radius: 999px;
      min-height: 34px;
      padding: 6px 12px;
      background: #ffe8da;
      color: #7d4a32;
      font-weight: 700;
      cursor: pointer;
    }

    .chip.active {
      background: #dcfce7;
      color: #166534;
    }

    .ctrl-note {
      margin-top: 8px;
      font-size: 12px;
      color: var(--muted);
    }

    @media (max-width: 900px) {
      .layout { grid-template-columns: 1fr; }
      .ctrl-grid { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <header>
      <div class="brand">智能门禁监控台</div>
      <div class="status-pill" aria-live="polite">
        <span class="dot" id="statusDot"></span>
        <span id="statusText">离线</span>
      </div>
    </header>

    <div class="layout">
      <section class="panel viewer" id="viewer">
        <div class="viewer-frame">
          <img id="streamImg" src="/stream.mjpg" alt="实时画面" />
          <div class="viewer-overlay">无信号</div>
        </div>
        <div class="actions">
          <button class="btn" id="reloadBtn" type="button">刷新画面</button>
          <a class="btn secondary" href="/stream.mjpg" target="_blank" rel="noopener">打开 MJPEG</a>
        </div>

    <div class="ctrl-panel">
      <div class="events-head">
        <div class="events-title">远程控制</div>
        <button class="btn secondary" id="syncCtrlBtn" type="button">同步状态</button>
      </div>
      <div class="ctrl-grid">
        <div class="ctrl-card">
          <span class="ctrl-title">绿色 LED</span>
          <div class="ctrl-row">
            <button class="chip" id="ledAutoBtn" type="button">自动</button>
            <button class="chip" id="ledOnBtn" type="button">开启</button>
            <button class="chip" id="ledOffBtn" type="button">关闭</button>
          </div>
        </div>
        <div class="ctrl-card">
          <span class="ctrl-title">报警功能</span>
          <div class="ctrl-row">
            <button class="chip" id="alarmOnBtn" type="button">开启</button>
            <button class="chip" id="alarmOffBtn" type="button">关闭</button>
          </div>
        </div>
        <div class="ctrl-card">
          <span class="ctrl-title">蜂鸣器功能</span>
          <div class="ctrl-row">
            <button class="chip" id="buzzerOnBtn" type="button">开启</button>
            <button class="chip" id="buzzerOffBtn" type="button">关闭</button>
          </div>
        </div>
      </div>
      <div class="ctrl-note" id="ctrlNote">控制通道未同步</div>
    </div>
      </section>

      <section class="panel">
        <div class="stats">
          <div class="stat">
            <label>分辨率</label>
            <strong id="statRes">--</strong>
          </div>
          <div class="stat">
            <label>帧率</label>
            <strong id="statFps">--</strong>
          </div>
          <div class="stat">
            <label>帧龄</label>
            <strong id="statAge">--</strong>
          </div>
          <div class="stat">
            <label>流模式</label>
            <strong id="statMode">Unknown</strong>
          </div>
        </div>
        <div class="divider"></div>
        <div class="state-grid">
          <div class="stat">
            <label>门锁状态</label>
            <strong><span class="state-badge" id="doorState">--</span></strong>
          </div>
          <div class="stat">
            <label>人脸识别</label>
            <strong id="faceState">--</strong>
          </div>
          <div class="stat">
            <label>手势识别</label>
            <strong id="gestureState">--</strong>
          </div>
          <div class="stat">
            <label>报警状态</label>
            <strong><span class="state-badge" id="alarmState">--</span></strong>
          </div>
        </div>
        <div class="note">提示：当前页面已预留门禁状态字段，后续可直接对接板端实时状态上报。</div>
      </section>
    </div>

    <section class="panel events-panel">
      <div class="events-head">
        <div class="events-title">事件记录</div>
        <button class="btn secondary" id="refreshEventsBtn" type="button">刷新记录</button>
      </div>
      <div class="events-list" id="eventsList">
        <div class="event-item">
          <span class="event-time">--:--:--</span>
          <span class="event-tag">INIT</span>
          <span class="event-msg">等待设备事件...</span>
        </div>
      </div>
    </section>
  </div>

  <script>
    const statusDot = document.getElementById("statusDot");
    const statusText = document.getElementById("statusText");
    const statRes = document.getElementById("statRes");
    const statFps = document.getElementById("statFps");
    const statAge = document.getElementById("statAge");
    const statMode = document.getElementById("statMode");
    const doorState = document.getElementById("doorState");
    const faceState = document.getElementById("faceState");
    const gestureState = document.getElementById("gestureState");
    const alarmState = document.getElementById("alarmState");
    const viewer = document.getElementById("viewer");
    const reloadBtn = document.getElementById("reloadBtn");
    const streamImg = document.getElementById("streamImg");
    const eventsList = document.getElementById("eventsList");
    const refreshEventsBtn = document.getElementById("refreshEventsBtn");
    const syncCtrlBtn = document.getElementById("syncCtrlBtn");
    const ledAutoBtn = document.getElementById("ledAutoBtn");
    const ledOnBtn = document.getElementById("ledOnBtn");
    const ledOffBtn = document.getElementById("ledOffBtn");
    const alarmOnBtn = document.getElementById("alarmOnBtn");
    const alarmOffBtn = document.getElementById("alarmOffBtn");
    const buzzerOnBtn = document.getElementById("buzzerOnBtn");
    const buzzerOffBtn = document.getElementById("buzzerOffBtn");
    const ctrlNote = document.getElementById("ctrlNote");
    let ctrlState = { led_mode: "auto", alarm_enable: true, buzzer_enable: false };

    function reloadStream() {
      streamImg.src = `/stream.mjpg?ts=${Date.now()}`;
    }

    reloadBtn.addEventListener("click", reloadStream);

    function renderEvents(events) {
      if (!Array.isArray(events) || events.length === 0) {
        eventsList.innerHTML = `<div class="event-item"><span class="event-time">--:--:--</span><span class="event-tag">EMPTY</span><span class="event-msg">暂无事件</span></div>`;
        return;
      }
      eventsList.innerHTML = events.slice().reverse().map((item) => {
        const level = (item.level || "").toLowerCase();
        const kind = (item.kind || "event").toUpperCase();
        const ts = item.ts || "--:--:--";
        const msg = item.message || "--";
        const tagCls = ["event-tag", level].join(" ").trim();
        return `<div class="event-item"><span class="event-time">${ts}</span><span class="${tagCls}">${kind}</span><span class="event-msg">${msg}</span></div>`;
      }).join("");
    }

    async function refreshEvents() {
      try {
        const res = await fetch("/api/events?limit=30");
        const data = await res.json();
        renderEvents(data.events || []);
      } catch (err) {
        renderEvents([]);
      }
    }

    refreshEventsBtn.addEventListener("click", refreshEvents);

    function setChipActive(target, enabled) {
      if (enabled) target.classList.add("active");
      else target.classList.remove("active");
    }

    function renderCtrl() {
      setChipActive(ledAutoBtn, ctrlState.led_mode === "auto");
      setChipActive(ledOnBtn, ctrlState.led_mode === "on");
      setChipActive(ledOffBtn, ctrlState.led_mode === "off");
      setChipActive(alarmOnBtn, !!ctrlState.alarm_enable);
      setChipActive(alarmOffBtn, !ctrlState.alarm_enable);
      setChipActive(buzzerOnBtn, !!ctrlState.buzzer_enable);
      setChipActive(buzzerOffBtn, !ctrlState.buzzer_enable);
    }

    async function syncControl() {
      try {
        const res = await fetch("/api/control");
        const data = await res.json();
        if (data && data.control) {
          ctrlState = {
            led_mode: data.control.led_mode || "auto",
            alarm_enable: !!data.control.alarm_enable,
            buzzer_enable: !!data.control.buzzer_enable,
          };
          renderCtrl();
          ctrlNote.textContent = `已同步，板卡IP: ${data.control.board_ip || "--"}`;
        }
      } catch (err) {
        ctrlNote.textContent = "同步失败";
      }
    }

    async function sendControl(next) {
      const payload = {
        led_mode: next.led_mode ?? ctrlState.led_mode,
        alarm_enable: (typeof next.alarm_enable === "boolean") ? next.alarm_enable : ctrlState.alarm_enable,
        buzzer_enable: (typeof next.buzzer_enable === "boolean") ? next.buzzer_enable : ctrlState.buzzer_enable,
      };
      try {
        const res = await fetch("/api/control", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(payload),
        });
        const data = await res.json();
        ctrlState = payload;
        renderCtrl();
        ctrlNote.textContent = data.board_ok ? "控制已下发到板卡" : `仅本地更新: ${data.board_msg || "转发失败"}`;
      } catch (err) {
        ctrlNote.textContent = "控制发送失败";
      }
    }

    syncCtrlBtn.addEventListener("click", syncControl);
    ledAutoBtn.addEventListener("click", () => sendControl({ led_mode: "auto" }));
    ledOnBtn.addEventListener("click", () => sendControl({ led_mode: "on" }));
    ledOffBtn.addEventListener("click", () => sendControl({ led_mode: "off" }));
    alarmOnBtn.addEventListener("click", () => sendControl({ alarm_enable: true }));
    alarmOffBtn.addEventListener("click", () => sendControl({ alarm_enable: false }));
    buzzerOnBtn.addEventListener("click", () => sendControl({ buzzer_enable: true }));
    buzzerOffBtn.addEventListener("click", () => sendControl({ buzzer_enable: false }));

    function applyBadge(el, level, text) {
      el.classList.remove("ok", "warn", "danger");
      if (level) el.classList.add(level);
      el.textContent = text;
    }

    async function refreshStatus() {
      try {
        const res = await fetch("/api/status");
        const data = await res.json();
        if (data.connected) {
          statusDot.classList.add("live");
          statusText.textContent = "在线";
          viewer.classList.remove("is-offline");
        } else {
          statusDot.classList.remove("live");
          statusText.textContent = "离线";
          viewer.classList.add("is-offline");
        }
        if (data.width && data.height) {
          statRes.textContent = `${data.width} x ${data.height}`;
        } else {
          statRes.textContent = "--";
        }
        statFps.textContent = data.fps ? data.fps.toFixed(1) : "--";
        statAge.textContent = data.age_ms >= 0 ? `${data.age_ms} ms` : "--";
        statMode.textContent = "MJPEG";

        const isDoorOpen = !!data.door_open;
        applyBadge(doorState, isDoorOpen ? "ok" : "warn", isDoorOpen ? "已开门" : "已关门");

        const fid = Number.isInteger(data.face_id) ? data.face_id : -1;
        const fscore = (typeof data.face_score_m === "number" && data.face_score_m >= 0)
          ? (data.face_score_m / 1000).toFixed(3)
          : "--";
        const fstate = data.face_state || "IDLE";
        faceState.textContent = fid >= 0 ? `${fstate} | ID:${fid} | S:${fscore}` : `${fstate} | ID:-- | S:${fscore}`;

        const palm = (typeof data.gesture_palm_m === "number" && data.gesture_palm_m >= 0)
          ? (data.gesture_palm_m / 1000).toFixed(3)
          : "--";
        const gstate = data.gesture_state || "IDLE";
        const leftSec = (typeof data.unlock_left_ms === "number" && data.unlock_left_ms > 0)
          ? (data.unlock_left_ms / 1000).toFixed(1)
          : "--";
        gestureState.textContent = `${gstate} | PALM:${palm} | LEFT:${leftSec}s`;

        const alarm = (data.alarm_state || "OFF").toUpperCase();
        if (alarm === "ON") {
          applyBadge(alarmState, "danger", "告警");
        } else {
          applyBadge(alarmState, "ok", "正常");
        }
      } catch (err) {
        statusDot.classList.remove("live");
        statusText.textContent = "离线";
        viewer.classList.add("is-offline");
        applyBadge(doorState, "warn", "--");
        faceState.textContent = "--";
        gestureState.textContent = "--";
        applyBadge(alarmState, "", "--");
      }
    }

    setInterval(refreshStatus, 1000);
    setInterval(refreshEvents, 1500);
    refreshStatus();
    refreshEvents();
    syncControl();
  </script>
</body>
</html>
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-port", type=int, default=9000)
    parser.add_argument("--web-port", type=int, default=8080)
    args = parser.parse_args()

    t = threading.Thread(target=stream_server, args=(args.data_port,), daemon=True)
    t.start()

    app.run(host="0.0.0.0", port=args.web_port, threaded=True)


if __name__ == "__main__":
    main()








