#pragma once

#include <QVector>

#include "replay_data.h"

// 用户自定义参数的一次仿真请求
struct SimRequest
{
    int    mode = 2;          // 1 = 恒流充电，2 = 恒流放电，3 = 恒压/限流
    double current = 2.5;     // A，正数
    double voltage = 3.45;    // V
    double soc0 = 0.5;        // 初始 SOC
    double duration = 1800.0; // s
};

// 与 BatterySim.mo 中 BatteryCell 一致的 Rint 模型 + 库仑计
// 已用 Sysplorer 导出的曲线校验：SOC 最大偏差 0.024%，电压 0.0014 V
class BatterySimulator
{
public:
    static QVector<ReplaySample> run(const SimRequest& req, double dt = 1.0);
};

