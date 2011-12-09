#ifndef __DYNAMITESTATUS_H
#define __DYNAMITESTATUS_H

#include <vdr/status.h>

class cDynamiteStatus : public cStatus {
private:
  static cDynamiteStatus *status;

  time_t init;
  int    initialChannel;
  bool   initialChannelSet;
  int    switchCount;

  cDynamiteStatus(int InitialChannel);

  virtual void ChannelSwitch(const cDevice *Device, int ChannelNumber);

public:
  static void Init(void);
  static void DeInit(void);
  static void SetInitialChannel(void);
  };

#endif // __DYNAMITESTATUS_H
