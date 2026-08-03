#pragma once

#include <cstdint>

#include "AppTypes.h"

namespace stick_s3_asr {

enum class ErrorRecovery {
  Fatal,
  RecoverableNetwork,
};

bool errorRecoveryIsNetworkRecoverable(ErrorRecovery recovery);
uint16_t appLoopDelayMs(AppMode mode,
                        bool lowLatencyAudioActive,
                        bool provisioningActive);

}  // namespace stick_s3_asr
