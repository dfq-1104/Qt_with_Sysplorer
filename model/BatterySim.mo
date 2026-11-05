package BatterySim
  "LFP 单体电池充放电仿真模型库

  ===== 建模意图 =====
  为 Qt 操作台提供可被 Sysplorer SDK 驱动的电池对象：给定电压/电流激励，
  回算荷电状态 SOC。界面侧按“工作模式联动”呈现，因此模型也必须按模式
  提供单一的电流激励入口，而不是让 U 与 I 同时作为独立自变量。

  ===== 模型抽象与边界 =====
  拓扑：Rint 等效电路（理想 OCV 源 + 欧姆内阻 R0），无 RC 极化支路。
  电荷：库仑计积分 dSOC/dt = eta * I / (3600 * Q_rated)，充电为正。
  输出：SOC、端电压、实际电流、端功率、累计净电量。

  ===== 已知简化（交付说明须一并交代）=====
  1. 只含欧姆内阻，不含极化/扩散过电位，也不含温度与老化；
     因此大倍率下的能量效率会系统性偏高（1C 时约 99%），
     实测 LFP 通常在 92%~95%，差值来自未建模的极化损耗。
  2. OCV-SOC 表为工程示意值，非电芯实测数据，不得用于精度评估。
  3. R0 视为等效直流内阻，已含快速极化成分，未拆分。

  ===== 符号约定 =====
  电流 I：充电为正、放电为负；SOC：0..1。
  "

  function ocvFromSoc
    "由 SOC 线性插值开路电压（表外按端段斜率外插，不外推为常量）"
    input Real soc "荷电状态，0..1";
    input Real socTable[:] "SOC 断点，升序";
    input Real ocvTable[:] "对应 OCV，单位 V";
    output Real ocv "开路电压，单位 V";
  protected
    Integer n;
    Integer i;
  algorithm
    n := size(socTable, 1);
    assert(n >= 2, "OCV 表至少需要 2 个断点");
    assert(size(ocvTable, 1) == n, "OCV 表长度必须与 SOC 断点数量一致");
    if soc <= socTable[1] then
      ocv := ocvTable[1] + (soc - socTable[1])*(ocvTable[2] - ocvTable[1])/(socTable[2] - socTable[1]);
    elseif soc >= socTable[n] then
      ocv := ocvTable[n] + (soc - socTable[n])*(ocvTable[n] - ocvTable[n - 1])/(socTable[n] - socTable[n - 1]);
    else
      i := 1;
      while (i < n - 1) and (soc > socTable[i + 1]) loop
        i := i + 1;
      end while;
      ocv := ocvTable[i] + (soc - socTable[i])*(ocvTable[i + 1] - ocvTable[i])/(socTable[i + 1] - socTable[i]);
    end if;
  end ocvFromSoc;

  model BatteryCell
    "LFP 单体电池：Rint 等效电路 + 库仑计 SOC 积分，支持恒流充 / 恒流放 / 恒压（限流）充"
    import Modelica.Units.SI;

    // ---------------- 电池本体参数 ----------------
    parameter SI.Charge Q_rated = 2.5*3600 "额定容量 [C]，2.5 Ah" 
      annotation (Dialog(group="电池参数"));
    parameter Real SOC0(unit="1") = 0.5 "初始荷电状态 0..1" 
      annotation (Dialog(group="电池参数"));
    parameter SI.Resistance R0 = 0.01 "等效直流内阻 [Ohm]" 
      annotation (Dialog(group="电池参数"));
    parameter Real eta_chg(unit="1") = 0.995 "充电库仑效率（电荷效率，非能量效率）" 
      annotation (Dialog(group="电池参数"));
    parameter Real eta_dis(unit="1") = 1.0 "放电库仑效率（电荷效率，非能量效率）" 
      annotation (Dialog(group="电池参数"));

    // ---------------- OCV-SOC 特性 ----------------
    parameter Real socTable[:] = {0, 0.05, 0.1, 0.2, 0.4, 0.6, 0.8, 0.9, 0.95, 1.0}
      "SOC 断点，升序，覆盖 0..1" annotation (Dialog(group="OCV 特性"));
    parameter Real ocvTable[:] = {2.50, 2.80, 3.00, 3.20, 3.25, 3.27, 3.30, 3.32, 3.35, 3.40}
      "对应 OCV [V]，示意值" annotation (Dialog(group="OCV 特性"));

    // ---------------- 保护限值 ----------------
    input SI.Voltage V_min = 2.5 "放电截止电压 [V]"
      annotation (Dialog(group="保护限值"));
    input SI.Voltage V_max = 3.45 "充电截止电压 [V]（端电压上限；取 Rint 模型可达值，非 3.65 V 极化截止）"
      annotation (Dialog(group="保护限值"));
    input SI.Current I_max = 5.0 "电流限幅 [A]，2C"
      annotation (Dialog(group="保护限值"));

    // ---------------- 工作模式与设定值 ----------------
    Integer mode(start=2) "工作模式：1=恒流充电，2=恒流放电，3=恒压/限流充电，0=静置（可被外层模型随时间驱动）" 
      annotation (Dialog(group="工作模式"),
                  choices(__MWORKS(choice = 1 "恒流充电", choice = 2 "恒流放电", choice = 3 "恒压/限流充电")));
    input SI.Current I_set = 2.5 "恒流模式设定电流，正数，方向由 mode 决定 [A]"
      annotation (Dialog(group="工作模式"));
    input SI.Voltage V_set = 3.45 "恒压/限流模式设定端电压 [V]（不应超过 V_max）"
      annotation (Dialog(group="工作模式"));

    // ---------------- 对外输出 ----------------
    output Real SOC(unit="1") "当前荷电状态 0..1";
    output Real SOC_pct(unit="1") = 100*SOC "当前荷电状态百分比 [%]";
    output SI.Voltage V_term "端电压 [V]";
    output SI.Current I_act "实际电流，充电为正 [A]";
    output SI.Power P_term "端功率 [W]，充电为正";
    output SI.Energy E_net "累计净电量 [J]，充电为正";
    output SI.Charge Q_remain "剩余容量 [C]";

  protected
    SI.Voltage V_ocv "开路电压 [V]";
    SI.Current I_cmd "未限幅的指令电流 [A]";
    Boolean charge_blocked "充电封锁";
    Boolean discharge_blocked "放电封锁";

  initial equation
    SOC = SOC0;
    E_net = 0;

  equation
    // 1) 由状态算 OCV
    V_ocv = ocvFromSoc(SOC, socTable, ocvTable);

    // 2) 按工作模式生成指令电流（恒压模式下电流由电压差与内阻决定）
    I_cmd = if mode == 1 then I_set 
            else if mode == 2 then -I_set 
            else if mode == 3 then (V_set - V_ocv)/R0 
            else 0.0;

    // 3) 过充/过放/截止电压保护：满足任一条件即封锁电流
    //    用 noEvent 抑制保护判据在阈值附近的抖动，避免求解器被事件淹没
    charge_blocked = noEvent(I_cmd > 0 and 
                             (SOC >= 1.0 or V_ocv + min(I_cmd, I_max)*R0 >= V_max));
    discharge_blocked = noEvent(I_cmd < 0 and 
                                (SOC <= 0.0 or V_ocv - min(-I_cmd, I_max)*R0 <= V_min));

    // 4) 实际电流与端电压
    I_act = if charge_blocked or discharge_blocked then 0.0 
            else max(min(I_cmd, I_max), -I_max);
    V_term = V_ocv + I_act*R0;
    P_term = V_term*I_act;

    // 5) 电荷与能量积分
    //    Q_rated 单位为库仑(C)，故直接用 I/Q，不能再除 3600
    der(SOC) = (if I_act >= 0 then eta_chg else eta_dis)*I_act/Q_rated;
    der(E_net) = P_term;

    // 6) 数值越界兜底（正常由保护逻辑挡住，这里防浮点误差累积）
    when SOC >= 1.0 then
      reinit(SOC, 1.0);
    end when;
    when SOC <= 0.0 then
      reinit(SOC, 0.0);
    end when;

    // 7) 派生量
    Q_remain = SOC*Q_rated;

    annotation (
      Icon(graphics={
        Rectangle(extent={{-60,60},{60,-60}},
          lineColor={0,0,0}, fillColor={245,245,245}, fillPattern=FillPattern.Solid),
        Rectangle(extent={{-40,52},{40,-52}},
          lineColor={0,0,0}, fillColor={255,255,255}, fillPattern=FillPattern.Solid),
        Rectangle(extent={{-34,-46},{34,34}},
          lineColor={0,130,0}, fillColor={0,160,0}, fillPattern=FillPattern.Solid),
        Line(points={{-14,60},{-14,72},{14,72},{14,60}}, color={0,0,0}, thickness=1.5),
        Text(extent={{-58,-72},{58,-96}}, textString="BatteryCell", textColor={0,0,0})}),
      Diagram(graphics={
        Rectangle(extent={{-100,80},{100,-80}},
          lineColor={200,200,200}, fillColor={255,255,255}, fillPattern=FillPattern.Solid),
        Text(extent={{-90,64},{90,40}}, textString="Rint 等效电路 + 库仑计", textColor={100,100,100}),
        Text(extent={{-90,20},{90,-10}}, textString="OCV(SOC) --- R0 --- 端电压", textColor={100,100,100}),
        Text(extent={{-90,-30},{90,-60}}, textString="I>0 充电 / I<0 放电", textColor={100,100,100})}),
      experiment(StopTime=1800, Interval=1, __Dymola_Algorithm="Dassl"),
      Documentation(info="<html>
<p>LFP 单体电池简化模型，用于充放电过程的电量演示与界面对接。</p>
<p><b>工作模式</b>：1 恒流充电、2 恒流放电、3 恒压/限流充电。</p>
<p><b>输入</b>：I_set（恒流模式）或 V_set（恒压/限流模式），由 mode 选择。</p>
<p><b>输出</b>：SOC、SOC_pct、V_term、I_act、P_term、E_net、Q_remain。</p>
<p><b>局限</b>：无极化/温度/老化建模，OCV 表为示意值。</p>
</html>"));
  end BatteryCell;

  model BatteryDemo
    "充放电演示：默认 1C 恒流放电，0.5 SOC 放到空约需 1800 s"
    BatteryCell cell(
      Q_rated=2.5*3600,
      SOC0=0.5000,
      R0=0.01,
      eta_chg=0.995,
      eta_dis=1.0,
      V_min=2.5,
      V_max=3.45,
      I_max=5.0,
      I_set=2.5000,
      V_set=3.45) 
      annotation (Placement(transformation(extent={{-30,-30},{30,30}})));

    Real SOC_out = cell.SOC "对外暴露的 SOC";
    Real V_out = cell.V_term "对外暴露的端电压";
    Real I_out = cell.I_act "对外暴露的电流";

  equation
    cell.mode = 2;

    annotation (
      Icon(graphics={
        Rectangle(extent={{-100,100},{100,-100}},
          lineColor={0,0,0}, fillColor={230,240,255}, fillPattern=FillPattern.Solid),
        Text(extent={{-90,20},{90,-20}}, textString="BatteryDemo", textColor={0,0,0})}),
      Diagram(graphics={
        Rectangle(extent={{-100,100},{100,-100}},
          lineColor={200,200,200}, fillColor={255,255,255}, fillPattern=FillPattern.Solid)}),
      experiment(__Dymola_Algorithm="Dassl",StopTime=600,Interval=1));
  end BatteryDemo;

  model BatteryChargeDemo
    "恒流充电演示：从 0.5 SOC 充满约需 1800 s"
    BatteryCell cell(
      Q_rated=2.5*3600,
      SOC0=0.2000,
      R0=0.01,
      eta_chg=0.995,
      eta_dis=1.0,
      V_min=2.5,
      V_max=3.45,
      I_max=5.0,
      I_set=2.0000,
      V_set=3.45) 
      annotation (Placement(transformation(extent={{-28,-28},{28,28}})));

    Real SOC_out = cell.SOC "对外暴露的 SOC";

  equation
    cell.mode = 1;

    annotation (experiment(StopTime=3600,Interval=1)
      );
  end BatteryChargeDemo;

  model BatteryCvDemo
    "恒压/限流充电演示：端电压设定 3.45 V，电流随 SOC 上升而衰减（Rint 模型下前期为限流段）"
    BatteryCell cell(
      Q_rated=2.5*3600,
      SOC0=0.2,
      R0=0.01,
      eta_chg=0.995,
      eta_dis=1.0,
      V_min=2.5,
      V_max=3.45,
      I_max=5.0,
      I_set=2.5,
      V_set=3.45) 
      annotation (Placement(transformation(extent={{-28,-28},{28,28}})));

    Real SOC_out = cell.SOC "对外暴露的 SOC";

  equation
    cell.mode = 3;

    annotation (
      experiment(StopTime=3600, Interval=1));
  end BatteryCvDemo;

  model BatteryCycleDemo
    "同一个模型连续运行：恒流充电 → 静置 → 恒流放电 → 静置（一次仿真覆盖三种模式）"
    BatteryCell cell(
      Q_rated=2.5*3600,
      SOC0=0.2,
      R0=0.01,
      eta_chg=0.995,
      eta_dis=1.0,
      V_min=2.5,
      V_max=3.45,
      I_max=5.0,
      I_set=2.5,
      V_set=3.45) 
      annotation (Placement(transformation(extent={{-28,-28},{28,28}})));

    Real SOC_out = cell.SOC "对外暴露的 SOC";
    Real I_out = cell.I_act "对外暴露的电流";
    Real Mode_out = mode "当前模式，供后处理标注阶段";

  protected
    Integer mode "由时间驱动的模式";

  equation
    // 0–3000 s 恒流充电（SOC 0.2 → 1）
    // 3000–3300 s 静置
    // 3300–6900 s 恒流放电（SOC 1 → 0）
    // 6900–7200 s 静置
    mode = if time < 3000 then 1 
           else if time < 3300 then 0 
           else if time < 6900 then 2 
           else 0;
    cell.mode = mode;

    annotation (
      experiment(StopTime=7200, Interval=1));
  end BatteryCycleDemo;

  model SingleRun
    "参数化单次运行：供界面按用户输入的电压/电流实时重算"
    parameter Integer runMode = 2 "1=恒流充电，2=恒流放电，3=恒压/限流充电";
    parameter Real runCurrent = 3.0 "设定电流 [A]";
    parameter Real runVoltage = 3.45 "恒流充电上限或恒流放电下限 [V]";
    parameter Real socStart = 0.5 "初始 SOC";

    // Top-level inputs survive translation and are written only at a paused
    // communication boundary. Real mode permits one atomic real-input batch.
    input Real liveCurrent = runCurrent "运行期电流指令 [A]";
    input Real liveVoltage = runVoltage "运行期恒压目标 [V]";
    input Real liveMode = runMode "0=静置，1=充电，2=放电，3=恒压";
    input Real liveVMin = if runMode == 2 then runVoltage else 2.5;
    input Real liveVMax = if runMode == 1 then runVoltage else 3.45;
    input Real liveIMax = 5.0;

    BatteryCell cell(
      Q_rated=2.5*3600,
      SOC0=socStart,
      R0=0.01,
      eta_chg=0.995,
      eta_dis=1.0,
      V_min=liveVMin,
      V_max=liveVMax,
      I_max=liveIMax,
      I_set=liveCurrent,
      V_set=liveVoltage)
      annotation (Placement(transformation(extent={{-28,-28},{28,28}})));

    Real SOC_out = cell.SOC "对外暴露的 SOC";
    Real I_out = cell.I_act "对外暴露的电流";
    Real V_out = cell.V_term "对外暴露的端电压";

  equation
    cell.mode = integer(liveMode);

    annotation (
      experiment(StopTime=3600, Interval=1));
  end SingleRun;

  annotation (
    version="1.0.0",
    Documentation(info="<html>
<p>BatterySim：LFP 单体电池充放电仿真模型库。</p>
<p>包含 BatteryCell（模型主体）与 BatteryDemo（仿真示例）。</p>
</html>"));
end BatterySim;

