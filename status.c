#include "status.h"

cDynamiteStatus *cDynamiteStatus::status = NULL;

#define SETSTARTUPCHANNELTIMEOUT 60 // 60 seconds

cDynamiteStatus::cDynamiteStatus(int StartupChannel)
{
  init = time(NULL);
  startupChannel = StartupChannel;
  startupChannelSet = false;
  switchCount = 0;
  isyslog("dynamite: startup channel is %d", startupChannel);
}

void cDynamiteStatus::ChannelSwitch(const cDevice *Device, int ChannelNumber, bool LiveView)
{
  if ((ChannelNumber == 0) || startupChannelSet || (startupChannel < 0) || (switchCount > 1))
     return;
  if ((cDevice::PrimaryDevice() != cDevice::ActualDevice()) && (cDevice::PrimaryDevice() == Device))
     return;
  if (ChannelNumber == startupChannel) {
     startupChannelSet = true;
     return;
     }
  if ((time(NULL) - init) > SETSTARTUPCHANNELTIMEOUT) {
     isyslog("dynamite: no devices within %d seconds for receiving startup channel %d, giving up", SETSTARTUPCHANNELTIMEOUT, startupChannel);
     startupChannelSet = true;
     return;
     }
  isyslog("dynamite: device %d switches channel to %d", Device->DeviceNumber() + 1, ChannelNumber);
  switchCount++;
  if (switchCount > 1)
     isyslog("dynamite: assuming manual channel switch, so give up trying to set startup channel on device attach");
}

void cDynamiteStatus::Init(void)
{
  if (status)
     return;
  int startupChannel = Setup.CurrentChannel;
  if (*Setup.InitialChannel) {
     cString cid = Setup.InitialChannel;
     if (isnumber(cid)) // for compatibility with old setup.conf files
        startupChannel = atoi(cid);
     else {
#if VDRVERSNUM > 20300
        LOCK_CHANNELS_READ;
        const cChannels *vdrchannels = Channels;
#else
        cChannels *vdrchannels = &Channels;
#endif
        if (const cChannel *Channel = vdrchannels->GetByChannelID(tChannelID::FromString(cid))) {
           status = new cDynamiteStatus(Channel->Number());
           return;
           }
        }
     }
#if VDRVERSNUM > 20300
  LOCK_CHANNELS_READ;
  const cChannels *vdrchannels = Channels;
#else
  cChannels *vdrchannels = &Channels;
#endif
  if (const cChannel *Channel = vdrchannels->GetByNumber(startupChannel))
     status = new cDynamiteStatus(Channel->Number());
}

void cDynamiteStatus::DeInit(void)
{
  if (status == NULL)
     return;
  delete status;
  status = NULL;
}

void cDynamiteStatus::SetStartupChannel(void)
{
  if (status == NULL)
     return;
  if (status->startupChannelSet) {
     DeInit();
     return;
     }
  isyslog("dynamite: new device attached, retry switching to startup channel %d", status->startupChannel);
#if VDRVERSNUM > 20300
  LOCK_CHANNELS_READ;
  const cChannels *vdrchannels = Channels;
#else
  cChannels *vdrchannels = &Channels;
#endif
  if (!vdrchannels->SwitchTo(status->startupChannel))
     status->switchCount--;
}
