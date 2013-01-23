/*
 * dynamite.c: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 */

#include <getopt.h>
#include <vdr/plugin.h>
#include "dynamicdevice.h"
#include "menu.h"
#include "monitor.h"
#include "status.h"

static const char *VERSION        = "0.1.2";
static const char *DESCRIPTION    = tr("attach/detach devices on the fly");
static const char *MAINMENUENTRY  = NULL;

class cDynamiteDvbDeviceProbe : public cDvbDeviceProbe {
private:
  static bool firstProbe;
public:
  virtual bool Probe(int Adapter, int Frontend)
  {
    if (firstProbe) {
       firstProbe = false;
       isyslog("dynamite: preparing %d dynamic device slots for dvb devices", MAXDVBDEVICES);
       while (cDevice::NumDevices() < MAXDVBDEVICES)
             new cDynamicDevice;
       }
    cString devpath = cString::sprintf("/dev/dvb/adapter%d/frontend%d", Adapter, Frontend);
    int freeIndex = -1;
    if (cDynamicDevice::IndexOf(*devpath, freeIndex, -1) >= 0) // already attached - should not happen
       return true;
    if (freeIndex < 0) {
       if ((cDevice::NumDevices() >= MAXDEVICES) || (cDynamicDevice::NumDynamicDevices() >= MAXDEVICES)) {
          esyslog("dynamite: too many dvb-devices, vdr supports only %d devices - increase MAXDEVICES and recompile vdr", MAXDEVICES);
          return false;
          }
       new cDynamicDevice;
       }
    isyslog("dynamite: grab dvb device %d/%d", Adapter, Frontend);
    //cDynamicDevice::AttachDevice(*devpath);
    // or better attach later when all plugins are started?
    cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcAttach, *devpath);
    return true; // grab all dvbdevices
  }
  };

bool cDynamiteDvbDeviceProbe::firstProbe = true;
  
class cDynamiteDeviceProbe : public cDynamicDeviceProbe {
private:
  class cDummyDevice: public cDevice {
  public:
    cDummyDevice(cDevice *ParentDevice):cDevice(ParentDevice) {}
    virtual ~cDummyDevice() {};
    };
public:
  virtual cDevice *Attach(cDevice *ParentDevice, const char *DevPath)
  {
    int nr;
    if (sscanf(DevPath, "dummydevice%d", &nr) == 1)
       return new cDummyDevice(ParentDevice);
    return NULL;
  }
  };

class cPluginDynamite : public cPlugin {
private:
  cDynamiteDeviceProbe *probe;
  cString *getTSTimeoutHandler;
  int  freeDeviceSlots;
  int  lastHousekeeping;
public:
  cPluginDynamite(void);
  virtual ~cPluginDynamite();
  virtual const char *Version(void) { return VERSION; }
  virtual const char *Description(void) { return DESCRIPTION; }
  virtual const char *CommandLineHelp(void);
  virtual bool ProcessArgs(int argc, char *argv[]);
  virtual bool Initialize(void);
  virtual bool Start(void);
  virtual void Stop(void);
  virtual void Housekeeping(void);
  virtual void MainThreadHook(void);
  virtual cString Active(void);
  virtual time_t WakeupTime(void);
  virtual const char *MainMenuEntry(void) { return MAINMENUENTRY; }
  virtual cOsdObject *MainMenuAction(void);
  virtual cMenuSetupPage *SetupMenu(void);
  virtual bool SetupParse(const char *Name, const char *Value);
  virtual bool Service(const char *Id, void *Data = NULL);
  virtual const char **SVDRPHelpPages(void);
  virtual cString SVDRPCommand(const char *Command, const char *Option, int &ReplyCode);
  };

cPluginDynamite::cPluginDynamite(void)
:probe(NULL)
,getTSTimeoutHandler(NULL)
,freeDeviceSlots(0)
,lastHousekeeping(0)
{
  cDynamicDevice::dynamite = this;
  cDynamicDevice::dvbprobe = new cDynamiteDvbDeviceProbe;
  // make sure we're the first one who cares for dvbdevices
  cDvbDeviceProbe *firstDvbProbe = DvbDeviceProbes.First();
  if (firstDvbProbe != cDynamicDevice::dvbprobe)
     DvbDeviceProbes.Move(cDynamicDevice::dvbprobe, firstDvbProbe);
  cUdevMonitor::AddFilter("dvb", new cUdevDvbFilter());
}

cPluginDynamite::~cPluginDynamite()
{
  cDynamiteStatus::DeInit();
  cUdevMonitor::ShutdownAllMonitors();
  cUdev::Free();
  if (cDynamicDevice::idleHook != NULL) {
     delete cDynamicDevice::idleHook;
     cDynamicDevice::idleHook = NULL;
     }
  if (cDynamicDevice::attachHook != NULL) {
     delete cDynamicDevice::attachHook;
     cDynamicDevice::attachHook = NULL;
     }
  if (cDynamicDevice::dvbprobe) {
     delete cDynamicDevice::dvbprobe;
     cDynamicDevice::dvbprobe = NULL;
     }
  if (probe) {
     delete probe;
     probe = NULL;
     }
  if (getTSTimeoutHandler != NULL) {
     delete getTSTimeoutHandler;
     getTSTimeoutHandler = NULL;
     }
}

const char *cPluginDynamite::CommandLineHelp(void)
{
  return "  --log-udev\n"
         "    log all udev events to syslog (useful for diagnostics)\n"
         "  --dummy-probe\n"
         "    start dummy-device probe\n"
         "  --GetTSTimeoutHandler=/path/to/program\n"
         "    set program to be called on GetTS-timeout\n"
         "  --free-device-slots=n\n"
         "    leave n slots free for non-dynamic devices\n"
         "  --attach-hook=/path/to/program\n"
         "    set program to be called on device attach\n"
         "  --idle-hook=/path/to/program\n"
         "    set program to be called on SetIdle and reactivation\n"
         "  --idle-timeout=m\n"
         "    if a device is unused for m minutes set it to idle, 0 disables auto-idle (default)\n"
         "  --idle-wakeup=h\n"
         "    if a device is idle for h hours wake it up (e.g. for EPG scan)";
}

bool cPluginDynamite::ProcessArgs(int argc, char *argv[])
{
  static struct option options[] =
  {
    {"log-udev", no_argument, 0, 'u'},
    {"dummy-probe", no_argument, 0, 'd'},
    {"GetTSTimeout", required_argument, 0, 't'},
    {"GetTSTimeoutHandler", required_argument, 0, 'h'},
    {"free-device-slots", required_argument, 0, 'f'},
    {"attach-hook", required_argument, 0, 'a'},
    {"idle-hook", required_argument, 0, 'i'},
    {"idle-timeout", required_argument, 0, 'I'},
    {"idle-wakeup", required_argument, 0, 'W'},
    {0, 0, 0, 0}
  };

  while (true) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "udt:h:f:a:i:I:W:", options, &option_index);
        if (c == -1)
           break;
        switch (c) {
          case 'u':
           {
             isyslog("dynamite: activate udev-logging");
             cUdevMonitor::AddFilter(NULL, new cUdevLogFilter());
             break;
           }
          case 'd':
           {
             isyslog("dynamite: activate dummy-device-probe");
             probe = new cDynamiteDeviceProbe;
             break;
           }
          case 't':
           {
             if ((optarg != NULL) && isnumber(optarg))
                cDynamicDevice::SetDefaultGetTSTimeout(strtol(optarg, NULL, 10));
             break;
           }
          case 'h':
           {
             if (getTSTimeoutHandler != NULL)
                delete getTSTimeoutHandler;
             getTSTimeoutHandler = NULL;
             if (optarg != NULL) {
                getTSTimeoutHandler = new cString(optarg);
                isyslog("dynamite: installing GetTS-Timeout-Handler %s", **getTSTimeoutHandler);
                }
             break;
           }
          case 'f':
           {
             if ((optarg != NULL) && isnumber(optarg)) {
                int tmp = strtol(optarg, NULL, 10);
                if ((tmp >= 0) && (tmp < MAXDEVICES))
                   freeDeviceSlots = tmp;
                else
                   esyslog("dynamite: \"%d\" free device slots is out of range", tmp);
                }
             break;
           }
          case 'a':
           {
             if (cDynamicDevice::attachHook != NULL)
                delete cDynamicDevice::attachHook;
             cDynamicDevice::attachHook = NULL;
             if (optarg != NULL) {
                cDynamicDevice::attachHook = new cString(optarg);
                isyslog("dynamite: installing attach-hook %s", **cDynamicDevice::attachHook);
                }
             break;
           }
          case 'i':
           {
             if (cDynamicDevice::idleHook != NULL)
                delete cDynamicDevice::idleHook;
             cDynamicDevice::idleHook = NULL;
             if (optarg != NULL) {
                cDynamicDevice::idleHook = new cString(optarg);
                isyslog("dynamite: installing idle-hook %s", **cDynamicDevice::idleHook);
                }
             break;
           }
          case 'I':
           {
             if ((optarg != NULL) && isnumber(optarg)) {
                int tmp = strtol(optarg, NULL, 10);
                if (tmp >= 0) {
                   cDynamicDevice::idleTimeoutMinutes = tmp;
                   if (tmp == 0)
                      isyslog("dynamite: disable auto-idle-mode");
                   else
                      isyslog("dynamite: setting auto-idle-timeout to %d minute%s", cDynamicDevice::idleTimeoutMinutes, (cDynamicDevice::idleTimeoutMinutes > 1) ? "s" : "");
                   }
                }
             break;
           }
          case 'W':
           {
             if ((optarg != NULL) && isnumber(optarg)) {
                int tmp = strtol(optarg, NULL, 10);
                if (tmp > 0) {
                   cDynamicDevice::idleWakeupHours = tmp;
                   isyslog("dynamite: setting auto-idle-wakeup to %d hour%s", cDynamicDevice::idleWakeupHours, (cDynamicDevice::idleWakeupHours > 1) ? "s" : "");
                   }
                }
             break;
           }
          }
        }
  return true;
}

bool cPluginDynamite::Initialize(void)
{
  static const char *badPlugins[] = {"streamdev-client", "mcli", NULL};
  static int         badPluginsFreeCount[] = {1, 8};
  int freeSlotsForKnownBadPlugins = 0;
  for (int i = 0; badPlugins[i]; i++) {
      if (cPluginManager::GetPlugin(badPlugins[i]) != NULL) {
         isyslog("dynamite: %s detected, leaving %d additional slot(s) free", badPlugins[i], badPluginsFreeCount[i]);
         freeSlotsForKnownBadPlugins+= badPluginsFreeCount[i];
         }
      }
  // create dynamic devices
  if (cDevice::NumDevices() < (MAXDEVICES - freeDeviceSlots - freeSlotsForKnownBadPlugins)) {
     isyslog("dynamite: creating dynamic device slots as much as possible");
     while (cDevice::NumDevices() < (MAXDEVICES - freeDeviceSlots - freeSlotsForKnownBadPlugins))
           new cDynamicDevice;
     }
  // look for all dvb devices
  cList<cUdevDevice> *devices = cUdev::EnumDevices("dvb", "DVB_DEVICE_TYPE", "frontend");
  if (devices != NULL) {
     devices->Sort();
     int dummy = 0;
     for (cUdevDevice *d = devices->First(); d; d = devices->Next(d)) {
         const char *devpath = d->GetDevnode();
         if ((devpath != NULL) && (cDynamicDevice::IndexOf(devpath, dummy, -1) < 0)) {
            isyslog("dynamite: probing %s", devpath);
            cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcAttach, devpath);
            }
         }
     delete devices;
     }

  if (!cDynamicDevice::ProcessQueuedCommands())
     esyslog("dynamite: can't process all queued commands");
  return true;
}

bool cPluginDynamite::Start(void)
{
  cDynamiteStatus::Init();

  if (!cDynamicDevice::ProcessQueuedCommands())
     esyslog("dynamite: can't process all queued commands");
  return true;
}

void cPluginDynamite::Stop(void)
{
  cDynamicDevice::DetachAllDevices(true);
}

void cPluginDynamite::Housekeeping(void)
{
  int now = time(NULL);
  if (lastHousekeeping == 0)
     lastHousekeeping = now;
  else if ((now - lastHousekeeping) > 60) {
     cDynamicDevice::AutoIdle();
     lastHousekeeping = now;
     }
}

void cPluginDynamite::MainThreadHook(void)
{
  // Perform actions in the context of the main program thread.
  // WARNING: Use with great care - see PLUGINS.html!
  if (!cDynamicDevice::ProcessQueuedCommands())
     esyslog("dynamite: can't process all queued commands");
  if (!cDynamicDevice::enableOsdMessages)
     cDynamicDevice::enableOsdMessages = true;
}

cString cPluginDynamite::Active(void)
{
  // Return a message string if shutdown should be postponed
  return NULL;
}

time_t cPluginDynamite::WakeupTime(void)
{
  // Return custom wakeup time for shutdown script
  return 0;
}

cOsdObject *cPluginDynamite::MainMenuAction(void)
{
  // Perform the action when selected from the main VDR menu.
  return NULL;
}

cMenuSetupPage *cPluginDynamite::SetupMenu(void)
{
  // Return a setup menu in case the plugin supports one.
  return new cDynamiteMainMenu;
}

bool cPluginDynamite::SetupParse(const char *Name, const char *Value)
{
  int replyCode;
  if (strcasecmp(Name, "DefaultGetTSTimeout") == 0)
     SVDRPCommand("SetDefaultGetTSTimeout", Value, replyCode);
  else if (strcasecmp(Name, "GetTSTimeoutHandler") == 0) {
     if (getTSTimeoutHandler != NULL)
        delete getTSTimeoutHandler;
     getTSTimeoutHandler = NULL;
     if (Value != NULL) {
        getTSTimeoutHandler = new cString(Value);
        isyslog("dynamite: installing GetTS-Timeout-Handler %s", **getTSTimeoutHandler);
        }
     }
  else if (strcasecmp(Name, "FreeDeviceSlots") == 0) {
     int tmp = strtol(Value, NULL, 10);
     if ((tmp >= 0) && (tmp < MAXDEVICES))
        freeDeviceSlots = tmp;
     else
        esyslog("dynamite: \"%d\" free device slots is out of range", tmp);
     }
  else if (strcasecmp(Name, "AttachHook") == 0) {
     if (cDynamicDevice::attachHook != NULL)
        delete cDynamicDevice::attachHook;
     cDynamicDevice::attachHook = NULL;
     if (Value != NULL) {
        cDynamicDevice::attachHook = new cString(Value);
        isyslog("dynamite: installing attach-hook %s", **cDynamicDevice::attachHook);
        }
     }
  else if (strcasecmp(Name, "IdleHook") == 0) {
     if (cDynamicDevice::idleHook != NULL)
        delete cDynamicDevice::idleHook;
     cDynamicDevice::idleHook = NULL;
     if (Value != NULL) {
        cDynamicDevice::idleHook = new cString(Value);
        isyslog("dynamite: installing idle-hook %s", **cDynamicDevice::idleHook);
        }
     }
  else if (strcasecmp(Name, "IdleTimeout") == 0) {
     int tmp = strtol(Value, NULL, 10);
     if (tmp >= 0)
        cDynamicDevice::idleTimeoutMinutes = tmp;
     }
  else if (strcasecmp(Name, "IdleWakeup") == 0) {
     int tmp = strtol(Value, NULL, 10);
     if (tmp >= 0)
        cDynamicDevice::idleWakeupHours = tmp;
     }
  else
     return false;
  return true;
}

bool cPluginDynamite::Service(const char *Id, void *Data)
{
  if (strcmp(Id, "dynamite-AttachDevice-v0.1") == 0) {
     if (Data != NULL)
        cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcAttach, (const char*)Data);
     return true;
     }
  if (strcmp(Id, "dynamite-ScanDevices-v0.1") == 0) {
     if (Data != NULL)
        cDynamicDevice::AttachDevicePattern((const char*)Data);
     return true;
     }
  if (strcmp(Id, "dynamite-DetachDevice-v0.1") == 0) {
     if (Data != NULL)
        cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcDetach, (const char*)Data);
     return true;
     }
  if (strcmp(Id, "dynamite-ForceDetachDevice-v0.1") == 0) {
     if (Data != NULL)
        cDynamicDevice::DetachDevice((const char*)Data, true);
     return true;
     }
  if (strcmp(Id, "dynamite-DetachAllDevices-v0.1") == 0) {
     if (Data != NULL)
        cDynamicDevice::DetachAllDevices((strcasecmp((const char*)Data, "force") == 0));
     return true;
     }
  if (strcmp(Id, "dynamite-LockDevice-v0.1") == 0) {
     if (Data != NULL)
        cDynamicDevice::SetLockDevice((const char*)Data, true);
     return true;
     }
  if (strcmp(Id, "dynamite-UnlockDevice-v0.1") == 0) {
     if (Data != NULL)
        cDynamicDevice::SetLockDevice((const char*)Data, false);
     return true;
     }
  if (strcmp(Id, "dynamite-SetIdle-v0.1") == 0) {
     if (Data != NULL)
        cDynamicDevice::SetIdle((const char*)Data, true);
     return true;
     }
  if (strcmp(Id, "dynamite-SetNotIdle-v0.1") == 0) {
     if (Data != NULL)
        cDynamicDevice::SetIdle((const char*)Data, false);
     return true;
     }
  if (strcmp(Id, "dynamite-DisableAutoIdle-v0.1") == 0) {
     if (Data != NULL)
        cDynamicDevice::SetAutoIdle((const char*)Data, true);
     return true;
     }
  if (strcmp(Id, "dynamite-EnableAutoIdle-v0.1") == 0) {
     if (Data != NULL)
        cDynamicDevice::SetAutoIdle((const char*)Data, false);
     return true;
     }
  if (strcmp(Id, "dynamite-SetGetTSTimeout-v0.1") == 0) {
     if (Data != NULL) {
        int replyCode;
        SVDRPCommand("SetGetTSTimeout", (const char*)Data, replyCode);
        }
     return true;
     }
  if (strcmp(Id, "dynamite-SetDefaultGetTSTimeout-v0.1") == 0) {
     if (Data != NULL) {
        int replyCode;
        SVDRPCommand("SetDefaultGetTSTimeout", (const char*)Data, replyCode);
        }
     return true;
     }
  if (strcmp(Id, "dynamite-AddUdevMonitor-v0.1") == 0) {
     if (Data != NULL) {
        int replyCode;
        SVDRPCommand("AddUdevMonitor", (const char*)Data, replyCode);
        }
     return true;
     }
  if (strcmp(Id, "dynamite-SetGetTSTimeoutHandlerArg-v0.1") == 0) {
     if (Data != NULL) {
        int replyCode;
        SVDRPCommand("SetGetTSTimeoutHandlerArg", (const char*)Data, replyCode);
        }
     return true;
     }
  if (strcmp(Id, "dynamite-CallGetTSTimeoutHandler-v0.1") == 0) {
     if (Data != NULL) {
        int replyCode;
        SVDRPCommand("CallGetTSTimeoutHandler", (const char*)Data, replyCode);
        }
     return true;
     }
  if (strcmp(Id, "dynamite-SetIdleTimeout-v0.1") == 0) {
     if (Data != NULL) {
        int replyCode;
        SVDRPCommand("SetIdleTimeout", (const char*)Data, replyCode);
        }
     return true;
     }
  if (strcmp(Id, "dynamite-SetIdleWakeup-v0.1") == 0) {
     if (Data != NULL) {
        int replyCode;
        SVDRPCommand("SetIdleWakeup", (const char*)Data, replyCode);
        }
     return true;
     }
  return false;
}

const char **cPluginDynamite::SVDRPHelpPages(void)
{
  static const char *HelpPages[] = {
    "ATTD devpath\n"
    "    Asks all cDynamicDeviceProbe-objects to attach the given devicepath\n"
    "    till one returns a valid pointer. You can control the order by the\n"
    "    order of the plugins you load\n"
    "    e.g. /dev/dvb/adapter0/frontend0\n"
    "    alternate command: AttachDevice",
    "DETD devpath\n"
    "    Looks through its remembered devicepaths and deletes the attached\n"
    "    device if found. Case is important!\n"
    "    Any timeouts or locks set to this slot will be reset to its defaults\n"
    "    alternate command: DetachDevice",
    "FDTD devpath\n"
    "    Looks through its remembered devicepaths and deletes the attached\n"
    "    device if found. Case is important!\n"
    "    The device will be detached regardless of recordings or other locks!\n"
    "    This is useful for unplugged usb-sticks etc.\n"
    "    alternate command: ForceDetachDevice",
    "DTAD [force]\n"
    "    detachs all attached devices\n"
    "    \"force\" will ignore recordings and other receivers\n"
    "    alternate command: DetachAllDevices",
    "SCND '/dev/path/glob*/pattern*'\n"
    "    Scan filesystem with pattern and try to attach each found device\n"
    "    don't forget to enclose the pattern with single quotes\n"
    "    e.g. SCND '/dev/dvb/adapter*/frontend*'\n"
    "    alternate command: ScanDevices",
    "LCKD /dev/path/to/device\n"
    "    Lock the device so it can't be detached\n"
    "    alternate command: LockDevice",
    "UNLD /dev/path/to/device\n"
    "    Remove the lock of the device so it can be detached\n"
    "    alternate command: UnlockDevice",
    "SetIdle /dev/path/to/device\n"
    "    Try to set the device to idle so it won't be used by epg-scan\n"
    "    and can close all its handles",
    "SetNotIdle /dev/path/to/device\n"
    "    Revoke the idle state of the device",
    "DisableAutoIdle /dev/path/to/device\n"
    "    disables the auto-idle mode on this device if configured",
    "EnableAutoIdle /dev/path/to/device\n"
    "    enables the auto-idle mode on this device if configured",
    "LSTD\n"
    "    Lists all devices managed by this plugin. The first column is an id,\n"
    "    the second column is the devicepath passed with ATTD\n"
    "    The id can be used with the DETD and UNLD commands instead of the path.\n"
    "    An asterisk behind the id means that this device is locked and\n"
    "    can't be detached.\n"
    "    alternate command: ListDevices",
    "SGTT /dev/path/to/device seconds\n"
    "    Sets the \"GetTSPacket\"-watchdog timeout to specified amount of seconds\n"
    "    If the device returns no data (Data == NULL) for this period of time,\n"
    "    the device will be detached. Usefull if you want to reload the driver etc.\n"
    "    A value of 0 seconds disables the watchdog.\n"
    "    alternate command: SetGetTSTimeout",
    "SDGT seconds\n"
    "    Sets the \"GetTSPacket\"-watchdog timeout for all attached devices\n"
    "    and all devices that will be attached.\n"
    "    alternate command: SetDefaultGetTSTimeout",
    "ADUM subsystem begin-of-devnode\n"
    "    Adds a filter to the udev-monitor.\n"
    "    If an event occurs whose devnode starts with the supplied parameter\n"
    "    this devnode will be queued for attaching, e.g.\n"
    "    AddUdevMonitor video4linux /dev/video\n"
    "    (this is what pvrinput uses)\n"
    "    alternate command: AddUdevMonitor",
    "SetGetTSTimeoutHandlerArg /dev/path/to/device arg\n"
    "    Sets the argument for the timout handler program.",
    "CallGetTSTimeoutHandler arg\n"
    "    Calls the timout handler program with the given arguments.",
    "SIDT minutes\n"
    "    Sets the timeout for an used device to be switched into idle mode,\n"
    "    a value of 0 will deactivate the auto-idle-mode.\n"
    "    alternate command: SetIdleTimeout",
    "SIDW hours\n"
    "    Sets the timeout for an idle device to be reactivated,\n"
    "    a lower interval than 1 hour is not possible.\n"
    "    alternate command: SetIdleWakeup",
    NULL
    };
  return HelpPages;
}

cString cPluginDynamite::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode)
{
  if ((strcasecmp(Command, "ATTD") == 0) || (strcasecmp(Command, "AttachDevice") == 0)) {
     cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcAttach, Option);
     return cString::sprintf("queued command for attaching %s", Option);
     }

  if ((strcasecmp(Command, "DETD") == 0) || (strcasecmp(Command, "DetachDevice") == 0)) {
     cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcDetach, Option);
     return cString::sprintf("queued command for detaching %s", Option);
     }

  if ((strcasecmp(Command, "FDTD") == 0) || (strcasecmp(Command, "ForceDetachDevice") == 0)) {
     cDynamicDevice::DetachDevice(Option, true);
     return cString::sprintf("forced detaching of %s", Option);
     }

  if ((strcasecmp(Command, "DTAD") == 0) || (strcasecmp(Command, "DetachAllDevices") == 0)) {
     bool force = false;
     if (Option && (strcasecmp(Option, "force") == 0))
        force = true;
     cDynamicDevice::DetachAllDevices(force);
     return cString::sprintf("detaching all devices...");
     }

  if ((strcasecmp(Command, "SCND") == 0) || (strcasecmp(Command, "ScanDevices") == 0))
     return cDynamicDevice::AttachDevicePattern(Option);

  if ((strcasecmp(Command, "LSTD") == 0) || (strcasecmp(Command, "ListDevices") == 0))
     return cDynamicDevice::ListAllDevices(ReplyCode);

  int lock = 0;
  if ((strcasecmp(Command, "LCKD") == 0) || (strcasecmp(Command, "LockDevice") == 0))
     lock = 1;
  else if ((strcasecmp(Command, "UNLD") == 0) || (strcasecmp(Command, "UnlockDevice") == 0))
     lock = 2;
  if (lock > 0) {
     switch (cDynamicDevice::SetLockDevice(Option, lock == 1)) {
       case ddrcSuccess:
         return cString::sprintf("%slocked device %s", (lock == 2) ? "un" : "", Option);
       case ddrcNotFound:
        {
          ReplyCode = 550;
          return cString::sprintf("device %s not found", Option);
        }
       default:
        {
          ReplyCode = 550;
          return cString::sprintf("can't %slock device %s and I don't know why...", (lock == 2) ? "un" : "", Option);
        }
       }
     }

  int idle = 0;
  if (strcasecmp(Command, "SetIdle") == 0)
     idle = 1;
  else if (strcasecmp(Command, "SetNotIdle") == 0)
     idle = 2;
  if (idle > 0) {
     switch (cDynamicDevice::SetIdle(Option, (idle == 1))) {
       case ddrcSuccess:
         return cString::sprintf("device %s is %s", Option, (idle == 1 ? "idle" : "not idle"));
       case ddrcNotFound:
        {
          ReplyCode = 550;
          return cString::sprintf("device %s not found", Option);
        }
       default:
        {
          ReplyCode = 550;
          return cString::sprintf("can't set device %s to %s and I don't know why...", Option, (idle == 1 ? "idle" : "not idle"));
        }
       }
     }

  int autoidle = 0;
  if (strcasecmp(Command, "DisableAutoIdle") == 0)
     autoidle = 1;
  else if (strcasecmp(Command, "EnableAutoIdle") == 0)
     autoidle = 2;
  if (autoidle > 0) {
     switch (cDynamicDevice::SetAutoIdle(Option, (autoidle == 1))) {
       case ddrcSuccess:
         return cString::sprintf("%s auto-idle mode on device %s", (autoidle == 1 ? "disabled" : "enabled"), Option);
       case ddrcNotFound:
        {
          ReplyCode = 550;
          return cString::sprintf("device %s not found", Option);
        }
       default:
        {
          ReplyCode = 550;
          return cString::sprintf("can't %s auto-idle mode on device %s and I don't know why...", (autoidle == 1 ? "disable" : "enable"), Option);
        }
       }
     }

  if ((strcasecmp(Command, "SGTT") == 0) || (strcasecmp(Command, "SetGetTSTimeout") == 0)) {
     cString ret;
     int len = strlen(Option);
     if (len > 0) {
        char *devPath = new char[len];
        int seconds = -1;
        if ((sscanf(Option, "%s %d", devPath, &seconds) == 2) && (seconds >= 0)) {
           cDynamicDevice::SetGetTSTimeout(devPath, seconds);
           if (seconds > 0)
              ret = cString::sprintf("set GetTS-Timeout on device %s to %d seconds", devPath, seconds);
           else
              ret = cString::sprintf("disable GetTS-Timeout on device %s", devPath);
           }
        delete [] devPath;
        }
     return ret;
     }

  if ((strcasecmp(Command, "SDGT") == 0) || (strcasecmp(Command, "SetDefaultGetTSTimeout") == 0)) {
     if (isnumber(Option)) {
        int seconds = strtol(Option, NULL, 10);
        cDynamicDevice::SetDefaultGetTSTimeout(seconds);
        return cString::sprintf("set default GetTS-Timeout on all devices to %d seconds", seconds);
        }
     }

  if ((strcasecmp(Command, "AUDM") == 0) || (strcasecmp(Command, "AddUdevMonitor") == 0)) {
     int maxlen = strlen(Option);
     if (maxlen > 0) {
        char *subsystem = new char[maxlen + 1];
        char *devnode = new char[maxlen + 1];
        subsystem[0] = '\0';
        devnode[0] = '\0';
        cString msg;
        if ((sscanf(Option, "%s %s", subsystem, devnode) == 2) && cUdevPatternFilter::AddFilter(subsystem, devnode))
           msg = cString::sprintf("add udev-filter for %s %s", subsystem, devnode);
        else {
           ReplyCode = 550;
           msg = cString::sprintf("can't add udev-filter for %s %s", subsystem, devnode);
           }
        delete [] subsystem;
        delete [] devnode;
        return msg;
        }
     }

  if (strcasecmp(Command, "SetGetTSTimeoutHandlerArg") == 0) {
     cString ret;
     int len = strlen(Option);
     if (len > 0) {
        cString devPath(Option);
        const char *arg = strchr(*devPath, ' ');
        if (arg && (arg < (*devPath + len + 1))) {
           devPath.Truncate(arg - *devPath);
           arg++;
           cDynamicDevice::SetGetTSTimeoutHandlerArg(*devPath, arg);
           ret = cString::sprintf("set GetTS-Timeout-Handler-Arg on device %s to %s", *devPath, arg);
           }
        }
     return ret;
     }

  if (strcasecmp(Command, "CallGetTSTimeoutHandler") == 0) {
     if (getTSTimeoutHandler == NULL) {
        cString msg = cString::sprintf("no GetTSTimeoutHandler configured, arg: %s", Option);
        isyslog("dynamite: %s", *msg);
        return cString::sprintf("no GetTSTimeoutHandler configured, arg: %s", Option);
        }
     isyslog("dynamite: executing %s %s", **getTSTimeoutHandler, Option);
     if (SystemExec(*cString::sprintf("%s %s", **getTSTimeoutHandler, Option), true) < 0) {
        cString msg = cString::sprintf("error (%d) on executing %s %s", errno, **getTSTimeoutHandler, Option);
        isyslog("dynamite: %s", *msg);
        return msg;
        }
     cString msg = cString::sprintf("success on executing %s %s", **getTSTimeoutHandler, Option);
     isyslog("dynamite: %s", *msg);
     return msg;
     }

  if ((strcasecmp(Command, "SIDT") == 0) || (strcasecmp(Command, "SetIdleTimeout") == 0)) {
     if (isnumber(Option)) {
        int minutes = strtol(Option, NULL, 10);
        if (minutes >= 0) {
           cDynamicDevice::idleTimeoutMinutes = minutes;
           return cString::sprintf("set Idle-Timeout to %d minutes", minutes);
           }
        ReplyCode = 550;
        return cString::sprintf("minutes must be greater than or equal to 0");
        }
     }

  if ((strcasecmp(Command, "SIDW") == 0) || (strcasecmp(Command, "SetIdleWakeup") == 0)) {
     if (isnumber(Option)) {
        int hours = strtol(Option, NULL, 10);
        if (hours > 0) {
           cDynamicDevice::idleWakeupHours = hours;
           return cString::sprintf("set Idle-Wakeup to %d hours", hours);
           }
        ReplyCode = 550;
        return cString::sprintf("hours must be greater than 0");
        }
     }

  return NULL;
}

VDRPLUGINCREATOR(cPluginDynamite); // Don't touch this!
