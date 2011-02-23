#include "udev.h"
#include <linux/stddef.h>

// --- cUdevListEntry --------------------------------------------------------

cUdevListEntry::cUdevListEntry(struct udev_list_entry *ListEntry)
:listEntry(ListEntry)
{
}

cUdevListEntry::~cUdevListEntry(void)
{
}

cUdevListEntry *cUdevListEntry::GetNext(void) const
{
  if (listEntry == NULL)
     return NULL;
  struct udev_list_entry *next = udev_list_entry_get_next(listEntry);
  if (next == NULL)
     return NULL;
  return new cUdevListEntry(next);
}

const char *cUdevListEntry::GetName(void) const
{
 if (listEntry == NULL)
     return NULL;
  return udev_list_entry_get_name(listEntry);
}

const char *cUdevListEntry::GetValue(void) const
{
 if (listEntry == NULL)
     return NULL;
  return udev_list_entry_get_value(listEntry);
}

// --- cUdevDevice -----------------------------------------------------------

cUdevDevice::cUdevDevice(udev_device *Device, bool DoUnref)
:device(Device)
,doUnref(DoUnref)
{
}

cUdevDevice::~cUdevDevice(void)
{
  if (doUnref && device)
     udev_device_unref(device);
}

const char  *cUdevDevice::GetAction(void) const
{
  if (device == NULL)
     return NULL;
  return udev_device_get_action(device);
}

cUdevListEntry *cUdevDevice::GetDevlinksList(void) const
{
  if (device == NULL)
     return NULL;
  struct udev_list_entry *listEntry = udev_device_get_devlinks_list_entry(device);
  if (listEntry == NULL)
     return NULL;
  return new cUdevListEntry(listEntry);
}

const char  *cUdevDevice::GetDevnode(void) const
{
  if (device == NULL)
     return false;
  return udev_device_get_devnode(device);
}

cUdevDevice *cUdevDevice::GetParent(void) const
{
  if (device == NULL)
     return NULL;
  struct udev_device *parent = udev_device_get_parent(device);
  if (parent == NULL)
     return NULL;
  return new cUdevDevice(parent, false);
}

const char *cUdevDevice::GetPropertyValue(const char *Key) const
{
  if (device == NULL)
     return false;
  return udev_device_get_property_value(device, Key);
}

const char *cUdevDevice::GetSyspath(void) const
{
  if (device == NULL)
     return false;
  return udev_device_get_syspath(device);
}

// --- cUdev -----------------------------------------------------------------

struct udev  *cUdev::udev = NULL;

struct udev *cUdev::Init(void)
{
  if (udev == NULL)
     udev = udev_new();
  return udev;
}

void cUdev::Free(void)
{
  if (udev)
     udev_unref(udev);
  udev = NULL;
}

cUdevDevice *cUdev::GetDeviceFromDevName(const char *DevName)
{
  if (DevName == NULL)
     return NULL;
  struct stat statbuf;
  if (stat(DevName, &statbuf) < 0)
     return NULL;
  char type;
  if (S_ISBLK(statbuf.st_mode))
     type = 'b';
  else if (S_ISCHR(statbuf.st_mode))
     type = 'c';
  else
     return NULL;
  udev_device *dev = udev_device_new_from_devnum(udev, type, statbuf.st_rdev);
  if (dev == NULL)
     return NULL;
  return new cUdevDevice(dev);
}
