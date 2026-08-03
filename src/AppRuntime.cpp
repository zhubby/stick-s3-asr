#include "AppRuntime.h"

namespace stick_s3_asr {

bool errorRecoveryIsNetworkRecoverable(ErrorRecovery recovery) {
  return recovery == ErrorRecovery::RecoverableNetwork;
}

uint16_t appLoopDelayMs(AppMode mode,
                        bool lowLatencyAudioActive,
                        bool provisioningActive) {
  if (lowLatencyAudioActive) {
    return 2;
  }
  if (mode == AppMode::Connecting || mode == AppMode::Pairing ||
      mode == AppMode::Recognizing || provisioningActive) {
    return 10;
  }
  return 25;
}

}  // namespace stick_s3_asr
