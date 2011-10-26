#include "dynamicdevice.h"
#include "monitor.h"
#include <glob.h>
#include <vdr/skins.h>
#include <vdr/transfer.h>

cPlugin *cDynamicDevice::dynamite = NULL;
int cDynamicDevice::defaultGetTSTimeout = 0;
int cDynamicDevice::idleTimeoutMinutes = 0;
int cDynamicDevice::idleWakeupHours = 0;
cString *cDynamicDevice::idleHook = NULL;
cDvbDeviceProbe *cDynamicDevice::dvbprobe = NULL;
bool cDynamicDevice::enableOsdMessages = false;
int cDynamicDevice::numDynamicDevices = 0;
cMutex cDynamicDevice::arrayMutex;
cDynamicDevice *cDynamicDevice::dynamicdevice[MAXDEVICES] = { NULL };

int cDynamicDevice::IndexOf(const char *DevPath, int &NextFreeIndex, int WishIndex)
{
  cMutexLock lock(&arrayMutex);
  NextFreeIndex = -1;
  int index = -1;
  for (int i = 0; (i < numDynamicDevices) && ((index < 0) || (NextFreeIndex < 0) || (WishIndex >= 0)); i++) {
      if (dynamicdevice[i]->devpath == NULL) {
         if (WishIndex >= 0)
            isyslog("dynamite: device at slot %d has cardindex %d", i + 1, dynamicdevice[i]->CardIndex());
         if ((NextFreeIndex < 0) || ((WishIndex >= 0) && (dynamicdevice[i]->CardIndex() == WishIndex))) {
            NextFreeIndex = i;
            if (dynamicdevice[i]->CardIndex() == WishIndex)
               break;
            }
         }
      else if (index < 0) {
         if (strcmp(DevPath, **dynamicdevice[i]->devpath) == 0)
            index = i;
         }
      }
  return index;
}

cDynamicDevice *cDynamicDevice::GetDynamicDevice(int Index)
{
  if ((Index < 0) || (Index >= numDynamicDevices))
     return NULL;
  return dynamicdevice[Index];
}

bool cDynamicDevice::ProcessQueuedCommands(void)
{
  for (cDynamicDeviceProbe::cDynamicDeviceProbeItem *dev = cDynamicDeviceProbe::commandQueue.First(); dev; dev = cDynamicDeviceProbe::commandQueue.Next(dev)) {
      switch (dev->cmd) {
         case ddpcAttach:
          {
           AttachDevice(*dev->devpath);
           break;
          }
         case ddpcDetach:
          {
           DetachDevice(*dev->devpath, false);
           break;
          }
         case ddpcService:
          {
           if (dynamite && (dev->devpath != NULL) && (**dev->devpath != NULL)) {
              int len = strlen(*dev->devpath);
              if (len > 0) {
                 char *data = strchr(const_cast<char*>(**dev->devpath), ' ');
                 if (data != NULL) {
                    data[0] = '\0';
                    data++;
                    dynamite->Service(*dev->devpath, data);
                    }
                 }
              }
           break;
          }
        }
      }
  cDynamicDeviceProbe::commandQueue.Clear();
  return true;
}

int cDynamicDevice::GetProposedCardIndex(const char *DevPath)
{
  int cardindex = -1;
  if (DevPath == NULL)
     return cardindex;
  cUdevDevice *dev = cUdev::GetDeviceFromDevName(DevPath);
  if (dev != NULL) {
     const char *val = dev->GetPropertyValue("dynamite_cardindex");
     isyslog("dynamite: udev cardindex is %s", val);
     int intVal = -1;
     if (val && (sscanf(val, "%d", &intVal) == 1) && (intVal >= 0) && (intVal <= MAXDEVICES))
        cardindex = intVal;
     delete dev;
     }
  return cardindex;
}

void cDynamicDevice::DetachAllDevices(bool Force)
{
  cMutexLock lock(&arrayMutex);
  isyslog("dynamite: %sdetaching all devices", (Force ? "force " : ""));
  for (int i = 0; i < numDynamicDevices; i++) {
      if (Force)
         dynamicdevice[i]->DeleteSubDevice();
      else if (dynamicdevice[i]->devpath)
         cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcDetach, (**dynamicdevice[i]->devpath));
      }
}

cString cDynamicDevice::ListAllDevices(int &ReplyCode)
{
  cMutexLock lock(&arrayMutex);
  cString devices;
  int count = 0;
  for (int i = 0; i < numDynamicDevices; i++) {
      if ((dynamicdevice[i]->devpath != NULL) && (dynamicdevice[i]->subDevice != NULL)) {
         count++;
         devices = cString::sprintf("%s%d%s %s\n", (count == 1) ? "" : *devices
                                                 , i + 1
                                                 , ((PrimaryDevice() == dynamicdevice[i]) || !dynamicdevice[i]->isDetachable) ? "*" : ""
                                                 , **dynamicdevice[i]->devpath);
         }
      }
  if (count == 0) {
     ReplyCode = 901;
     return cString::sprintf("there are no attached devices");
     }
  return devices;
}

cString cDynamicDevice::AttachDevicePattern(const char *Pattern)
{
  if (!Pattern)
     return "invalid pattern";
  cString reply;
  glob_t result;
  if (glob(Pattern, GLOB_MARK, 0, &result) == 0) {
     for (uint i = 0; i < result.gl_pathc; i++) {
         cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcAttach, result.gl_pathv[i]);
         reply = cString::sprintf("%squeued %s for attaching\n", (i == 0) ? "" : *reply, result.gl_pathv[i]);
         }
     }
  globfree(&result);
  return reply;
}

eDynamicDeviceReturnCode cDynamicDevice::AttachDevice(const char *DevPath)
{
  if (!DevPath)
     return ddrcNotSupported;

  cMutexLock lock(&arrayMutex);
  int wishIndex = GetProposedCardIndex(DevPath);
  if (wishIndex >= 0)
     isyslog("dynamite: %s wants card index %d", DevPath, wishIndex);
  int freeIndex = -1;
  int index = IndexOf(DevPath, freeIndex, wishIndex);
  int adapter = -1;
  int frontend = -1;

  if (index >= 0) {
     esyslog("dynamite: %s is already attached", DevPath);
     return ddrcAlreadyAttached;
     }

  if (freeIndex < 0) {
     esyslog("dynamite: no more free slots for %s", DevPath);
     return ddrcNoFreeDynDev;
     }

  cUdevDevice *dev = cUdev::GetDeviceFromDevName(DevPath);
  if (dev != NULL) {
     bool ignore = false;
     const char *tmp;
     if (((tmp = dev->GetPropertyValue("dynamite_attach")) != NULL)
      && ((strcmp(tmp, "0") == 0) || (strcasecmp(tmp, "n") == 0)
       || (strcasecmp(tmp, "no") == 0) || (strcasecmp(tmp, "ignore") == 0))) {
        isyslog("dynamite: udev says don't attach %s", DevPath);
        ignore = true;
        }
     else if (((tmp = dev->GetPropertyValue("dynamite_instanceid")) != NULL) && isnumber(tmp)) {
        int devInstanceId = strtol(tmp, NULL, 10);
        if (devInstanceId != InstanceId) {
           isyslog("dynamite: device %s is for vdr instance %d, we are %d", DevPath, devInstanceId, InstanceId);
           ignore = true;
           }
        }
     delete dev;
     if (ignore)
        return ddrcNotSupported;
     }

  cDevice::nextParentDevice = dynamicdevice[freeIndex];
  
  for (cDynamicDeviceProbe *ddp = DynamicDeviceProbes.First(); ddp; ddp = DynamicDeviceProbes.Next(ddp)) {
      if (ddp->Attach(dynamicdevice[freeIndex], DevPath))
         goto attach; // a plugin has created the actual device
      }

  // if it's a dvbdevice try the DvbDeviceProbes as a fallback for unpatched plugins
  if (sscanf(DevPath, "/dev/dvb/adapter%d/frontend%d", &adapter, &frontend) == 2) {
     for (cDvbDeviceProbe *dp = DvbDeviceProbes.First(); dp; dp = DvbDeviceProbes.Next(dp)) {
         if (dp != dvbprobe) {
            if (dp->Probe(adapter, frontend))
               goto attach;
            }
         }
     new cDvbDevice(adapter, frontend, dynamicdevice[freeIndex]);
     goto attach;
     }

  esyslog("dynamite: can't attach %s", DevPath);
  return ddrcNotSupported;

attach:
  dynamicdevice[freeIndex]->lastCloseDvr = time(NULL);
  while (!dynamicdevice[freeIndex]->Ready())
        cCondWait::SleepMs(2);
  dynamicdevice[freeIndex]->devpath = new cString(DevPath);
  isyslog("dynamite: attached device %s to dynamic device slot %d", DevPath, freeIndex + 1);
  dynamicdevice[freeIndex]->ReadUdevProperties();
  cPluginManager::CallAllServices("dynamite-event-DeviceAttached-v0.1", (void*)DevPath);
  if (enableOsdMessages) {
     cString osdMsg = cString::sprintf(tr("attached %s"), DevPath);
     Skins.QueueMessage(mtInfo, *osdMsg);
     }
  return ddrcSuccess;
}

eDynamicDeviceReturnCode cDynamicDevice::DetachDevice(const char *DevPath, bool Force)
{
  if (!DevPath)
     return ddrcNotSupported;

  cMutexLock lock(&arrayMutex);
  int freeIndex = -1;
  int index = -1;
  if (isnumber(DevPath))
     index = strtol(DevPath, NULL, 10) - 1;
  else
     index = IndexOf(DevPath, freeIndex, -1);

  if ((index < 0) || (index >= numDynamicDevices)) {
     esyslog("dynamite: device %s not found", DevPath);
     return ddrcNotFound;
     }

  if (!Force) {
     if (!dynamicdevice[index]->isDetachable) {
        esyslog("dynamite: detaching of device %s is not allowed", DevPath);
        return ddrcNotAllowed;
        }

     if (dynamicdevice[index] == PrimaryDevice()) {
        esyslog("dynamite: detaching of primary device %s is not supported", DevPath);
        return ddrcIsPrimaryDevice;
        }

     if (dynamicdevice[index]->Receiving(false)) {
        esyslog("dynamite: can't detach device %s, it's receiving something important", DevPath);
        return ddrcIsReceiving;
        }
     }

  dynamicdevice[index]->DeleteSubDevice();
  isyslog("dynamite: detached device %s%s", DevPath, (Force ? " (forced)" : ""));
  if (enableOsdMessages) {
     cString osdMsg = cString::sprintf(tr("detached %s"), DevPath);
     Skins.QueueMessage(mtInfo, *osdMsg);
     }
  return ddrcSuccess;
}

eDynamicDeviceReturnCode cDynamicDevice::SetLockDevice(const char *DevPath, bool Lock)
{
  if (!DevPath)
     return ddrcNotSupported;

  cMutexLock lock(&arrayMutex);
  int freeIndex = -1;
  int index = -1;
  if (isnumber(DevPath))
     index = strtol(DevPath, NULL, 10) - 1;
  else
     index = IndexOf(DevPath, freeIndex, -1);

  if ((index < 0) || (index >= numDynamicDevices))
     return ddrcNotFound;

  dynamicdevice[index]->InternSetLock(Lock);
  return ddrcSuccess;
}

static void CallIdleHook(const char *IdleHook, const char *DevPath, bool Idle)
{
  const char *idleHookCmd = *cString::sprintf("%s --idle=%s --device=%s", IdleHook, (Idle ? "on" : "off"), DevPath);
  isyslog("dynamite: calling idle hook %s", idleHookCmd);
  SystemExec(idleHookCmd, false);
}

eDynamicDeviceReturnCode cDynamicDevice::SetIdle(const char *DevPath, bool Idle)
{
  if (!DevPath)
     return ddrcNotSupported;

  cMutexLock lock(&arrayMutex);
  int freeIndex = -1;
  int index = -1;
  if (isnumber(DevPath))
     index = strtol(DevPath, NULL, 10) - 1;
  else
     index = IndexOf(DevPath, freeIndex, -1);

  if ((index < 0) || (index >= numDynamicDevices))
     return ddrcNotFound;

  isyslog("dynamite: set device %s to %s", DevPath, (Idle ? "idle" : "not idle"));
  if (idleHook && !Idle)
     CallIdleHook(**idleHook, dynamicdevice[index]->GetDevPath(), Idle);
  if (((cDevice*)dynamicdevice[index])->SetIdle(Idle)) {
     if (idleHook && Idle)
        CallIdleHook(**idleHook, dynamicdevice[index]->GetDevPath(), Idle);
     }
  else if (idleHook && !Idle)
     CallIdleHook(**idleHook, dynamicdevice[index]->GetDevPath(), Idle);
  if (Idle) {
     dynamicdevice[index]->idleSince = time(NULL);
     dynamicdevice[index]->lastCloseDvr = dynamicdevice[index]->idleSince;
     }
  else {
     dynamicdevice[index]->idleSince = 0;
     dynamicdevice[index]->lastCloseDvr = time(NULL);
     }
  return ddrcSuccess;
}

void cDynamicDevice::AutoIdle(void)
{
  if (idleTimeoutMinutes <= 0)
     return;
  cMutexLock lock(&arrayMutex);
  time_t now = time(NULL);
  bool wokeupSomeDevice = false;
  int seconds = 0;
  for (int i = 0; i < numDynamicDevices; i++) {
      if (dynamicdevice[i]->devpath != NULL) {
         if (dynamicdevice[i]->IsIdle()) {
            seconds = now - dynamicdevice[i]->idleSince;
            if ((dynamicdevice[i]->idleSince > 0) && (seconds >= (idleWakeupHours * 3600))) {
               isyslog("dynamite: device %s idle for %d hours, waking up", dynamicdevice[i]->GetDevPath(), seconds / 3600);
               cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcService, *cString::sprintf("dynamite-SetNotIdle-v0.1 %s", dynamicdevice[i]->GetDevPath()));
               wokeupSomeDevice = true;
               }
            }
         else {
            seconds = now - dynamicdevice[i]->lastCloseDvr;
            if ((dynamicdevice[i]->lastCloseDvr > 0) && (seconds >= (idleTimeoutMinutes * 60))) {
               if (dynamicdevice[i]->lastCloseDvr > 0)
                  isyslog("dynamite: device %s unused for %d minutes, set to idle", dynamicdevice[i]->GetDevPath(), seconds / 60);
               else
                  isyslog("dynamite: device %s never used , set to idle", dynamicdevice[i]->GetDevPath());
               cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcService, *cString::sprintf("dynamite-SetIdle-v0.1 %s", dynamicdevice[i]->GetDevPath()));
               }
            }
         }
      }

  if (wokeupSomeDevice) {
     // initiate epg-scan?
     }
}

eDynamicDeviceReturnCode cDynamicDevice::SetGetTSTimeout(const char *DevPath, int Seconds)
{
  if (!DevPath || (Seconds < 0))
     return ddrcNotSupported;

  cMutexLock lock(&arrayMutex);
  int freeIndex = -1;
  int index = -1;
  if (isnumber(DevPath))
     index = strtol(DevPath, NULL, 10) - 1;
  else
     index = IndexOf(DevPath, freeIndex, -1);

  if ((index < 0) || (index >= numDynamicDevices))
     return ddrcNotFound;

  dynamicdevice[index]->InternSetGetTSTimeout(Seconds);
  return ddrcSuccess;
}

void cDynamicDevice::SetDefaultGetTSTimeout(int Seconds)
{
  if (Seconds >= 0) {
     defaultGetTSTimeout = Seconds;
     isyslog("dynamite: set default GetTS-Timeout to %d seconds", Seconds);
     cMutexLock lock(&arrayMutex);
     for (int i = 0; i < numDynamicDevices; i++)
         dynamicdevice[i]->InternSetGetTSTimeout(Seconds);
     }
}

eDynamicDeviceReturnCode cDynamicDevice::SetGetTSTimeoutHandlerArg(const char *DevPath, const char *Arg)
{
  if (!DevPath || !Arg)
     return ddrcNotSupported;

  cMutexLock lock(&arrayMutex);
  int freeIndex = -1;
  int index = -1;
  if (isnumber(DevPath))
     index = strtol(DevPath, NULL, 10) - 1;
  else
     index = IndexOf(DevPath, freeIndex, -1);

  if ((index < 0) || (index >= numDynamicDevices))
     return ddrcNotFound;

  dynamicdevice[index]->InternSetGetTSTimeoutHandlerArg(Arg);
  return ddrcSuccess;
}

bool cDynamicDevice::IsAttached(const char *DevPath)
{
  cMutexLock lock(&arrayMutex);
  int freeIndex = -1;
  int index = IndexOf(DevPath, freeIndex, -1);
  return ((index >= 0) && (index >= numDynamicDevices));
}

cDynamicDevice::cDynamicDevice()
:index(-1)
,devpath(NULL)
,udevRemoveSyspath(NULL)
,getTSTimeoutHandlerArg(NULL)
,isDetachable(true)
,getTSTimeout(defaultGetTSTimeout)
,restartSectionHandler(false)
{
  index = numDynamicDevices;
  if (numDynamicDevices < MAXDEVICES) {
     dynamicdevice[index] = this;
     numDynamicDevices++;
     }
  else
     esyslog("dynamite: ERROR: too many dynamic devices!");
}

cDynamicDevice::~cDynamicDevice()
{
  DeleteSubDevice();
  if (getTSTimeoutHandlerArg)
     delete getTSTimeoutHandlerArg;
  getTSTimeoutHandlerArg = NULL;
}

const char *cDynamicDevice::GetDevPath(void) const
{
  return (devpath ? **devpath : "");
}

void cDynamicDevice::ReadUdevProperties(void)
{
  if (devpath == NULL)
     return;
  cUdevDevice *dev = cUdev::GetDeviceFromDevName(**devpath);
  if (dev != NULL) {
     const char *timeout = dev->GetPropertyValue("dynamite_timeout");
     int seconds = -1;
     if (timeout && (sscanf(timeout, "%d", &seconds) == 1) && (seconds >= 0))
        InternSetGetTSTimeout(seconds);

     const char *timeoutHandlerArg = dev->GetPropertyValue("dynamite_timeout_handler_arg");
     if (timeoutHandlerArg)
        InternSetGetTSTimeoutHandlerArg(timeoutHandlerArg);

     cUdevDevice *p = dev->GetParent();
     if (p) {
        const char *subsystem = p->GetSubsystem();
        const char *syspath = p->GetSyspath();
        if (subsystem && syspath && (strcmp(subsystem, "usb") == 0)) {
           cUdevUsbRemoveFilter::AddItem(syspath, **devpath);
           if (udevRemoveSyspath)
              delete udevRemoveSyspath;
           udevRemoveSyspath = new cString(syspath);
           }
        }

     delete dev;
     }
}

void cDynamicDevice::InternSetGetTSTimeout(int Seconds)
{
  getTSTimeout = Seconds;
  if (subDevice == NULL)
     return; // no log message if no device is connected
  if (Seconds == 0)
     isyslog("dynamite: disable GetTSTimeout on device %s", GetDevPath());
  else
     isyslog("dynamite: set GetTSTimeout on device %s to %d seconds", GetDevPath(), Seconds);
}

void cDynamicDevice::InternSetGetTSTimeoutHandlerArg(const char *Arg)
{
  if (getTSTimeoutHandlerArg)
     delete getTSTimeoutHandlerArg;
  getTSTimeoutHandlerArg = new cString(Arg);
  isyslog("dynamite: set GetTSTimeoutHandlerArg on device %s to %s", GetDevPath(), Arg);
}

void cDynamicDevice::InternSetLock(bool Lock)
{
  isDetachable = !Lock;
  isyslog("dynamite: %slocked device %s", Lock ? "" : "un", GetDevPath());
}

void cDynamicDevice::DeleteSubDevice()
{
  if (subDevice) {
     Cancel(3);
     if (cTransferControl::ReceiverDevice() == this)
        cControl::Shutdown();
     subDevice->StopSectionHandler();
     delete subDevice;
     subDevice = NULL;
     isyslog("dynamite: deleted device for %s", (devpath ? **devpath : "(unknown)"));
     if (devpath)
        cPluginManager::CallAllServices("dynamite-event-DeviceDetached-v0.1", (void*)**devpath);
     }
  if (udevRemoveSyspath) {
     cUdevUsbRemoveFilter::RemoveItem(**udevRemoveSyspath, GetDevPath());
     delete udevRemoveSyspath;
     udevRemoveSyspath = NULL;
     }
  if (devpath) {
     delete devpath;
     devpath = NULL;
     }
  isDetachable = true;
  getTSTimeout = defaultGetTSTimeout;
}

bool cDynamicDevice::SetIdleDevice(bool Idle, bool TestOnly)
{
  if (subDevice)
     return subDevice->SetIdleDevice(Idle, TestOnly);
  return false;
}

bool cDynamicDevice::ProvidesEIT(void) const
{
  if (subDevice)
     return subDevice->ProvidesEIT();
  return false;
}

void cDynamicDevice::MakePrimaryDevice(bool On)
{
  if (subDevice)
     subDevice->MakePrimaryDevice(On);
  cDevice::MakePrimaryDevice(On);
}

bool cDynamicDevice::HasDecoder(void) const
{
  if (subDevice)
     return subDevice->HasDecoder();
  return cDevice::HasDecoder();
}

bool cDynamicDevice::AvoidRecording(void) const
{
  if (subDevice)
     return subDevice->AvoidRecording();
  return cDevice::AvoidRecording();
}

cSpuDecoder *cDynamicDevice::GetSpuDecoder(void)
{
  if (subDevice)
     return subDevice->GetSpuDecoder();
  return cDevice::GetSpuDecoder();
}

bool cDynamicDevice::HasCi(void)
{
  if (subDevice)
     return subDevice->HasCi();
  return cDevice::HasCi();
}

uchar *cDynamicDevice::GrabImage(int &Size, bool Jpeg, int Quality, int SizeX, int SizeY)
{
  if (subDevice)
     return subDevice->GrabImage(Size, Jpeg, Quality, SizeX, SizeY);
  return cDevice::GrabImage(Size, Jpeg, Quality, SizeX, SizeY);
}

void cDynamicDevice::SetVideoDisplayFormat(eVideoDisplayFormat VideoDisplayFormat)
{
  if (subDevice)
     return subDevice->SetVideoDisplayFormat(VideoDisplayFormat);
  cDevice::SetVideoDisplayFormat(VideoDisplayFormat);
}

void cDynamicDevice::SetVideoFormat(bool VideoFormat16_9)
{
  if (subDevice)
     return subDevice->SetVideoFormat(VideoFormat16_9);
  cDevice::SetVideoFormat(VideoFormat16_9);
}

eVideoSystem cDynamicDevice::GetVideoSystem(void)
{
  if (subDevice)
     return subDevice->GetVideoSystem();
  return cDevice::GetVideoSystem();
}

void cDynamicDevice::GetVideoSize(int &Width, int &Height, double &VideoAspect)
{
  if (subDevice)
     return subDevice->GetVideoSize(Width, Height, VideoAspect);
  cDevice::GetVideoSize(Width, Height, VideoAspect);
}

void cDynamicDevice::GetOsdSize(int &Width, int &Height, double &PixelAspect)
{
  if (subDevice)
     return subDevice->GetOsdSize(Width, Height, PixelAspect);
  cDevice::GetOsdSize(Width, Height, PixelAspect);
}

bool cDynamicDevice::SetPid(cPidHandle *Handle, int Type, bool On)
{
  if (subDevice)
     return subDevice->SetPid(Handle, Type, On);
  return cDevice::SetPid(Handle, Type, On);
}

int cDynamicDevice::OpenFilter(u_short Pid, u_char Tid, u_char Mask)
{
  if (subDevice)
     return subDevice->OpenFilter(Pid, Tid, Mask);
  return cDevice::OpenFilter(Pid, Tid, Mask);
}

void cDynamicDevice::CloseFilter(int Handle)
{
  if (subDevice)
     return subDevice->CloseFilter(Handle);
  cDevice::CloseFilter(Handle);
}

bool cDynamicDevice::ProvidesSource(int Source) const
{
  if (subDevice)
     return subDevice->ProvidesSource(Source);
  return cDevice::ProvidesSource(Source);
}

bool cDynamicDevice::ProvidesTransponder(const cChannel *Channel) const
{
  if (subDevice)
     return subDevice->ProvidesTransponder(Channel);
  return cDevice::ProvidesTransponder(Channel);
}

bool cDynamicDevice::ProvidesTransponderExclusively(const cChannel *Channel) const
{
  if (subDevice)
     return subDevice->ProvidesTransponderExclusively(Channel);
  return cDevice::ProvidesTransponderExclusively(Channel);
}

bool cDynamicDevice::ProvidesChannel(const cChannel *Channel, int Priority, bool *NeedsDetachReceivers) const
{
  if (subDevice)
     return subDevice->ProvidesChannel(Channel, Priority, NeedsDetachReceivers);
  return cDevice::ProvidesChannel(Channel, Priority, NeedsDetachReceivers);
}

int cDynamicDevice::NumProvidedSystems(void) const
{
  if (subDevice)
     return subDevice->NumProvidedSystems();
  return cDevice::NumProvidedSystems();
}

int cDynamicDevice::SignalStrength(void) const
{
  if (subDevice)
     return subDevice->SignalStrength();
  return cDevice::SignalStrength();
}

int cDynamicDevice::SignalQuality(void) const
{
  if (subDevice)
     return subDevice->SignalQuality();
  return cDevice::SignalQuality();
}

const cChannel *cDynamicDevice::GetCurrentlyTunedTransponder(void) const
{
  if (!IsIdle() && subDevice)
     return subDevice->GetCurrentlyTunedTransponder();
  return cDevice::GetCurrentlyTunedTransponder();
}

bool cDynamicDevice::IsTunedToTransponder(const cChannel *Channel)
{
  if (!IsIdle() && subDevice)
     return subDevice->IsTunedToTransponder(Channel);
  return cDevice::IsTunedToTransponder(Channel);
}

bool cDynamicDevice::MaySwitchTransponder(void)
{
  if (subDevice)
     return subDevice->MaySwitchTransponder();
  return cDevice::MaySwitchTransponder();
}

bool cDynamicDevice::SetChannelDevice(const cChannel *Channel, bool LiveView)
{
  if (subDevice)
     return subDevice->SetChannelDevice(Channel, LiveView);
  return cDevice::SetChannelDevice(Channel, LiveView);
}

bool cDynamicDevice::HasLock(int TimeoutMs)
{
  if (subDevice)
     return subDevice->HasLock(TimeoutMs);
  return cDevice::HasLock(TimeoutMs);
}

bool cDynamicDevice::HasProgramme(void)
{
  if (subDevice)
     return subDevice->HasProgramme();
  return cDevice::HasProgramme();
}

int cDynamicDevice::GetAudioChannelDevice(void)
{
  if (subDevice)
     return subDevice->GetAudioChannelDevice();
  return cDevice::GetAudioChannelDevice();
}

void cDynamicDevice::SetAudioChannelDevice(int AudioChannel)
{
  if (subDevice)
     return subDevice->SetAudioChannelDevice(AudioChannel);
  cDevice::SetAudioChannelDevice(AudioChannel);
}

void cDynamicDevice::SetVolumeDevice(int Volume)
{
  if (subDevice)
     return subDevice->SetVolumeDevice(Volume);
  cDevice::SetVolumeDevice(Volume);
}

void cDynamicDevice::SetDigitalAudioDevice(bool On)
{
  if (subDevice)
     return subDevice->SetDigitalAudioDevice(On);
  cDevice::SetDigitalAudioDevice(On);
}

void cDynamicDevice::SetAudioTrackDevice(eTrackType Type)
{
  if (subDevice)
     return subDevice->SetAudioTrackDevice(Type);
  cDevice::SetAudioTrackDevice(Type);
}

void cDynamicDevice::SetSubtitleTrackDevice(eTrackType Type)
{
  if (subDevice)
     return subDevice->SetSubtitleTrackDevice(Type);
  cDevice::SetSubtitleTrackDevice(Type);
}

bool cDynamicDevice::CanReplay(void) const
{
  if (subDevice)
     return subDevice->CanReplay();
  return cDevice::CanReplay();
}

bool cDynamicDevice::SetPlayMode(ePlayMode PlayMode)
{
  if (subDevice)
     return subDevice->SetPlayMode(PlayMode);
  return cDevice::SetPlayMode(PlayMode);
}

int64_t cDynamicDevice::GetSTC(void)
{
  if (subDevice)
     return subDevice->GetSTC();
  return cDevice::GetSTC();
}

bool cDynamicDevice::IsPlayingVideo(void) const
{
  if (subDevice)
     return subDevice->IsPlayingVideo();
  return cDevice::IsPlayingVideo();
}

bool cDynamicDevice::HasIBPTrickSpeed(void)
{
  if (subDevice)
     return subDevice->HasIBPTrickSpeed();
  return cDevice::HasIBPTrickSpeed();
}

void cDynamicDevice::TrickSpeed(int Speed)
{
  if (subDevice)
     return subDevice->TrickSpeed(Speed);
  cDevice::TrickSpeed(Speed);
}

void cDynamicDevice::Clear(void)
{
  if (subDevice)
     return subDevice->Clear();
  cDevice::Clear();
}

void cDynamicDevice::Play(void)
{
  if (subDevice)
     return subDevice->Play();
  cDevice::Play();
}

void cDynamicDevice::Freeze(void)
{
  if (subDevice)
     return subDevice->Freeze();
  cDevice::Freeze();
}

void cDynamicDevice::Mute(void)
{
  if (subDevice)
     return subDevice->Mute();
  cDevice::Mute();
}

void cDynamicDevice::StillPicture(const uchar *Data, int Length)
{
  if (subDevice)
     return subDevice->StillPicture(Data, Length);
  cDevice::StillPicture(Data, Length);
}

bool cDynamicDevice::Poll(cPoller &Poller, int TimeoutMs)
{
  if (subDevice)
     return subDevice->Poll(Poller, TimeoutMs);
  return cDevice::Poll(Poller, TimeoutMs);
}

bool cDynamicDevice::Flush(int TimeoutMs)
{
  if (subDevice)
     return subDevice->Flush(TimeoutMs);
  return cDevice::Flush(TimeoutMs);
}

int cDynamicDevice::PlayVideo(const uchar *Data, int Length)
{
  if (subDevice)
     return subDevice->PlayVideo(Data, Length);
  return cDevice::PlayVideo(Data, Length);
}

int cDynamicDevice::PlayAudio(const uchar *Data, int Length, uchar Id)
{
  if (subDevice)
     return subDevice->PlayAudio(Data, Length, Id);
  return cDevice::PlayAudio(Data, Length, Id);
}

int cDynamicDevice::PlaySubtitle(const uchar *Data, int Length)
{
  if (subDevice)
     return subDevice->PlaySubtitle(Data, Length);
  return cDevice::PlaySubtitle(Data, Length);
}

int cDynamicDevice::PlayPesPacket(const uchar *Data, int Length, bool VideoOnly)
{
  if (subDevice)
     return subDevice->PlayPesPacket(Data, Length, VideoOnly);
  return cDevice::PlayPesPacket(Data, Length, VideoOnly);
}

int cDynamicDevice::PlayPes(const uchar *Data, int Length, bool VideoOnly)
{
  if (subDevice)
     return subDevice->PlayPes(Data, Length, VideoOnly);
  return cDevice::PlayPes(Data, Length, VideoOnly);
}

int cDynamicDevice::PlayTsVideo(const uchar *Data, int Length)
{
  if (subDevice)
     return subDevice->PlayTsVideo(Data, Length);
  return cDevice::PlayTsVideo(Data, Length);
}

int cDynamicDevice::PlayTsAudio(const uchar *Data, int Length)
{
  if (subDevice)
     return subDevice->PlayTsAudio(Data, Length);
  return cDevice::PlayTsAudio(Data, Length);
}

int cDynamicDevice::PlayTsSubtitle(const uchar *Data, int Length)
{
  if (subDevice)
     return subDevice->PlayTsSubtitle(Data, Length);
  return cDevice::PlayTsSubtitle(Data, Length);
}

int cDynamicDevice::PlayTs(const uchar *Data, int Length, bool VideoOnly)
{
  if (subDevice)
     return subDevice->PlayTs(Data, Length, VideoOnly);
  return cDevice::PlayTs(Data, Length, VideoOnly);
}

bool cDynamicDevice::Ready(void)
{
  if (subDevice)
     return subDevice->Ready();
  return cDevice::Ready();
}

bool cDynamicDevice::OpenDvr(void)
{
  lastCloseDvr = 0;
  if (subDevice) {
     getTSWatchdog = 0;
     return subDevice->OpenDvr();
     }
  return cDevice::OpenDvr();
}

void cDynamicDevice::CloseDvr(void)
{
  lastCloseDvr = time(NULL);
  if (subDevice)
     return subDevice->CloseDvr();
  cDevice::CloseDvr();
}

bool cDynamicDevice::GetTSPacket(uchar *&Data)
{
  if (subDevice) {
     bool r = subDevice->GetTSPacket(Data);
     if (getTSTimeout > 0) {
        if (Data == NULL) {
           if (getTSWatchdog == 0)
              getTSWatchdog = time(NULL);
           else if ((time(NULL) - getTSWatchdog) > getTSTimeout) {
              const char *d = NULL;
              if (devpath)
                 d = **devpath;
              esyslog("dynamite: device %s hasn't delivered any data for %d seconds, detaching all receivers", d, getTSTimeout);
              subDevice->DetachAllReceivers();
              cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcDetach, *devpath);
              const char *timeoutHandlerArg = *devpath;
              if (getTSTimeoutHandlerArg)
                 timeoutHandlerArg = **getTSTimeoutHandlerArg;
              cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcService, *cString::sprintf("dynamite-CallGetTSTimeoutHandler-v0.1 %s", timeoutHandlerArg));
              return false;
              }
           }
        else
           getTSWatchdog = 0;
        }
     return r;
     }
  return cDevice::GetTSPacket(Data);
}

#ifdef YAVDR_PATCHES
//opt-21_internal-cam-devices.dpatch
bool cDynamicDevice::HasInternalCam(void)
{
  if (subDevice)
     return subDevice->HasInternalCam();
  return cDevice::HasInternalCam();
}

//opt-44_rotor.dpatch 
bool cDynamicDevice::SendDiseqcCmd(dvb_diseqc_master_cmd cmd)
{
  if (subDevice)
     return subDevice->SendDiseqcCmd(cmd);
  return cDevice::SendDiseqcCmd(cmd);
}

//opt-64_lnb-sharing.dpatch 
void cDynamicDevice::SetLnbNrFromSetup(void)
{
  if (subDevice)
     return subDevice->SetLnbNrFromSetup();
  cDevice::SetLnbNrFromSetup();
}

int cDynamicDevice::LnbNr(void) const
{
  if (subDevice)
     return subDevice->LnbNr();
  return cDevice::LnbNr();
}

bool cDynamicDevice::IsShareLnb(const cDevice *Device)
{
  if (subDevice)
     return subDevice->IsShareLnb(Device);
  return cDevice::IsShareLnb(Device);
}

bool cDynamicDevice::IsLnbConflict(const cChannel *Channel)
{
  if (subDevice)
     return subDevice->IsLnbConflict(Channel);
  return cDevice::IsLnbConflict(Channel);
}
#endif
