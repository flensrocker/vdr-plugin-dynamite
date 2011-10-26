#ifndef __DYNAMITEUDEV_H
#define __DYNAMITEUDEV_H

#include <libudev.h>

#include <vdr/tools.h>

class cUdevListEntry {
private:
  struct udev_list_entry *listEntry;
public:
  cUdevListEntry(struct udev_list_entry *ListEntry);
  virtual ~cUdevListEntry(void);
  
  cUdevListEntry *GetNext(void) const;
  const char *GetName(void) const;
  const char *GetValue(void) const;
  };

class cUdevDevice : public cListObject {
private:
  struct udev_device *device;
  bool doUnref;
public:
  cUdevDevice(udev_device *Device, bool DoUnref = true);
  virtual ~cUdevDevice(void);

  const char  *GetAction(void) const;
  cUdevListEntry *GetDevlinksList(void) const;
  const char  *GetDevnode(void) const;
  const char  *GetDevpath(void) const;
  cUdevDevice *GetParent(void) const;
  const char  *GetPropertyValue(const char *Key) const;
  const char  *GetSubsystem(void) const;
  const char  *GetSysname(void) const;
  const char  *GetSyspath(void) const;
  };

class cUdev {
private:
  static struct udev *udev;
public:
  static struct udev *Init(void);
  static void Free(void);
  static cUdevDevice *GetDeviceFromDevName(const char *DevName);
  static cUdevDevice *GetDeviceFromSysPath(const char *SysPath);
  static cList<cUdevDevice> *EnumDevices(const char *Subsystem, const char *Property, const char *Value);
  };

#endif // __DYNAMITEUDEV_H
