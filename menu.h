#ifndef __DYNAMITEMENU_H
#define __DYNAMITEMENU_H

#include <vdr/menuitems.h>


class cDynamiteMainMenu : public cMenuSetupPage
{
private:

protected:
  virtual void Store(void);

public:
  cDynamiteMainMenu(void);
  virtual ~cDynamiteMainMenu(void);
  virtual eOSState ProcessKey(eKeys Key);
};

#endif

