#ifndef __DYNAMITEMENU_H
#define __DYNAMITEMENU_H

#include <vdr/osdbase.h>


class cDynamiteMainMenu : public cOsdMenu
{
private:

protected:

public:
  cDynamiteMainMenu(void);
  virtual ~cDynamiteMainMenu(void);
  virtual eOSState ProcessKey(eKeys Key);
};

#endif

