#include "battery_simulator.h"

#include <QtGlobal>

QVector<ReplaySample> BatterySimulator::run(const SimRequest& req, double dt)
{
    // ---- 与 BatteryCell 完全一致的参数 ----
    static const double SOC_T[] = { 0.0, 0.05, 0.10, 0.20, 0.40, 0.60, 0.80, 0.90, 0.95, 1.00 };
    static const double OCV_T[] = { 2.50, 2.80, 3.00, 3.20, 3.25, 3.27, 3.30, 3.32, 3.35, 3.40 };
    const int N = static_cast<int>(sizeof(SOC_T) / sizeof(SOC_T[0]));

    const double Q_RATED = 2.5 * 3600.0;
    const double R0 = 0.01;
    const double V_MIN = 2.5;
    const double V_MAX = 3.45;
    const double I_MAX = 5.0;
    const double ETA_CHG = 0.995;
    const double ETA_DIS = 1.0;

    if (dt <= 0.0) dt = 1.0;

    QVector<ReplaySample> out;
    out.reserve(static_cast<int>(req.duration / dt) + 2);

    double soc = qBound(0.0, req.soc0, 1.0);

    for (int k = 0; k * dt <= req.duration + 1e-9; ++k) {
        const double t = k * dt;

        // OCV 线性插值
        double ocv = OCV_T[N - 1];
        if (soc <= SOC_T[0]) {
            ocv = OCV_T[0];
        } else if (soc >= SOC_T[N - 1]) {
            ocv = OCV_T[N - 1];
        } else {
            int i = 0;
            while (i < N - 2 && soc > SOC_T[i + 1]) ++i;
            const double w = (soc - SOC_T[i]) / (SOC_T[i + 1] - SOC_T[i]);
            ocv = OCV_T[i] + (OCV_T[i + 1] - OCV_T[i]) * w;
        }

        // 指令电流
        double i_cmd = 0.0;
        if (req.mode == 1) {
            i_cmd = req.current;
        } else if (req.mode == 2) {
            i_cmd = -req.current;
        } else {
            i_cmd = (req.voltage - ocv) / R0;
        }

        // 过充 / 过放保护
        const bool chargeBlocked =
            (i_cmd > 0.0) && ((soc >= 1.0) || (ocv + qMin(i_cmd, I_MAX) * R0 >= V_MAX));
        const bool dischargeBlocked =
            (i_cmd < 0.0) && ((soc <= 0.0) || (ocv - qMin(-i_cmd, I_MAX) * R0 <= V_MIN));

        const double i_act = (chargeBlocked || dischargeBlocked)
                                 ? 0.0
                                 : qBound(-I_MAX, i_cmd, I_MAX);
        const double v_term = ocv + i_act * R0;

        ReplaySample s;
        s.t = t;
        s.soc = soc;
        s.current = i_act;
        s.voltage = v_term;
        out.append(s);

        const double eta = (i_act >= 0.0) ? ETA_CHG : ETA_DIS;
        soc = qBound(0.0, soc + eta * i_act / Q_RATED * dt, 1.0);
    }

    return out;
}

