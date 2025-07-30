import random
import shutil
import os
import subprocess
import glob
from cereal import log
from openpilot.system.hardware.base import HardwareBase, ThermalConfig, ThermalZone

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

  def get_sim_lpa(self) -> str:
    raise NotImplementedError("SIM LPA not implemented for PC")

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

    # 扫描CPU热区
    for i in range(10):
        zone_path = f'/sys/class/thermal/thermal_zone{i}'
        if os.path.exists(f'{zone_path}/temp'):
            try:
                with open(f'{zone_path}/type') as f:
                    zone_type = f.read().strip()
                cpu_zones.append(ThermalZone(zone_type))
            except:
                cpu_zones.append(ThermalZone(f'thermal_zone{i}'))

    # 尝试添加GPU热区（基于您的系统信息）
    # 虽然您的 /sys/class/drm/card0/device/gpu_busy_percent 显示0
    # 但可能存在GPU温度传感器
    gpu_thermal_paths = [
        '/sys/class/drm/card0/device/hwmon/hwmon*/temp1_input',
        '/sys/class/hwmon/hwmon*/temp*_label'  # 查找标记为GPU的温度传感器
    ]

    for pattern in gpu_thermal_paths:
        for path in glob.glob(pattern):
            try:
                # 检查是否是GPU相关的温度传感器
                if 'gpu' in path.lower() or 'card' in path.lower():
                    gpu_zones.append(ThermalZone(f'gpu_thermal_{len(gpu_zones)}'))
            except:
                pass

    if not cpu_zones:
        cpu_zones.append(ThermalZone('thermal_zone0'))

    return ThermalConfig(
        cpu=cpu_zones,
        gpu=gpu_zones if gpu_zones else None,  # 只有找到GPU热区才添加
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
    try:
      with open('/sys/class/drm/card0/device/gpu_busy_percent') as f:
        return int(f.read().strip())
    except Exception:
      return 0

  def get_modem_temperatures(self):
    return []

  def get_nvme_temperatures(self):
    return []

  def initialize_hardware(self):
    pass

  def get_networks(self):
    return None
