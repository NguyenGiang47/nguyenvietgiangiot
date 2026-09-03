from flask import Flask, request, jsonify, send_from_directory
from datetime import datetime
from pathlib import Path

import json
import io
import os
import sys
import time
import wave
import numpy as np


# Windows thường dùng console CP1252, không in được đầy đủ tiếng Việt.
# Đổi log sang UTF-8 để lỗi hiển thị không làm dừng request upload.
for _stream in (
    sys.stdout,
    sys.stderr
):

    try:

        _stream.reconfigure(
            encoding="utf-8",
            errors="replace"
        )

    except (AttributeError, OSError):

        pass

from scipy.signal import butter, sosfiltfilt

# ============================================================
# FLASK
# ============================================================

app = Flask(__name__)

BASE_DIR = Path(__file__).resolve().parent

UPLOAD_FOLDER = BASE_DIR / "recordings"
DATA_FILE = BASE_DIR / "recordings.json"

UPLOAD_FOLDER.mkdir(
    parents=True,
    exist_ok=True
)


# ============================================================
# GOOGLE DRIVE
# ============================================================

# QUAN TRONG:
# Dùng full Drive scope để server có thể nhìn thấy
# thư mục ÂM THANH / KHOC / DAP_PHA mà bạn đã tạo bằng tay.
DRIVE_SCOPES = [
    "https://www.googleapis.com/auth/drive"
]

DRIVE_CREDENTIALS_FILE = (
    BASE_DIR / "credentials.json"
)

DRIVE_TOKEN_FILE = (
    BASE_DIR / "token.json"
)

DRIVE_ROOT_FOLDER = "ÂM THANH"

# ID thư mục gốc Google Drive lấy từ URL:
# https://drive.google.com/drive/folders/1uj78ypKE6NX0oGPH8L0fN7hCri06Rpiq
DRIVE_ROOT_FOLDER_ID = "1uj78ypKE6NX0oGPH8L0fN7hCri06Rpiq"
DRIVE_ROOT_FOLDER_URL = (
    "https://drive.google.com/drive/folders/"
    "1uj78ypKE6NX0oGPH8L0fN7hCri06Rpiq"
)

# Drive đôi khi phản hồi chậm hoặc mất mạng trong lúc upload WAV.
# Thử lại ngay trên server trước khi báo lỗi về ESP32.
DRIVE_UPLOAD_RETRIES = 3
DRIVE_RETRY_DELAY_SECONDS = 2

_drive_service = None
_drive_folder_cache = {}


# ============================================================
# GOOGLE LOGIN
# ============================================================

def get_drive_service():

    global _drive_service

    if _drive_service is not None:
        return _drive_service

    if not DRIVE_CREDENTIALS_FILE.is_file():

        raise RuntimeError(
            "Không tìm thấy credentials.json. "
            "Hãy đặt credentials.json cùng thư mục server.py"
        )

    creds = None

    # --------------------------------------------------------
    # Đọc token cũ nếu có
    # --------------------------------------------------------

    if DRIVE_TOKEN_FILE.is_file():

        try:

            creds = (
                Credentials
                .from_authorized_user_file(
                    str(
                        DRIVE_TOKEN_FILE
                    ),
                    DRIVE_SCOPES
                )
            )

        except Exception:

            creds = None

    # Token cũ có thể chỉ có quyền đọc Drive. Khi đó phải đăng nhập lại
    # để cấp quyền ghi file vào thư mục đã chỉ định.
    if (
        creds
        and creds.scopes
        and not set(DRIVE_SCOPES).issubset(
            set(creds.scopes)
        )
    ):

        print(
            "[DRIVE] Token cu thieu quyen ghi, dang nhap lai..."
        )
        creds = None

    # --------------------------------------------------------
    # Login / refresh
    # --------------------------------------------------------

    if not creds or not creds.valid:

        if (
            creds
            and creds.expired
            and creds.refresh_token
        ):

            print(
                "[DRIVE] Refresh token..."
            )

            creds.refresh(
                Request()
            )

        else:

            print(
                "[DRIVE] Mở trình duyệt đăng nhập Google..."
            )

            flow = (
                InstalledAppFlow
                .from_client_secrets_file(
                    str(
                        DRIVE_CREDENTIALS_FILE
                    ),
                    DRIVE_SCOPES
                )
            )

            creds = (
                flow.run_local_server(
                    port=0
                )
            )

        DRIVE_TOKEN_FILE.write_text(
            creds.to_json(),
            encoding="utf-8"
        )

    # --------------------------------------------------------
    # Build Drive API
    # --------------------------------------------------------

    _drive_service = build(
        "drive",
        "v3",
        credentials=creds,
        cache_discovery=False
    )

    return _drive_service


# ============================================================
# DRIVE FOLDER
# ============================================================

def drive_escape(text):

    return (
        str(text)
        .replace(
            "\\",
            "\\\\"
        )
        .replace(
            "'",
            "\\'"
        )
    )


def find_folder(
    name,
    parent_id=None
):

    service = (
        get_drive_service()
    )

    q = (
        "mimeType="
        "'application/vnd.google-apps.folder' "
        "and trashed=false "
        f"and name='{drive_escape(name)}'"
    )

    if parent_id:

        q += (
            f" and "
            f"'{drive_escape(parent_id)}' "
            f"in parents"
        )

    result = (
        service
        .files()
        .list(
            q=q,
            spaces="drive",
            includeItemsFromAllDrives=True,
            supportsAllDrives=True,
            fields=(
                "files("
                "id,"
                "name,"
                "parents"
                ")"
            ),
            pageSize=100
        )
        .execute()
    )

    files = result.get(
        "files",
        []
    )

    if not files:

        return None

    return files[0]["id"]


def create_folder(
    name,
    parent_id=None
):

    service = (
        get_drive_service()
    )

    metadata = {
        "name": name,
        "mimeType":
            "application/vnd.google-apps.folder"
    }

    if parent_id:

        metadata[
            "parents"
        ] = [
            parent_id
        ]

    result = (
        service
        .files()
        .create(
            body=metadata,
            supportsAllDrives=True,
            fields="id,name"
        )
        .execute()
    )

    print(
        "[DRIVE] Tạo folder:",
        result.get("name"),
        result.get("id")
    )

    return result["id"]


def get_or_create_folder(
    name,
    parent_id=None
):

    key = (
        name,
        parent_id
    )

    if key in _drive_folder_cache:

        return (
            _drive_folder_cache[
                key
            ]
        )

    folder_id = (
        find_folder(
            name,
            parent_id
        )
    )

    if not folder_id:

        print(
            "[DRIVE] Không tìm thấy folder",
            name,
            "=> tạo mới"
        )

        folder_id = (
            create_folder(
                name,
                parent_id
            )
        )

    else:

        print(
            "[DRIVE] Tìm thấy:",
            name,
            folder_id
        )

    _drive_folder_cache[
        key
    ] = folder_id

    return folder_id


def get_event_drive_folder(
    event_type
):

    # --------------------------------------------------------
    # Root: ÂM THANH
    # --------------------------------------------------------

    # Dùng trực tiếp thư mục gốc đã chỉ định, tránh tìm/tạo nhầm
    # thư mục khác chỉ vì tên hiển thị giống nhau.
    root_id = DRIVE_ROOT_FOLDER_ID

    # --------------------------------------------------------
    # Subfolder
    # --------------------------------------------------------

    folder_name = {
        "khoc": "KHOC",
        "dap_pha": "DAP_PHA"
    }.get(
        event_type
    )

    if not folder_name:

        raise ValueError(
            f"Nhãn không được upload: "
            f"{event_type}"
        )

    return (
        get_or_create_folder(
            folder_name,
            root_id
        )
    )


def find_drive_file(
    filename,
    parent_id
):

    service = (
        get_drive_service()
    )

    q = (
        "trashed=false "
        "and mimeType!='application/vnd.google-apps.folder' "
        f"and name='{drive_escape(filename)}' "
        f"and '{drive_escape(parent_id)}' in parents"
    )

    result = (
        service
        .files()
        .list(
            q=q,
            spaces="drive",
            includeItemsFromAllDrives=True,
            supportsAllDrives=True,
            pageSize=1,
            fields=(
                "files(id,name,webViewLink)"
            )
        )
        .execute()
    )

    files = result.get(
        "files",
        []
    )

    return files[0] if files else None


# ============================================================
# UPLOAD WAV DRIVE
# ============================================================

def upload_wav_to_drive(
    filename,
    wav_bytes,
    event_type
):

    folder_id = (
        get_event_drive_folder(
            event_type
        )
    )

    service = (
        get_drive_service()
    )

    last_error = None

    for attempt in range(
        1,
        DRIVE_UPLOAD_RETRIES + 1
    ):

        try:

            # Nếu lần trước đã tạo file nhưng phản hồi bị mất,
            # lấy lại file cũ thay vì tạo file trùng.
            existing = (
                find_drive_file(
                    filename,
                    folder_id
                )
            )

            if existing:

                if not existing.get("webViewLink"):

                    existing[
                        "webViewLink"
                    ] = (
                        "https://drive.google.com/file/d/"
                        + existing["id"]
                        + "/view"
                    )

                print(
                    "[DRIVE] File da ton tai, dung lai:",
                    filename
                )

                return existing

            metadata = {
                "name": filename,
                "parents": [
                    folder_id
                ]
            }

            # Tạo Media mới cho mỗi lần thử vì stream cũ đã bị đọc hết.
            media = MediaIoBaseUpload(
                io.BytesIO(
                    wav_bytes
                ),
                mimetype="audio/wav",
                resumable=False
            )

            uploaded = (
                service
                .files()
                .create(
                    body=metadata,
                    media_body=media,
                    supportsAllDrives=True,
                    fields=(
                        "id,"
                        "name,"
                        "webViewLink"
                    )
                )
                .execute()
            )

            if not uploaded.get("id"):

                raise RuntimeError(
                    "Drive khong tra ve file ID"
                )

            if not uploaded.get("webViewLink"):

                uploaded[
                    "webViewLink"
                ] = (
                    "https://drive.google.com/file/d/"
                    + uploaded["id"]
                    + "/view"
                )

            return uploaded

        except Exception as e:

            last_error = e

            print(
                f"[DRIVE] Lan thu {attempt}/{DRIVE_UPLOAD_RETRIES} that bai:",
                e
            )

            if attempt < DRIVE_UPLOAD_RETRIES:

                time.sleep(
                    DRIVE_RETRY_DELAY_SECONDS
                )

    raise RuntimeError(
        "Upload Drive that bai sau "
        f"{DRIVE_UPLOAD_RETRIES} lan: {last_error}"
    )


# ============================================================
# JSON DATABASE
# ============================================================

def load_data():

    if not DATA_FILE.exists():

        return []

    try:

        with DATA_FILE.open(
            "r",
            encoding="utf-8"
        ) as f:

            data = json.load(
                f
            )

            if isinstance(
                data,
                list
            ):

                return data

    except Exception as e:

        print(
            "[JSON] Loi:",
            e
        )

    return []


def save_data(
    data
):

    with DATA_FILE.open(
        "w",
        encoding="utf-8"
    ) as f:

        json.dump(
            data,
            f,
            ensure_ascii=False,
            indent=2
        )


# ============================================================
# WAV
# ============================================================

def read_wav_pcm16(
    raw_bytes
):

    with wave.open(
        io.BytesIO(
            raw_bytes
        ),
        "rb"
    ) as wf:

        channels = (
            wf.getnchannels()
        )

        sampwidth = (
            wf.getsampwidth()
        )

        samplerate = (
            wf.getframerate()
        )

        frames = (
            wf.getnframes()
        )

        if channels != 1:

            raise ValueError(
                "WAV phải mono"
            )

        if sampwidth != 2:

            raise ValueError(
                "WAV phải PCM16"
            )

        pcm = (
            wf.readframes(
                frames
            )
        )

    samples = (
        np.frombuffer(
            pcm,
            dtype="<i2"
        )
        .astype(
            np.float32
        )
    )

    return (
        samplerate,
        samples
    )


def make_wav_pcm16(
    samples,
    samplerate
):

    samples = np.asarray(
        samples,
        dtype=np.float32
    )

    samples = np.clip(
        np.round(
            samples
        ),
        -32768,
        32767
    ).astype(
        "<i2"
    )

    out = io.BytesIO()

    with wave.open(
        out,
        "wb"
    ) as wf:

        wf.setnchannels(
            1
        )

        wf.setsampwidth(
            2
        )

        wf.setframerate(
            samplerate
        )

        wf.writeframes(
            samples.tobytes()
        )

    return out.getvalue()


# ============================================================
# AUDIO QUALITY
# ============================================================

def analyze_audio_quality(
    samples
):

    if len(samples) == 0:

        return {
            "peak_percent": 0,
            "clip_percent": 100,
            "rms_dbfs": -120,
            "bad": True,
            "reason": "empty"
        }

    x = (
        samples.astype(
            np.float32
        )
        / 32768.0
    )

    peak = float(
        np.max(
            np.abs(
                x
            )
        )
    )

    peak_percent = (
        peak * 100
    )

    clip_percent = float(
        np.mean(
            np.abs(
                x
            )
            >= 0.985
        )
        * 100
    )

    rms = float(
        np.sqrt(
            np.mean(
                x * x
            )
            + 1e-12
        )
    )

    rms_dbfs = float(
        20.0
        * np.log10(
            max(
                rms,
                1e-6
            )
        )
    )

    bad = (
        clip_percent
        > 1.0
    )

    reason = ""

    if bad:

        reason = (
            f"clipping quá nhiều "
            f"({clip_percent:.2f}%)"
        )

    return {
        "peak_percent":
            round(
                peak_percent,
                2
            ),

        "clip_percent":
            round(
                clip_percent,
                3
            ),

        "rms_dbfs":
            round(
                rms_dbfs,
                2
            ),

        "bad":
            bad,

        "reason":
            reason
    }


# ============================================================
# CLEAN AUDIO
# ============================================================

def clean_audio(
    samples,
    samplerate
):

    x = samples.astype(
        np.float32
    )

    if len(x) < 32:

        return x

    # Remove DC
    x = (
        x
        - np.mean(
            x
        )
    )

    # --------------------------------------------------------
    # Bandpass nhẹ
    # --------------------------------------------------------

    low_hz = 80.0

    high_hz = min(
        7000.0,
        samplerate
        * 0.45
    )

    sos = butter(
        4,
        [
            low_hz,
            high_hz
        ],
        btype="bandpass",
        fs=samplerate,
        output="sos"
    )

    try:

        x = sosfiltfilt(
            sos,
            x
        )

    except ValueError:

        pass

    # --------------------------------------------------------
    # Noise gate nhẹ
    # --------------------------------------------------------

    absx = np.abs(
        x
    )

    gate_start = 180.0
    gate_full = 650.0

    gain = np.ones_like(
        x,
        dtype=np.float32
    )

    quiet = (
        absx
        < gate_start
    )

    transition = (
        (absx >= gate_start)
        &
        (absx < gate_full)
    )

    gain[
        quiet
    ] = 0.18

    gain[
        transition
    ] = (
        0.18
        +
        0.82
        *
        (
            (
                absx[
                    transition
                ]
                -
                gate_start
            )
            /
            (
                gate_full
                -
                gate_start
            )
        )
    )

    x = (
        x * gain
    )

    # --------------------------------------------------------
    # Limit peak -3dBFS
    # --------------------------------------------------------

    peak = float(
        np.max(
            np.abs(
                x
            )
        )
    )

    target_peak = 23170.0

    if (
        peak
        > target_peak
    ):

        x = (
            x
            *
            (
                target_peak
                /
                peak
            )
        )

    return x


# ============================================================
# RECEIVE ESP32
# ============================================================

@app.post(
    "/api/recordings"
)
def receive_recording():

    device_id = (
        request.args.get(
            "device_id",
            "ESP32"
        )
    )

    # Chỉ cho phép ký tự an toàn để dùng trong tên file.
    safe_device_id = "".join(
        c if c.isalnum() or c in ".-_" else "_"
        for c in str(device_id)
    )[:64] or "ESP32"

    # ESP32 gửi cùng event_id khi retry để server không tạo file trùng.
    event_id = "".join(
        c if c.isalnum() or c in ".-_" else "_"
        for c in request.args.get(
            "event_id",
            ""
        ).strip()
    )[:96]

    event_type = (
        request.args.get(
            "type",
            "unknown"
        )
        .strip()
        .lower()
    )

    # Chỉ lưu hai nhãn cảnh báo
    if event_type not in (
        "khoc",
        "dap_pha"
    ):

        return jsonify(
            success=False,
            error=(
                "Không lưu nhãn "
                + event_type
            )
        ), 400

    # Nếu lần gửi trước đã thành công nhưng ESP32 không nhận được reply,
    # trả lại kết quả cũ thay vì tạo file local trùng.
    if event_id:

        for old in load_data():

            if (
                old.get("event_id") == event_id
                and old.get("local_saved")
            ):

                return jsonify(
                    success=True,
                    duplicate=True,
                    local_saved=True,
                    event_type=old.get("type"),
                    event_id=event_id,
                    file=old.get("file")
                ), 200

    try:

        confidence = float(
            request.args.get(
                "confidence",
                0
            )
        )

        khoc = float(
            request.args.get(
                "khoc",
                0
            )
        )

        dap_pha = float(
            request.args.get(
                "dap_pha",
                0
            )
        )

    except ValueError:

        return jsonify(
            success=False,
            error="Sai confidence"
        ), 400

    raw = (
        request.get_data()
    )

    if not raw:

        return jsonify(
            success=False,
            error="Không có WAV"
        ), 400

    # --------------------------------------------------------
    # Read WAV
    # --------------------------------------------------------

    try:

        samplerate, samples = (
            read_wav_pcm16(
                raw
            )
        )

    except Exception as e:

        return jsonify(
            success=False,
            error=(
                f"WAV lỗi: {e}"
            )
        ), 400

    if samplerate != 16000:

        return jsonify(
            success=False,
            error=(
                f"Sample rate sai: "
                f"{samplerate}"
            )
        ), 400

    # --------------------------------------------------------
    # Quality
    # --------------------------------------------------------

    quality = (
        analyze_audio_quality(
            samples
        )
    )

    if quality["bad"]:

        if len(samples) == 0:

            return jsonify(
                success=False,
                rejected=True,
                quality=quality
            ), 422

        # Tiếng đập mạnh có thể làm tín hiệu bị clip. Không bỏ mất
        # cảnh báo thật; clean_audio() vẫn lọc và giới hạn biên độ trước
        # khi lưu WAV.
        print(
            "[AUDIO] Quality warning; continue local save. "
            f"clip={quality['clip_percent']}%"
        )

    # --------------------------------------------------------
    # Clean
    # --------------------------------------------------------

    cleaned = (
        clean_audio(
            samples,
            samplerate
        )
    )

    clean_wav = (
        make_wav_pcm16(
            cleaned,
            samplerate
        )
    )

    # --------------------------------------------------------
    # Filename
    # --------------------------------------------------------

    now = datetime.now()

    if event_id:

        # Tên cố định theo event_id giúp nhận diện lần retry của cùng
        # một cảnh báo, kể cả khi server đã upload xong nhưng mất reply.
        filename = (
            f"{event_id}_{event_type}.wav"
        )

    else:

        filename = (
            f"{now.strftime('%Y%m%d_%H%M%S_%f')}_"
            f"{safe_device_id}_"
            f"{event_type}.wav"
        )

    local_path = (
        UPLOAD_FOLDER
        / filename
    )

    # --------------------------------------------------------
    # Save local
    # --------------------------------------------------------

    local_path.write_bytes(
        clean_wav
    )

    print(
        "[LOCAL] OK:",
        filename
    )

    # --------------------------------------------------------
    # JSON
    # --------------------------------------------------------

    rec = {

        "id":
            filename,

        "time":
            now.strftime(
                "%d/%m/%Y %H:%M:%S"
            ),

        "device":
            safe_device_id,

        "event_id":
            event_id,

        "type":
            event_type,

        "confidence":
            confidence,

        "khoc":
            khoc,

        "dap_pha":
            dap_pha,

        "file":
            filename,

        "peak_percent":
            quality[
                "peak_percent"
            ],

        "clip_percent":
            quality[
                "clip_percent"
            ],

        "rms_dbfs":
            quality[
                "rms_dbfs"
            ],

        "local_saved":
            True
    }

    data = load_data()

    data.insert(
        0,
        rec
    )

    save_data(
        data[:500]
    )

    # --------------------------------------------------------
    # Response
    # --------------------------------------------------------

    return jsonify(

        success=True,

        local_saved=True,

        event_type=
            event_type,

        event_id=
            event_id,

        file=
            filename,

        quality=
            quality

    ), 200


# ============================================================
# EVENTS
# ============================================================

@app.get(
    "/api/events"
)
def events():

    return jsonify(
        load_data()
    )


# ============================================================
# HEALTH
# ============================================================

@app.get(
    "/api/health"
)
def health():

    return jsonify(
        success=True,
        server="ESP32 Audio Server",
        storage="local"
    )


# ============================================================
# WAV PLAY
# ============================================================

@app.get(
    "/recordings/<path:filename>"
)
def recording(
    filename
):

    return send_from_directory(
        str(
            UPLOAD_FOLDER
        ),
        filename
    )


# ============================================================
# DELETE ONE
# ============================================================

@app.delete(
    "/api/events/<path:event_id>"
)
def delete_one(
    event_id
):

    data = load_data()

    kept = []

    target = None

    for x in data:

        if (
            x.get("id")
            == event_id
            or
            x.get("file")
            == event_id
        ):

            target = x

        else:

            kept.append(
                x
            )

    if target is None:

        return jsonify(
            success=False,
            error="Not found"
        ), 404

    filename = (
        target.get(
            "file",
            ""
        )
    )

    path = (
        UPLOAD_FOLDER
        / filename
    )

    if path.is_file():

        try:

            path.unlink()

        except OSError:

            pass

    save_data(
        kept
    )

    return jsonify(
        success=True,
        file=filename
    )


# ============================================================
# DELETE ALL LOCAL
# ============================================================

@app.delete(
    "/api/events"
)
def delete_all():

    data = load_data()

    for x in data:

        filename = (
            x.get(
                "file",
                ""
            )
        )

        path = (
            UPLOAD_FOLDER
            / filename
        )

        if path.is_file():

            try:

                path.unlink()

            except OSError:

                pass

    save_data(
        []
    )

    return jsonify(
        success=True,
        deleted=len(
            data
        )
    )


# ============================================================
# WEB UI
# ============================================================

@app.get("/")
def index():

    # Dùng giao diện chuẩn trong index.html.
    return send_from_directory(
        str(BASE_DIR),
        "index.html"
    )

    # HTML cũ bên dưới được giữ lại để không làm mất dữ liệu khi nâng cấp.
    return """
<!doctype html>

<html lang="vi">

<head>

<meta charset="utf-8">

<meta
    name="viewport"
    content="width=device-width,initial-scale=1"
>

<title>
ESP32-S3 Audio AI
</title>

<style>

body{
    font-family:Arial;
    background:#f4f6f8;
    margin:0;
    color:#222;
}

header{
    background:#17212d;
    color:white;
    padding:20px;
}

.wrap{
    max-width:1200px;
    margin:20px auto;
    padding:0 15px;
}

.card{
    background:white;
    padding:18px;
    border-radius:12px;
    margin-bottom:15px;
}

table{
    width:100%;
    border-collapse:collapse;
}

th,td{
    padding:10px;
    border-bottom:1px solid #ddd;
    text-align:left;
}

th{
    background:#17212d;
    color:white;
}

.khoc{
    color:#d32f2f;
    font-weight:bold;
}

.dap_pha{
    color:#ef6c00;
    font-weight:bold;
}

.good{
    color:green;
    font-weight:bold;
}

audio{
    width:220px;
}

</style>

</head>

<body>

<header>

<h1>
ESP32-S3 AUDIO AI
</h1>

<div>
KHÓC / ĐẬP PHÁ
</div>

</header>

<div class="wrap">

<div class="card">

<div id="status">
Đang chờ...
</div>

</div>


<div class="card">

<table>

<thead>

<tr>

<th>Thời gian</th>

<th>Sự kiện</th>

<th>Độ tin cậy</th>

<th>WAV</th>

</tr>

</thead>

<tbody id="rows">

</tbody>

</table>

</div>

</div>


<script>

function nameOf(t){

    if(t === "khoc"){
        return "KHÓC";
    }

    if(t === "dap_pha"){
        return "ĐẬP PHÁ";
    }

    return t;
}


async function load(){

    try{

        const r =
            await fetch(
                "/api/events",
                {
                    cache:"no-store"
                }
            );

        const d =
            await r.json();

        const rows =
            document.getElementById(
                "rows"
            );

        rows.innerHTML = "";

        if(!d.length){

            document.getElementById(
                "status"
            ).textContent =
                "Chưa có cảnh báo";

            return;
        }

        const latest = d[0];

        document.getElementById(
            "status"
        ).innerHTML =
            "Mới nhất: <b>"
            + nameOf(latest.type)
            + "</b> - "
            + (
                Number(
                    latest.confidence
                )
                * 100
            ).toFixed(1)
            + "%";

        for(const e of d){

            const tr =
                document.createElement(
                    "tr"
                );

            tr.innerHTML =

                "<td>"
                + e.time
                + "</td>"

                + '<td class="'
                + e.type
                + '">'
                + nameOf(e.type)
                + "</td>"

                + "<td><b>"
                + (
                    Number(
                        e.confidence
                    )
                    * 100
                ).toFixed(1)
                + "%</b></td>"

                + "<td>"
                + '<audio controls src="/recordings/'
                + encodeURIComponent(
                    e.file
                )
                + '"></audio>'
                + "</td>";

            rows.appendChild(
                tr
            );
        }

    }
    catch(e){

        document.getElementById(
            "status"
        ).textContent =
            "Không kết nối server";
    }
}


load();

setInterval(
    load,
    2000
);

</script>

</body>

</html>
"""


# ============================================================
# START SERVER
# ============================================================

if __name__ == "__main__":

    print()
    print(
        "============================================"
    )

    print(
        " ESP32-S3 AUDIO SERVER (LOCAL)"
    )

    print(
        "============================================"
    )

    print()
    print(
        "[SERVER] Port 5000"
    )

    print(
        "[SERVER] ESP32 gui vao:"
    )

    print(
        "http://IP_MAY_TINH:5000/api/recordings"
    )

    print()

    app.run(
        host="0.0.0.0",
        port=5000,
        debug=False,
        threaded=True
    )
