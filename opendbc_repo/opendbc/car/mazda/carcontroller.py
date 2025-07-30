from opendbc.can.packer import CANPacker
from opendbc.car import Bus, apply_driver_steer_torque_limits, structs
from opendbc.car.interfaces import CarControllerBase
from opendbc.car.mazda import mazdacan
from opendbc.car.mazda.values import CarControllerParams, Buttons
from opendbc.car.common.conversions import Conversions as CV
from openpilot.common.params import Params

VisualAlert = structs.CarControl.HUDControl.VisualAlert


class CarController(CarControllerBase):
  def __init__(self, dbc_names, CP):
    super().__init__(dbc_names, CP)
    self.apply_torque_last = 0
    self.packer = CANPacker(dbc_names[Bus.pt])
    self.brake_counter = 0

    self.activateCruise = 0
    self.speed_from_pcm = 1

    # 优化的速度控制参数 - 参考新代码的参数管理
    self.last_target_speed = 0
    self.speed_adjustment_threshold = 5  # 速度调整阈值
    self.control_frequency = 10  # 控制频率 (10Hz)

  def update(self, CC, CS, now_nanos):

    if self.frame % 50 == 0:
      params = Params()
      self.speed_from_pcm = params.get_int("SpeedFromPCM")

    can_sends = []

    apply_torque = 0

    if CC.latActive:
      # calculate steer and also set limits due to driver torque
      new_torque = int(round(CC.actuators.torque * CarControllerParams.STEER_MAX))
      apply_torque = apply_driver_steer_torque_limits(new_torque, self.apply_torque_last,
                                                      CS.out.steeringTorque, CarControllerParams)

    if CC.cruiseControl.cancel:
      # If brake is pressed, let us wait >70ms before trying to disable crz to avoid
      # a race condition with the stock system, where the second cancel from openpilot
      # will disable the crz 'main on'. crz ctrl msg runs at 50hz. 70ms allows us to
      # read 3 messages and most likely sync state before we attempt cancel.
      self.brake_counter = self.brake_counter + 1
      if self.frame % 10 == 0 and not (CS.out.brakePressed and self.brake_counter < 7):
        # Cancel Stock ACC if it's enabled while OP is disengaged
        # Send at a rate of 10hz until we sync with stock ACC state
        can_sends.append(mazdacan.create_button_cmd(self.packer, self.CP, CS.crz_btns_counter, Buttons.CANCEL))
    else:
      self.brake_counter = 0
      if CC.cruiseControl.resume and self.frame % 5 == 0:
        # Mazda Stop and Go requires a RES button (or gas) press if the car stops more than 3 seconds
        # Send Resume button when planner wants car to move
        can_sends.append(mazdacan.create_button_cmd(self.packer, self.CP, CS.crz_btns_counter, Buttons.RESUME))
      # 优化的速度控制逻辑 - 参考新代码的控制频率
      elif CC.enabled and self.frame % 10 == 0 and not CS.out.gasPressed:
        # 参考新代码的安全检查和控制频率
        spam_button = self.make_spam_button(CC, CS)
        if spam_button > 0:
          self.brake_counter = 0
          can_sends.append(mazdacan.create_button_cmd(self.packer, self.CP, self.frame // 10, spam_button))

    self.apply_torque_last = apply_torque

    # send HUD alerts
    if self.frame % 50 == 0:
      ldw = CC.hudControl.visualAlert == VisualAlert.ldw
      steer_required = CC.hudControl.visualAlert == VisualAlert.steerRequired
      # TODO: find a way to silence audible warnings so we can add more hud alerts
      steer_required = steer_required and CS.lkas_allowed_speed
      can_sends.append(mazdacan.create_alert_command(self.packer, CS.cam_laneinfo, ldw, steer_required))

    # send steering command
    can_sends.append(mazdacan.create_steering_control(self.packer, self.CP,
                                                      self.frame, apply_torque, CS.cam_lkas))

    new_actuators = CC.actuators.as_builder()
    new_actuators.torque = apply_torque / CarControllerParams.STEER_MAX
    new_actuators.torqueOutputCan = apply_torque

    self.frame += 1
    return new_actuators, can_sends

  def make_spam_button(self, CC, CS):
    hud_control = CC.hudControl

    # 优化的速度计算 - 参考新代码的精度控制
    set_speed_in_units = hud_control.setSpeed * (CV.MS_TO_KPH if CS.is_metric else CV.MS_TO_MPH)
    target = int(set_speed_in_units + 0.5)
    target = int(round(target / 5.0) * 5.0)  # 保持5km/h步进

    current = int(CS.out.cruiseState.speed * CV.MS_TO_KPH + 0.5)
    current = int(round(current / 5.0) * 5.0)

    v_ego_kph = CS.out.vEgo * CV.MS_TO_KPH

    # 安全检查 - 参考新代码的安全机制
    cant_activate = CS.out.brakePressed or CS.out.gasPressed

    # 速度限制检查 - 参考新代码的边界控制
    V_CRUISE_MAX_KPH = 160  # 最大巡航速度
    V_CRUISE_MIN_KPH = 31   # 最小巡航速度

    # 确保目标速度在合理范围内
    target = max(min(target, V_CRUISE_MAX_KPH), V_CRUISE_MIN_KPH)
    current = max(min(current, V_CRUISE_MAX_KPH), V_CRUISE_MIN_KPH)

    if CC.enabled:
      if not CS.out.cruiseState.enabled:
        # 激活巡航的条件 - 参考新代码的激活逻辑
        if (hud_control.leadVisible or v_ego_kph > 10.0) and self.activateCruise == 0 and not cant_activate:
          self.activateCruise = 1
          print("RESUME")
          return Buttons.RESUME
      elif CC.cruiseControl.resume:
        return Buttons.RESUME
      elif target < current and current >= V_CRUISE_MIN_KPH and self.speed_from_pcm != 1:
        # 优化的减速逻辑 - 参考新代码的精确控制
        speed_diff = current - target
        if speed_diff >= 5:  # 只有当速度差足够大时才调整
          print(f"SET_MINUS target={target}, current={current}, diff={speed_diff}")
          return Buttons.SET_MINUS
      elif target > current and current < V_CRUISE_MAX_KPH and self.speed_from_pcm != 1:
        # 优化的加速逻辑 - 参考新代码的精确控制
        speed_diff = target - current
        if speed_diff >= 5:  # 只有当速度差足够大时才调整
          print(f"SET_PLUS target={target}, current={current}, diff={speed_diff}")
          return Buttons.SET_PLUS
    elif CS.out.activateCruise:
      # 参考新代码的激活逻辑
      if (hud_control.leadVisible or v_ego_kph > 10.0) and self.activateCruise == 0 and not cant_activate:
        self.activateCruise = 1
        print("RESUME")
        return Buttons.RESUME

    return 0
