#ifndef __DYNAMITEMONITOR_H
#define __DYNAMITEMONITOR_H

#include <vdr/thread.h>
#include <vdr/tools.h>
#include "udev.h"

#define MAXUDEVMONITORS 10

using namespace dynamite;

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
  virtual void Process(const cUdevDevice &Device) = 0;
public:
  cUdevFilter(void);
  virtual ~cUdevFilter(void);
  };

class cUdevLogFilter : public cUdevFilter {
protected:
  virtual void Process(const cUdevDevice &Device);
  };

class cUdevDvbFilter : public cUdevFilter {
protected:
  virtual void Process(const cUdevDevice &Device);
  };

class cUdevUsbRemoveFilter : public cUdevFilter {
private:
  class cItem : public cListObject {
  public:
    cString *item;
    cString *devpath;
    cItem(const char *i, const char *d);
    virtual ~cItem(void);
    };

  static cMutex mutexFilter;
  static cUdevUsbRemoveFilter *filter;
  cMutex mutexItems;
  cList<cItem>  items;
protected:
  virtual void Process(const cUdevDevice &Device);
public:
  static void AddItem(const char *item, const char *devpath);
  static void RemoveItem(const char *item, const char *devpath);
  };

class cUdevPatternFilter : public cUdevFilter {
public:
  static bool AddFilter(const char *Subsystem, const char *Pattern);
protected:
  static cMutex   filtersMutex;
  static cList<cUdevPatternFilter> filters;

  virtual ~cUdevPatternFilter(void);
  virtual void Process(const cUdevDevice &Device);
private:
  cUdevPatternFilter(const char *Pattern);
  cString pattern;
  };

#endif // __DYNAMITEMONITOR_H
