import os
import sys
import glob
import platform

Import("env")

def detect_serial_port():
    # 사용 가능한 포트 목록 가져오기
    system = platform.system()

    if system == "Windows":
        # Windows용: COM1 ~ COM256 중 존재하는 포트 찾기
        ports = ["COM%s" % (i + 1) for i in range(256)]
    elif system == "Darwin":
        # macOS용: /dev/cu.* (USB to serial)
        ports = glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/cu.*")
    else:
        # Linux용: /dev/ttyUSB* 또는 /dev/ttyACM*
        ports = glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*")

    # 실제 존재하는 포트 필터링
    ports = [p for p in ports if os.path.exists(p)]
    if not ports:
        print("❌ [erase_flash] 시리얼 포트를 찾을 수 없습니다.")
        sys.exit(1)

    return ports[0]

# 포트 자동 감지 또는 환경에서 가져오기
upload_port = env.get("UPLOAD_PORT") or detect_serial_port()

# PlatformIO 내부 Python 경로
pio_python = os.path.expanduser("~/.platformio/penv/bin/python") if platform.system() != "Windows" else sys.executable

# esptool.py 경로
esptool_path = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")

# 명령어 조합
cmd = f'"{pio_python}" "{esptool_path}" --chip esp32 --port "{upload_port}" erase_flash'

# 실행
print(f"🧨 플래시 초기화 실행: {cmd}")
env.Execute(cmd)
