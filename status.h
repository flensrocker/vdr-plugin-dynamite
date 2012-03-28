#ifndef __DYNAMITESTATUS_H
#define __DYNAMITESTATUS_H

#include <vdr/status.h>

class cDynamiteStatus : public cStatus {
private:
  static cDynamiteStatus *status;

  time_t init;
  int    startupChannel;
  bool   startupChannelSet;
  int    switchCount;

  cDynamiteStatus(int StartupChannel);

  virtual void ChannelSwitch(const cDevice *Device, int ChannelNumber, bool LiveView);

public:
  static void Init(void);
  static void DeInit(void);
  static void SetStartupChannel(void);
  };

#endif // __DYNAMITESTATUS_H
