#ifndef __DYNAMITEMONITOR_H
#define __DYNAMITEMONITOR_H

#include <vdr/thread.h>
#include <vdr/tools.h>
#include "udev.h"

#define MAXUDEVMONITORS 10

class cUdevFilter;

class cUdevMonitor : public cThread {
private:
  static cMutex mutexMonitors;
  static int numMonitors;
  static cUdevMonitor *monitors[MAXUDEVMONITORS];

  struct udev_monitor *monitor;
  int      index;
  cString  subsystem;
  cMutex   filtersMutex;
  cList<cUdevFilter> filters;

  cUdevMonitor(const char *Subsystem);
protected:
  virtual void Action(void);
public:
  static cUdevMonitor *Get(const char *Subsystem);
  static bool AddFilter(const char *Subsystem, cUdevFilter *Filter);
  static void ShutdownAllMonitors(void);

  virtual ~cUdevMonitor(void);
  cString GetSubsystem() const { return subsystem; };
  bool AddFilter(cUdevFilter *Filter);
  bool DelFilter(cUdevFilter *Filter);
  };

class cUdevFilter : public cListObject {
friend class cUdevMonitor;
protected:
  const cUdevMonitor *monitor;
  virtual void Process(cUdevDevice &Device) = 0;
public:
  cUdevFilter(void);
  virtual ~cUdevFilter(void);
  };

class cUdevLogFilter : public cUdevFilter {
protected:
  virtual void Process(cUdevDevice &Device);
  };

class cUdevDvbFilter : public cUdevFilter {
protected:
  virtual void Process(cUdevDevice &Device);
  };
  
#endif // __DYNAMITEMONITOR_H
