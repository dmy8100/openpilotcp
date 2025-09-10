import random
import shutil
import os
import subprocess
import glob
from cereal import log
from openpilot.system.hardware.base import HardwareBase, LPABase, ThermalConfig, ThermalZone

NetworkType = log.DeviceState.NetworkType
NetworkStrength = log.DeviceState.NetworkStrength


class Pc(HardwareBase):
  def get_os_version(self):
    return None

  def get_device_type(self):
    return "pc"

  def reboot(self, reason=None):
    subprocess.check_output(["sudo", "reboot"])

  def uninstall(self):
    print("uninstall")

  def get_imei(self, slot):
    return f"{random.randint(0, 1 << 32):015d}"

  def get_serial(self):
    return "cccccccc"

  def get_network_info(self):
    return None

  def get_sim_lpa(self) -> LPABase:
    raise NotImplementedError("SIM LPA not implemented for PC")

  def get_network_type(self):
    return NetworkType.wifi

  def get_memory_usage_percent(self):
    try:
        with open('/proc/meminfo') as f:
            meminfo = {}
            for line in f:
                parts = line.split()
                if len(parts) >= 2:
                    key = parts[0].rstrip(':')
                    value = int(parts[1])  # kB
                    meminfo[key] = value

        # 基于您的系统数据: MemTotal: 13196996 kB, MemAvailable: 11451184 kB
        total = meminfo.get('MemTotal', 0)
        available = meminfo.get('MemAvailable', meminfo.get('MemFree', 0))
        if total > 0:
            used_percent = ((total - available) / total) * 100
            return int(used_percent)
    except:
        pass
    return 0

  def get_free_space_percent(self):
    try:
      # 获取根分区使用情况
      usage = shutil.disk_usage('/')
      free_percent = (usage.free / usage.total) * 100
      return free_percent
    except:
      return 0.0

  def get_sim_info(self):
    return {
      'sim_id': '',
      'mcc_mnc': None,
      'network_type': ["Unknown"],
      'sim_state': ["ABSENT"],
      'data_connected': False
    }

  def get_network_strength(self, network_type):
    return NetworkStrength.unknown

  def get_current_power_draw(self):
    return 0

  def get_som_power_draw(self):
    return 0

  def shutdown(self):
    os.system("sudo poweroff")

  def get_thermal_config(self):
    cpu_zones = []
    gpu_zones = []



    # 尝试从 hwmon 设备读取温度数据
    hwmon_thermal_paths = glob.glob('/sys/class/hwmon/hwmon1/temp1_input')
    for path in hwmon_thermal_paths:
        try:
            with open(path) as f:
                temp = int(f.read().strip()) // 1000  # 转换为摄氏度
            # 获取传感器名称
            label_path = path.replace('_input', '_label')
            if os.path.exists(label_path):
                with open(label_path) as f:
                    zone_type = f.read().strip()
            else:
                zone_type = os.path.basename(path).replace('_input', '')
            # 直接传入温度值，避免调用 read 方法
            zone = ThermalZone(zone_type)
            zone.scale = 1  # 温度值已经转换为摄氏度，不需要再除以 scale
            zone.zone_number = -1  # 禁用自动读取
            zone.read = lambda path=path: int(open(path).read().strip()) // 1000  # 每次调用时重新读取温度值
            cpu_zones.append(zone)
        except:
            pass

    if not cpu_zones:
        cpu_zones.append(ThermalZone('thermal_zone0'))

    return ThermalConfig(
        cpu=cpu_zones,
        gpu=None,  # 移除GPU热区检测
        memory=None,  # PC环境通常没有独立的内存温度传感器
        pmic=None     # PC环境没有PMIC
    )

  def set_screen_brightness(self, percentage):
    pass

  def get_screen_brightness(self):
    return 0

  def set_power_save(self, powersave_enabled):
    pass

  def get_gpu_usage_percent(self):
    import glob
    max_usage = 0
    try:
        gpu_busy_files = glob.glob('/sys/class/drm/card*/device/gpu_busy_percent')
        for gpu_file in gpu_busy_files:
            try:
                with open(gpu_file) as f:
                    usage = int(f.read().strip())
                    max_usage = max(max_usage, usage)
            except:
                continue
    except Exception:
        pass
    return max_usage

  def get_modem_temperatures(self):
    return []

  def get_nvme_temperatures(self):
    return []

  def initialize_hardware(self):
    pass

  def get_networks(self):
    return None

