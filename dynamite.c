/*
 * dynamite.c: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 */

#include <vdr/plugin.h>
#include "dynamicdevice.h"
#include "monitor.h"

static const char *VERSION        = "0.0.5d-rc1";
static const char *DESCRIPTION    = "attach/detach devices on the fly";
static const char *MAINMENUENTRY  = NULL;

class cDynamiteDvbDeviceProbe : public cDvbDeviceProbe {
private:
  static bool firstProbe;
public:
  virtual bool Probe(int Adapter, int Frontend)
  {
    if (firstProbe) {
       firstProbe = false;
       while (cDevice::NumDevices() < MAXDVBDEVICES)
             new cDynamicDevice;
       }
    isyslog("dynamite: grab dvb device %d/%d", Adapter, Frontend);
    cDynamicDevice::AttachDevice(*cString::sprintf("/dev/dvb/adapter%d/frontend%d", Adapter, Frontend));
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
{
  cDynamicDevice::dvbprobe = new cDynamiteDvbDeviceProbe;
  // make sure we're the first one you cares for dvbdevices
  cDvbDeviceProbe *firstDvbProbe = DvbDeviceProbes.First();
  if (firstDvbProbe != cDynamicDevice::dvbprobe)
     DvbDeviceProbes.Move(cDynamicDevice::dvbprobe, firstDvbProbe);
  probe = new cDynamiteDeviceProbe;
  cUdevMonitor::AddFilter("dvb", new cUdevDvbFilter());
}

cPluginDynamite::~cPluginDynamite()
{
  cUdevMonitor::ShutdownAllMonitors();
  cUdev::Free();
  if (cDynamicDevice::dvbprobe)
     delete cDynamicDevice::dvbprobe;
  if (probe)
     delete probe;
}

const char *cPluginDynamite::CommandLineHelp(void)
{
  return "  --log-udev      log all udev events to syslog (useful for diagnostics)\n";
}

bool cPluginDynamite::ProcessArgs(int argc, char *argv[])
{
  for (int i = 0; i < argc; i++) {
      if (strcmp(argv[i], "--log-udev") == 0)
         cUdevMonitor::AddFilter(NULL, new cUdevLogFilter());
      }
  return true;
}

bool cPluginDynamite::Initialize(void)
{
  // create dynamic devices
  while (cDevice::NumDevices() < MAXDEVICES)
        new cDynamicDevice;
  if (!cDynamicDevice::ProcessQueuedCommands())
     esyslog("dynamite: can't process all queued commands");
  return true;
}

bool cPluginDynamite::Start(void)
{
  if (!cDynamicDevice::ProcessQueuedCommands())
     esyslog("dynamite: can't process all queued commands");
  return true;
}

void cPluginDynamite::Stop(void)
{
  cDynamicDevice::DetachAllDevices();
}

void cPluginDynamite::Housekeeping(void)
{
  // Perform any cleanup or other regular tasks.
}

void cPluginDynamite::MainThreadHook(void)
{
  // Perform actions in the context of the main program thread.
  // WARNING: Use with great care - see PLUGINS.html!
  if (!cDynamicDevice::ProcessQueuedCommands())
     esyslog("dynamite: can't process all queued commands");
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
  return NULL;
}

bool cPluginDynamite::SetupParse(const char *Name, const char *Value)
{
  int replyCode;
  if (strcasecmp(Name, "DefaultGetTSTimeout") == 0)
     SVDRPCommand("SetDefaultGetTSTimeout", Value, replyCode);
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
    "SCND '/dev/path/glob*/pattern*'\n"
    "    Scan filesystem with pattern and try to attach each found device\n"
    "    don't forget to enclose the pattern with single quotes\n"
    "    e.g. SCND '/dev/dvb/adapter*/frontend*'\n"
    "    alternate command: ScanDevices",
    "LCKD /dev/path/to/device\n"
    "    alternate command: LockDevice",
    "    Lock the device so it can't be detached\n"
    "    alternate command: LockDevice",
    "UNLD /dev/path/to/device\n"
    "    Remove the lock of the device so it can be detached\n"
    "    alternate command: UnlockDevice",
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
    "    this devnode will be queued for attaching.\n"
    "    alternate command: AddUdevMonitor",
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
  return NULL;
}

VDRPLUGINCREATOR(cPluginDynamite); // Don't touch this!
