#ifndef __DYNAMITEUDEV_H
#define __DYNAMITEUDEV_H

#include <libudev.h>

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

class cUdevDevice {
private:
  struct udev_device *device;
  bool doUnref;
public:
  cUdevDevice(udev_device *Device, bool DoUnref = true);
  virtual ~cUdevDevice(void);

  const char  *GetAction(void) const;
  cUdevListEntry *GetDevlinksList(void) const;
  cUdevDevice *GetParent(void) const;
  const char  *GetPropertyValue(const char *Key) const;
  const char  *GetSyspath(void) const;
  };

class cUdev {
private:
  static struct udev *udev;
public:
  static struct udev *Init(void);
  static void Free(void);
  };

#endif // __DYNAMITEUDEV_H
