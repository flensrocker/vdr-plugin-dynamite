#include "monitor.h"
#include "dynamicdevice.h"
#include <unistd.h>

// --- cUdevMonitor ----------------------------------------------------------

cMutex cUdevMonitor::mutexMonitors;
int cUdevMonitor::numMonitors = 0;
cUdevMonitor *cUdevMonitor::monitors[MAXUDEVMONITORS] = { NULL };

cUdevMonitor *cUdevMonitor::Get(const char *Subsystem)
{
  cMutexLock lock(&mutexMonitors);
  int i = 0;
  while (i < numMonitors) {
        if (monitors[i]) {
            if ((Subsystem == NULL) && (*monitors[i]->subsystem == NULL))
               return monitors[i];
            if ((Subsystem != NULL)
                && (*monitors[i]->subsystem != NULL)
                && (strcmp(*monitors[i]->subsystem, Subsystem) == 0))
               return monitors[i];
           }
        i++;
        }
  if (i < MAXUDEVMONITORS) {
     numMonitors++;
     cUdevMonitor *m = new cUdevMonitor(Subsystem);
     m->index = i;
     monitors[i] = m;
     return m;
     }
  return NULL;
}

bool cUdevMonitor::AddFilter(const char *Subsystem, cUdevFilter *Filter)
{
  if (Filter == NULL)
     return false;
  cUdevMonitor *m = Get(Subsystem);
  if (m == NULL) {
     delete Filter;
     return false;
     }
  if (!m->AddFilter(Filter)) {
     delete Filter;
     return false;
     }
  return true;
}

void cUdevMonitor::ShutdownAllMonitors(void)
{
  cMutexLock lock(&mutexMonitors);
  for (int i = 0; i < numMonitors; i++) {
      if (monitors[i]) {
         delete monitors[i];
         monitors[i] = NULL;
         }
      }
}

cUdevMonitor::cUdevMonitor(const char *Subsystem)
:monitor(NULL)
,index(-1)
,subsystem(Subsystem)
{
  struct udev *udev = cUdev::Init();
  if (udev) {
     monitor = udev_monitor_new_from_netlink(udev, "udev");
     if (monitor && Subsystem)
        udev_monitor_filter_add_match_subsystem_devtype(monitor, Subsystem, NULL);
     }
  SetDescription("dynamite udev monitor for subsystem %s", Subsystem);
}

cUdevMonitor::~cUdevMonitor(void)
{
  if ((index >= 0) && (index < MAXUDEVMONITORS)) {
     cMutexLock lock(&mutexMonitors);
     if (monitors[index] == this)
        monitors[index] = NULL;
     }
  Cancel(3);
  filters.Clear();
  if (monitor)
     udev_monitor_unref(monitor);
  monitor = NULL;
}

#define MONITOR_POLL_INTERVAL_MS 500

void cUdevMonitor::Action(void)
{
  if (monitor == NULL)
     return;
  udev_monitor_enable_receiving(monitor);
  int fd = udev_monitor_get_fd(monitor);
  if (fd == 0)
     return;
  fd_set fds;
  struct timeval tv;
  while (Running()) {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = MONITOR_POLL_INTERVAL_MS * 1000;
        int ret = select(fd+1, &fds, NULL, NULL, &tv);
        if (!Running())
           break;
        if ((ret > 0) && FD_ISSET(fd, &fds)) {
           udev_device *dev = udev_monitor_receive_device(monitor);
           if (dev) {
              cMutexLock lock(&filtersMutex);
              cUdevDevice d(dev);
              for (cUdevFilter *f = filters.First(); f; f = filters.Next(f))
                  f->Process(d);
              }
           }
        }
  close(fd);
}

bool cUdevMonitor::AddFilter(cUdevFilter *Filter)
{
  if (monitor == NULL)
     return false;
  if (Filter == NULL)
     return false;
  if (Filter->monitor == this)
     return true;
  if (Filter->monitor != NULL)
     return false;
  cMutexLock lock(&filtersMutex);
  Filter->monitor = this;
  filters.Add(Filter);
  if (!Running())
     Start();
  return true;
}

bool cUdevMonitor::DelFilter(cUdevFilter *Filter)
{
  if (monitor == NULL)
     return false;
  if (Filter == NULL)
     return false;
  if (Filter->monitor != this)
     return false;
  cMutexLock lock(&filtersMutex);
  filters.Del(Filter);
  Filter->monitor = NULL;
  if (filters.Count() == 0)
     Cancel(3);
  return true;
}

// --- cUdevFilter -----------------------------------------------------------

cUdevFilter::cUdevFilter(void)
:monitor(NULL)
{
}

cUdevFilter::~cUdevFilter(void)
{
}

// --- cUdevLogFilter --------------------------------------------------------

static void InternLogProcess(int Level, cUdevDevice &Device)
{
  char *indent = new char[Level + 2];
  indent[0] = '+';
  int i = 0;
  while (i < Level) {
        indent[i + 1] = '-';
        i++;
        }
  indent[i + 1] = '\0';
  const char *action = Device.GetAction();
  const char *subsystem = Device.GetPropertyValue("SUBSYSTEM");
  const char *syspath = Device.GetSyspath();
  const char *devpath = Device.GetPropertyValue("DEVPATH");
  const char *devname = Device.GetPropertyValue("DEVNAME");
  isyslog("dynamite: udev: %s action=%s, subsystem=%s, syspath=%s, devpath=%s, devname=%s", indent, action, subsystem, syspath, devpath, devname);
  cUdevListEntry *devlink = Device.GetDevlinksList();
  while (devlink) {
        isyslog("dynamite: udev: %s devlink: %s", indent, devlink->GetName());
        cUdevListEntry *tmp = devlink->GetNext();
        delete devlink;
        devlink = tmp;
        }
  cUdevDevice *parent = Device.GetParent();
  if (parent != NULL) {
     InternLogProcess(Level + 1, *parent);
     delete parent;
     }
}

void cUdevLogFilter::Process(cUdevDevice &Device)
{
  InternLogProcess(0, Device);
}

// --- cUdevDvbFilter --------------------------------------------------------

void cUdevDvbFilter::Process(cUdevDevice &Device)
{
  const char *action = Device.GetAction();
  if (action && (strcmp(action, "add") == 0)) {
     const char *dvb_device_type = Device.GetPropertyValue("DVB_DEVICE_TYPE");
     if (!dvb_device_type || (strcmp(dvb_device_type, "frontend") != 0))
        return;
     const char *devname = Device.GetPropertyValue("DEVNAME");
     if (!devname || cDynamicDevice::IsAttached(devname))
        return;
     cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcAttach, devname);
     }
}

// --- cUdevPatternFilter ----------------------------------------------------

cMutex   cUdevPatternFilter::filtersMutex;
cList<cUdevPatternFilter> cUdevPatternFilter::filters;

bool cUdevPatternFilter::AddFilter(const char *Subsystem, const char *Pattern)
{
  if (Pattern == NULL)
     return false;
  cMutexLock lock(&filtersMutex);
  cUdevPatternFilter *f = filters.First();
  while (f) {
        if (f->monitor && (strcmp(*f->pattern, Pattern) == 0)) {
           if ((Subsystem == NULL) && (*f->monitor->GetSubsystem() == NULL))
              return true;
           if ((Subsystem != NULL)
               && (*f->monitor->GetSubsystem() != NULL)
               && (strcmp(*f->monitor->GetSubsystem(), Subsystem) == 0))
              return true;
           }
        f = filters.Next(f);
        }
  return cUdevMonitor::AddFilter(Subsystem, new cUdevPatternFilter(Pattern));
}

cUdevPatternFilter::cUdevPatternFilter(const char *Pattern)
:pattern(Pattern)
{
  cMutexLock lock(&filtersMutex);
  filters.Add(this);
}

cUdevPatternFilter::~cUdevPatternFilter(void)
{
  cMutexLock lock(&filtersMutex);
  filters.Del(this, false);
}

void cUdevPatternFilter::Process(cUdevDevice &Device)
{
  const char *action = Device.GetAction();
  if (action && (strcmp(action, "add") == 0)) {
     const char *devname = Device.GetDevnode();
     if (devname != NULL) {
        int dLen = strlen(devname);
        int pLen = strlen(*pattern);
        if ((pLen <= dLen) && (strncmp(devname, *pattern, pLen) == 0))
           cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcAttach, devname);
        }
     }
}
