#include "menu.h"
#include "dynamicdevice.h"


class cDynamiteMenuDevicelist : public cOsdMenu
{
public:
  cVector<int> deviceIds;
  cStringList  devicePaths;

  cDynamiteMenuDevicelist(const char *Title)
   :cOsdMenu(Title)
  {
    cDynamicDevice *d;
    const char *cp;
    char *p;
    for (int i = 0; i < cDynamicDevice::NumDynamicDevices(); i++) {
        d = cDynamicDevice::GetDynamicDevice(i);
        if (d == NULL)
           continue;
        cp = d->GetDevPath();
        if ((cp == NULL) || (strlen(cp) == 0))
           continue;
        p = strdup(cp);
        if (p == NULL)
           continue;
        deviceIds.Append(i);
        devicePaths.Append(p);
        cString text = cString::sprintf("%d%s %s", i + 1, (d->IsDetachable() ? " " : "*"), p);
        Add(new cOsdItem(*text));
        }
  }
};

enum eMenuAction { maList,
                   maScan,
                   maDetach,
                   maLock,
                   maUnlock,
                   maDisableAutoIdle,
                   maEnableAutoIdle,
                   maSetIdle
                 };

class cDynamiteMenuItem : public cOsdItem
{
public:
  eMenuAction action;
  bool showList;

  cDynamiteMenuItem(eMenuAction Action, const char *Text, bool ShowList = true)
   :cOsdItem(Text)
   ,action(Action)
   ,showList(ShowList)
  {
  }

  virtual eOSState Action(int DevId, const char *DevPath)
  {
    switch (action) {
      case maScan:
        isyslog("dynamite: menu action: scan for dvb devices");
        cDynamicDevice::AttachDevicePattern("/dev/dvb/adapter*/frontend*");
        return osEnd;
      case maDetach:
        isyslog("dynamite: menu action: detach device %s", DevPath);
        cDynamicDeviceProbe::QueueDynamicDeviceCommand(ddpcDetach, DevPath);
        return osEnd;
      case maLock:
      case maUnlock:
        isyslog("dynamite: menu action: %slock device %s", (action == maUnlock ? "un" : ""), DevPath);
        cDynamicDevice::SetLockDevice(DevPath, action == maLock);
        break;
      case maDisableAutoIdle:
      case maEnableAutoIdle:
        isyslog("dynamite: menu action: %s auto-idle mode on device %s", (action == maDisableAutoIdle ? "disable" : "enable"), DevPath);
        cDynamicDevice::SetAutoIdle(DevPath, action == maDisableAutoIdle);
        break;
      case maSetIdle:
        isyslog("dynamite: menu action: set idle mode on device %s", DevPath);
        cDynamicDevice::SetIdle(DevPath, true);
        break;
      default:
        return osUnknown;
      }
    return osUnknown;
  }
};

cDynamiteMainMenu::cDynamiteMainMenu(void)
{
  Add(new cDynamiteMenuItem(maList, tr("list attached devices")));
  Add(new cDynamiteMenuItem(maScan, tr("scan for new DVB devices"), false));
  Add(new cDynamiteMenuItem(maDetach, tr("detach device")));
  Add(new cDynamiteMenuItem(maLock, tr("disable detach of device")));
  Add(new cDynamiteMenuItem(maUnlock, tr("enable detach of device")));
  Add(new cDynamiteMenuItem(maDisableAutoIdle, tr("disable auto-idle mode of device")));
  Add(new cDynamiteMenuItem(maEnableAutoIdle, tr("enable auto-idle mode of device")));
  Add(new cDynamiteMenuItem(maSetIdle, tr("switch device to idle")));
}

cDynamiteMainMenu::~cDynamiteMainMenu(void)
{
}

void cDynamiteMainMenu::Store(void)
{
}

eOSState cDynamiteMainMenu::ProcessKey(eKeys Key)
{
  cDynamiteMenuItem *item = dynamic_cast<cDynamiteMenuItem*>(Get(Current()));
  if (item == NULL)
     return cOsdMenu::ProcessKey(Key);
  eOSState state = osUnknown;
  switch (Key) {
    case kOk:
     {
       if (HasSubMenu()) {
          cDynamiteMenuDevicelist *dl = dynamic_cast<cDynamiteMenuDevicelist*>(SubMenu());
          if (dl != NULL) {
             int i = dl->Current();
             if ((i >= 0) && (i < dl->deviceIds.Size()))
                state = item->Action(dl->deviceIds[i], dl->devicePaths[i]);
             }
          CloseSubMenu();
          }
       else {
          if (item->showList)
             state = AddSubMenu(new cDynamiteMenuDevicelist(item->Text()));
          else
             state = item->Action(-1, NULL);
          }
       break;
     }
    default:
     {
      state = cOsdMenu::ProcessKey(Key);
      break;
     }
    }
  return state;
}
