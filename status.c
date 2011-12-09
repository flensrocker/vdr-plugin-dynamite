#include "status.h"

cDynamiteStatus *cDynamiteStatus::status = NULL;

#define SETINITIALCHANNELTIMEOUT 60 // 60 seconds

cDynamiteStatus::cDynamiteStatus(int InitialChannel)
{
  init = time(NULL);
  initialChannel = InitialChannel;
  initialChannelSet = false;
  switchCount = 0;
  isyslog("dynamite: initial channel is %d", initialChannel);
}

void cDynamiteStatus::ChannelSwitch(const cDevice *Device, int ChannelNumber)
{
  if ((ChannelNumber == 0) || initialChannelSet || (initialChannel < 0) || (switchCount > 1))
     return;
  if ((cDevice::PrimaryDevice() != cDevice::ActualDevice()) && (cDevice::PrimaryDevice() == Device))
     return;
  if (ChannelNumber == initialChannel) {
     initialChannelSet = true;
     return;
     }
  if ((time(NULL) - init) > SETINITIALCHANNELTIMEOUT) {
     isyslog("dynamite: no devices within %d seconds for receiving initial channel %d, giving up", SETINITIALCHANNELTIMEOUT, initialChannel);
     initialChannelSet = true;
     return;
     }
  isyslog("dynamite: device %d switches channel to %d", Device->DeviceNumber() + 1, ChannelNumber);
  switchCount++;
  if (switchCount > 1)
     isyslog("dynamite: assuming manual channel switch, so give up trying to set initial channel on device attach");
}

void cDynamiteStatus::Init(void)
{
  if (status)
     return;
  if (*Setup.InitialChannel) {
     cString cid = Setup.InitialChannel;
     if (isnumber(cid)) { // for compatibility with old setup.conf files
        if (cChannel *Channel = Channels.GetByNumber(atoi(cid)))
           cid = Channel->GetChannelID().ToString();
        }
     if (cChannel *Channel = Channels.GetByChannelID(tChannelID::FromString(cid)))
        status = new cDynamiteStatus(Channel->Number());
     }
}

void cDynamiteStatus::DeInit(void)
{
  if (status == NULL)
     return;
  delete status;
  status = NULL;
}

void cDynamiteStatus::SetInitialChannel(void)
{
  if (status == NULL)
     return;
  if (status->initialChannelSet) {
     DeInit();
     return;
     }
  if (!Channels.SwitchTo(status->initialChannel))
     status->switchCount--;
}
