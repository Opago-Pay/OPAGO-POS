#include "lnPoSMain.h"

void lnurlPoSMain()
{
  inputs = "";
  pinToShow = "";
  dataIn = "";

  isLNURLMoneyNumber(true);

  while (unConfirmed)
  {
    key_val = getTouch();

    if (key_val == "*")
    {
      unConfirmed = false;
    }
    else if (key_val == "#")
    {
      makeLNURL();
      qrShowCodeLNURL(" *MENU #SHOW PIN");

      while (unConfirmed)
      {
        key_val = "";
        getKeypad(false, true, false, false);

        if (key_val == "#")
        {
          showPin();

          while (unConfirmed)
          {
            key_val = "";
            getKeypad(false, true, false, false);

            if (key_val == "*")
            {
              unConfirmed = false;
            }
          }
        }
        else if (key_val == "*")
        {
          unConfirmed = false;
        }
        handleBrightnessAdjust(key_val, LNURLPOS);
      }
    }
    else
    {
      delay(100);
    }
  }
}
