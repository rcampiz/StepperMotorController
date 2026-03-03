/**
 * @file encoder_handlers.cpp
 * @brief CommandParser encoder command handlers (CTRL:ENC namespace)
 */

#include "L2_protocol/command_parser_internal.hpp"

namespace Comms {

void CommandParser::cmdEncoder() {
  // Check if encoder is available
  if (!m_dispatcher.isEncoderAvailable()) {
    respondErr("Encoder not available");
    return;
  }

  // Get full encoder state via dispatcher
  auto enc = m_dispatcher.getEncoderSnapshot();

  // newlib-nano: no %lld — pre-format int64_t as string
  char countStr[24];
  i64toa(enc.count, countStr, sizeof(countStr));

  if (m_format == ResponseFormat::JSON) {
    char buf[192];
    snprintf(
        buf, sizeof(buf),
        "{\"count\":%s,\"velocity\":%ld,\"index_seen\":%s,\"index_tick\":%lu,"
        "\"revolutions\":%ld,\"index_period_us\":%lu}",
        countStr, static_cast<long>(enc.velocity),
        enc.indexSeen ? "true" : "false",
        static_cast<unsigned long>(enc.indexTick),
        static_cast<long>(enc.revolutions),
        static_cast<unsigned long>(enc.indexPeriodUs));
    respondJsonOk("ENC", buf);
  } else {
    // ASCII format: OK count=<n> vel=<n> idx=<0|1> idx_tick=<n> rev=<n>
    char buf[96];
    snprintf(buf, sizeof(buf), "count=%s vel=%ld idx=%d idx_tick=%lu rev=%ld",
             countStr, static_cast<long>(enc.velocity),
             enc.indexSeen ? 1 : 0,
             static_cast<unsigned long>(enc.indexTick),
             static_cast<long>(enc.revolutions));
    respondOk(buf);
  }
}

void CommandParser::cmdEncFilter(const ParsedCommand &cmd) {
  // Query mode: no arguments → return full filter config
  if (cmd.argCount < 1) {
    ICommandDispatcher::EncFilterParams cfg;
    m_dispatcher.encFilterGetConfig(cfg);
    char buf[512];
    unsigned pGain = (cfg.padeGainPct > 0) ? cfg.padeGainPct : 50;
    unsigned pMax = (cfg.padeMaxCorr > 0) ? cfg.padeMaxCorr : 50;
    unsigned bqCut = (cfg.biquadCutoffHz > 0) ? cfg.biquadCutoffHz : 10;
    unsigned ntCtr = (cfg.notchCenterHz > 0) ? cfg.notchCenterHz : 25;
    unsigned ntQ = (cfg.notchQ10 > 0) ? cfg.notchQ10 : 50;
    unsigned hAlph = (cfg.holtAlpha > 0) ? cfg.holtAlpha : 51;
    unsigned hBeta = (cfg.holtBeta > 0) ? cfg.holtBeta : 13;
    if (m_format == ResponseFormat::JSON) {
      snprintf(
          buf, sizeof(buf),
          "{\"meas_window_ms\":%u,\"sample_rate_hz\":%u,"
          "\"ema_enabled\":%s,\"ema_alpha\":%u,"
          "\"sma_enabled\":%s,\"sma_window\":%u,"
          "\"pade_enabled\":%s,\"pade_gain\":%u,\"pade_max_corr\":%u,"
          "\"biquad_enabled\":%s,\"biquad_cutoff_hz\":%u,"
          "\"notch_enabled\":%s,\"notch_center_hz\":%u,\"notch_q10\":%u,"
          "\"holt_enabled\":%s,\"holt_alpha\":%u,\"holt_beta\":%u}",
          (unsigned)cfg.measWindowMs, (unsigned)cfg.sampleRateHz,
          (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_EMA) ? "true" : "false",
          (unsigned)cfg.emaAlpha,
          (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_SMA) ? "true" : "false",
          (unsigned)cfg.smaWindow,
          (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_PADE) ? "true" : "false", pGain,
          pMax, (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_BIQUAD) ? "true" : "false",
          bqCut, (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_NOTCH) ? "true" : "false",
          ntCtr, ntQ,
          (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_HOLT) ? "true" : "false", hAlph,
          hBeta);
      respondJsonOk(m_currentCmd, buf);
    } else {
      snprintf(buf, sizeof(buf),
               "window=%ums rate=%uHz EMA=%s alpha=%u SMA=%s window=%u"
               " PADE=%s gain=%u%% max=%utps BIQUAD=%s cut=%uHz"
               " NOTCH=%s ctr=%uHz Q=%u.%u HOLT=%s a=%u b=%u",
               (unsigned)cfg.measWindowMs, (unsigned)cfg.sampleRateHz,
               (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_EMA) ? "ON" : "OFF",
               (unsigned)cfg.emaAlpha,
               (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_SMA) ? "ON" : "OFF",
               (unsigned)cfg.smaWindow,
               (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_PADE) ? "ON" : "OFF", pGain,
               pMax, (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_BIQUAD) ? "ON" : "OFF",
               bqCut, (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_NOTCH) ? "ON" : "OFF",
               ntCtr, ntQ / 10, ntQ % 10,
               (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_HOLT) ? "ON" : "OFF", hAlph,
               hBeta);
      respondOk(buf);
    }
    return;
  }

  // Legacy set commands: NONE / EMA <alpha> / SMA <window>
  if (strcmp(cmd.args[0], "NONE") == 0) {
    m_dispatcher.encFilterSetLegacy(0, 0);
    m_dispatcher.configSetEncFilter(0, 0);
    respondOk("NONE");
    return;
  }

  if (strcmp(cmd.args[0], "EMA") == 0) {
    if (cmd.argCount < 2) {
      respondErr("EMA requires alpha (0-255)");
      return;
    }
    long val = strtol(cmd.args[1], nullptr, 10);
    if (val < 0 || val > 255) {
      respondErr("EMA alpha must be 0-255");
      return;
    }
    m_dispatcher.encFilterSetLegacy(1, static_cast<uint8_t>(val));
    m_dispatcher.configSetEncFilter(ICommandDispatcher::EncFilterParams::FILT_EMA,
                                    static_cast<uint8_t>(val));
    char buf[48];
    snprintf(buf, sizeof(buf), "EMA alpha=%u", (unsigned)val);
    respondOk(buf);
    return;
  }

  if (strcmp(cmd.args[0], "SMA") == 0) {
    if (cmd.argCount < 2) {
      respondErr("SMA requires window size (2-32)");
      return;
    }
    long val = strtol(cmd.args[1], nullptr, 10);
    if (val < 2 || val > 32) {
      respondErr("SMA window must be 2-32");
      return;
    }
    m_dispatcher.encFilterSetLegacy(2, static_cast<uint8_t>(val));
    m_dispatcher.configSetEncFilter(ICommandDispatcher::EncFilterParams::FILT_SMA, 0);
    m_dispatcher.configSetEncSmaWindow(static_cast<uint8_t>(val));
    char buf[48];
    snprintf(buf, sizeof(buf), "SMA window=%u", (unsigned)val);
    respondOk(buf);
    return;
  }

  // Legacy: bare number treated as EMA alpha
  long val = strtol(cmd.args[0], nullptr, 10);
  if (val < 0 || val > 255) {
    respondErr("Unknown filter type or alpha out of range (0-255)");
    return;
  }
  m_dispatcher.encFilterSetLegacy(1, static_cast<uint8_t>(val));
  char buf[48];
  snprintf(buf, sizeof(buf), "EMA alpha=%u", (unsigned)val);
  respondOk(buf);
}

// CTRL:ENC:FILT:* sub-commands
void CommandParser::cmdEncFilterSub(const char *sub, const ParsedCommand &cmd) {
  // CTRL:ENC:FILT:WINDOW <ms>
  if (strcmp(sub, "WINDOW") == 0) {
    if (cmd.argCount < 1) {
      respondErr("WINDOW requires ms (1-255)");
      return;
    }
    long val = strtol(cmd.args[0], nullptr, 10);
    if (val < 1 || val > 255) {
      respondErr("Window must be 1-255 ms");
      return;
    }
    ICommandDispatcher::EncFilterParams cfg;
    m_dispatcher.encFilterGetConfig(cfg);
    cfg.measWindowMs = static_cast<uint8_t>(val);
    m_dispatcher.encFilterSetConfig(cfg);
    char buf[32];
    snprintf(buf, sizeof(buf), "window=%ums", (unsigned)val);
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:EMA <0|1> [alpha]
  if (strcmp(sub, "EMA") == 0) {
    if (cmd.argCount < 1) {
      respondErr("EMA requires 0|1 [alpha]");
      return;
    }
    long enable = strtol(cmd.args[0], nullptr, 10);
    ICommandDispatcher::EncFilterParams cfg;
    m_dispatcher.encFilterGetConfig(cfg);
    if (enable) {
      cfg.filterFlags |= ICommandDispatcher::EncFilterParams::FILT_EMA;
      if (cmd.argCount >= 2) {
        long alpha = strtol(cmd.args[1], nullptr, 10);
        if (alpha < 0 || alpha > 255) {
          respondErr("EMA alpha must be 0-255");
          return;
        }
        cfg.emaAlpha = static_cast<uint8_t>(alpha);
      }
    } else {
      cfg.filterFlags &= ~ICommandDispatcher::EncFilterParams::FILT_EMA;
    }
    m_dispatcher.encFilterSetConfig(cfg);
    char buf[48];
    snprintf(buf, sizeof(buf), "EMA %s alpha=%u",
             (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_EMA) ? "ON" : "OFF",
             (unsigned)cfg.emaAlpha);
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:SMA <0|1> [window]
  if (strcmp(sub, "SMA") == 0) {
    if (cmd.argCount < 1) {
      respondErr("SMA requires 0|1 [window]");
      return;
    }
    long enable = strtol(cmd.args[0], nullptr, 10);
    ICommandDispatcher::EncFilterParams cfg;
    m_dispatcher.encFilterGetConfig(cfg);
    if (enable) {
      cfg.filterFlags |= ICommandDispatcher::EncFilterParams::FILT_SMA;
      if (cmd.argCount >= 2) {
        long win = strtol(cmd.args[1], nullptr, 10);
        if (win < 0 || win > 32) {
          respondErr("SMA window must be 0-32");
          return;
        }
        cfg.smaWindow = static_cast<uint8_t>(win);
      }
    } else {
      cfg.filterFlags &= ~ICommandDispatcher::EncFilterParams::FILT_SMA;
    }
    m_dispatcher.encFilterSetConfig(cfg);
    char buf[48];
    snprintf(buf, sizeof(buf), "SMA %s window=%u",
             (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_SMA) ? "ON" : "OFF",
             (unsigned)cfg.smaWindow);
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:PADE <0|1> [gain%] [maxCorr]
  if (strcmp(sub, "PADE") == 0) {
    if (cmd.argCount < 1) {
      respondErr("PADE requires 0|1 [gain% 1-100] [maxCorr 1-255]");
      return;
    }
    long enable = strtol(cmd.args[0], nullptr, 10);
    ICommandDispatcher::EncFilterParams cfg;
    m_dispatcher.encFilterGetConfig(cfg);
    if (enable) {
      cfg.filterFlags |= ICommandDispatcher::EncFilterParams::FILT_PADE;
      if (cmd.argCount >= 2) {
        long gain = strtol(cmd.args[1], nullptr, 10);
        if (gain < 1 || gain > 100) {
          respondErr("Gain must be 1-100%%");
          return;
        }
        cfg.padeGainPct = static_cast<uint8_t>(gain);
      }
      if (cmd.argCount >= 3) {
        long maxC = strtol(cmd.args[2], nullptr, 10);
        if (maxC < 1 || maxC > 255) {
          respondErr("MaxCorr must be 1-255 tps");
          return;
        }
        cfg.padeMaxCorr = static_cast<uint8_t>(maxC);
      }
    } else {
      cfg.filterFlags &= ~ICommandDispatcher::EncFilterParams::FILT_PADE;
    }
    m_dispatcher.encFilterSetConfig(cfg);
    char buf[64];
    snprintf(buf, sizeof(buf), "PADE %s gain=%u%% max=%utps",
             (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_PADE) ? "ON" : "OFF",
             (unsigned)((cfg.padeGainPct > 0) ? cfg.padeGainPct : 50),
             (unsigned)((cfg.padeMaxCorr > 0) ? cfg.padeMaxCorr : 50));
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:BIQUAD <0|1> [cutoffHz]
  if (strcmp(sub, "BIQUAD") == 0) {
    if (cmd.argCount < 1) {
      respondErr("BIQUAD requires 0|1 [cutoff 1-50 Hz]");
      return;
    }
    long enable = strtol(cmd.args[0], nullptr, 10);
    ICommandDispatcher::EncFilterParams cfg;
    m_dispatcher.encFilterGetConfig(cfg);
    if (enable) {
      cfg.filterFlags |= ICommandDispatcher::EncFilterParams::FILT_BIQUAD;
      if (cmd.argCount >= 2) {
        long cutoff = strtol(cmd.args[1], nullptr, 10);
        if (cutoff < 1 || cutoff > 50) {
          respondErr("Cutoff must be 1-50 Hz");
          return;
        }
        cfg.biquadCutoffHz = static_cast<uint8_t>(cutoff);
      }
    } else {
      cfg.filterFlags &= ~ICommandDispatcher::EncFilterParams::FILT_BIQUAD;
    }
    m_dispatcher.encFilterSetConfig(cfg);
    char buf[64];
    snprintf(buf, sizeof(buf), "BIQUAD %s cutoff=%uHz",
             (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_BIQUAD) ? "ON" : "OFF",
             (unsigned)((cfg.biquadCutoffHz > 0) ? cfg.biquadCutoffHz : 10));
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:NOTCH <0|1> [centerHz] [Q*10]
  if (strcmp(sub, "NOTCH") == 0) {
    if (cmd.argCount < 1) {
      respondErr("NOTCH requires 0|1 [center 1-50 Hz] [Q*10 1-100]");
      return;
    }
    long enable = strtol(cmd.args[0], nullptr, 10);
    ICommandDispatcher::EncFilterParams cfg;
    m_dispatcher.encFilterGetConfig(cfg);
    if (enable) {
      cfg.filterFlags |= ICommandDispatcher::EncFilterParams::FILT_NOTCH;
      if (cmd.argCount >= 2) {
        long center = strtol(cmd.args[1], nullptr, 10);
        if (center < 1 || center > 50) {
          respondErr("Center must be 1-50 Hz");
          return;
        }
        cfg.notchCenterHz = static_cast<uint8_t>(center);
      }
      if (cmd.argCount >= 3) {
        long q10 = strtol(cmd.args[2], nullptr, 10);
        if (q10 < 1 || q10 > 100) {
          respondErr("Q*10 must be 1-100");
          return;
        }
        cfg.notchQ10 = static_cast<uint8_t>(q10);
      }
    } else {
      cfg.filterFlags &= ~ICommandDispatcher::EncFilterParams::FILT_NOTCH;
    }
    m_dispatcher.encFilterSetConfig(cfg);
    char buf[64];
    unsigned ctr = (cfg.notchCenterHz > 0) ? cfg.notchCenterHz : 25;
    unsigned q = (cfg.notchQ10 > 0) ? cfg.notchQ10 : 50;
    snprintf(buf, sizeof(buf), "NOTCH %s center=%uHz Q=%u.%u",
             (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_NOTCH) ? "ON" : "OFF", ctr,
             q / 10, q % 10);
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:HOLT <0|1> [alpha] [beta]
  if (strcmp(sub, "HOLT") == 0) {
    if (cmd.argCount < 1) {
      respondErr("HOLT requires 0|1 [alpha 0-255] [beta 0-255]");
      return;
    }
    long enable = strtol(cmd.args[0], nullptr, 10);
    ICommandDispatcher::EncFilterParams cfg;
    m_dispatcher.encFilterGetConfig(cfg);
    if (enable) {
      cfg.filterFlags |= ICommandDispatcher::EncFilterParams::FILT_HOLT;
      if (cmd.argCount >= 2) {
        long alpha = strtol(cmd.args[1], nullptr, 10);
        if (alpha < 0 || alpha > 255) {
          respondErr("Alpha must be 0-255");
          return;
        }
        cfg.holtAlpha = static_cast<uint8_t>(alpha);
      }
      if (cmd.argCount >= 3) {
        long beta = strtol(cmd.args[2], nullptr, 10);
        if (beta < 0 || beta > 255) {
          respondErr("Beta must be 0-255");
          return;
        }
        cfg.holtBeta = static_cast<uint8_t>(beta);
      }
    } else {
      cfg.filterFlags &= ~ICommandDispatcher::EncFilterParams::FILT_HOLT;
    }
    m_dispatcher.encFilterSetConfig(cfg);
    char buf[64];
    snprintf(buf, sizeof(buf), "HOLT %s alpha=%u beta=%u",
             (cfg.filterFlags & ICommandDispatcher::EncFilterParams::FILT_HOLT) ? "ON" : "OFF",
             (unsigned)((cfg.holtAlpha > 0) ? cfg.holtAlpha : 51),
             (unsigned)((cfg.holtBeta > 0) ? cfg.holtBeta : 13));
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:RATE <hz>
  if (strcmp(sub, "RATE") == 0) {
    if (cmd.argCount < 1) {
      respondErr("RATE requires hz (100-10000)");
      return;
    }
    long val = strtol(cmd.args[0], nullptr, 10);
    if (val < 100 || val > 10000) {
      respondErr("Rate must be 100-10000 Hz");
      return;
    }
    ICommandDispatcher::EncFilterParams cfg;
    m_dispatcher.encFilterGetConfig(cfg);
    cfg.sampleRateHz = static_cast<uint16_t>(val);
    m_dispatcher.encFilterSetConfig(cfg);
    char buf[32];
    snprintf(buf, sizeof(buf), "rate=%uHz", (unsigned)val);
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:SAVE
  if (strcmp(sub, "SAVE") == 0) {
    ICommandDispatcher::EncFilterParams cfg;
    m_dispatcher.encFilterGetConfig(cfg);
    uint8_t rateDiv = (cfg.sampleRateHz != 1000)
                          ? static_cast<uint8_t>(cfg.sampleRateHz / 100)
                          : 0;
    m_dispatcher.configSetEncFilterFull(cfg.filterFlags, cfg.emaAlpha,
                                        cfg.smaWindow, cfg.measWindowMs,
                                        rateDiv);
    if (m_dispatcher.configSaveToFlash()) {
      respondOk("Filter config saved");
    } else {
      respondErr("Flash write failed");
    }
    return;
  }

  // CTRL:ENC:FILT:RESET
  if (strcmp(sub, "RESET") == 0) {
    ICommandDispatcher::EncFilterParams cfg = {};
    cfg.filterFlags = ICommandDispatcher::EncFilterParams::FILT_EMA | ICommandDispatcher::EncFilterParams::FILT_SMA |
                      ICommandDispatcher::EncFilterParams::FILT_PADE | ICommandDispatcher::EncFilterParams::FILT_BIQUAD;
    cfg.emaAlpha = 200;
    cfg.smaWindow = 8;
    cfg.measWindowMs = 40;
    cfg.sampleRateHz = 1000;
    cfg.padeGainPct = 25;
    cfg.padeMaxCorr = 50;
    cfg.biquadCutoffHz = 5;
    cfg.notchCenterHz = 0;
    cfg.notchQ10 = 0;
    cfg.holtAlpha = 0;
    cfg.holtBeta = 0;
    m_dispatcher.encFilterSetConfig(cfg);
    respondOk("Filter reset to defaults");
    return;
  }

  respondErr("Unknown ENC:FILT sub-command");
}

void CommandParser::cmdGetEncoderStatus() {
  const char *encStr = encoderStatusToString(m_dispatcher.getEncoderStatus());

  char buf[80];
  if (m_dispatcher.isEncoderAvailable()) {
    auto enc = m_dispatcher.getEncoderSnapshot();
    snprintf(buf, sizeof(buf), "status=%s count=%ld vel=%ld idx=%d", encStr,
             static_cast<long>(static_cast<int32_t>(enc.count)),
             static_cast<long>(enc.velocity),
             enc.indexSeen ? 1 : 0);
  } else {
    snprintf(buf, sizeof(buf), "status=%s", encStr);
  }
  respondOk(buf);
}

} // namespace Comms
