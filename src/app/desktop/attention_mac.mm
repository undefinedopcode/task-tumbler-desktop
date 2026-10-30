#include "attention.h"

#import <AppKit/AppKit.h>

namespace tumbler::attention {

void request(QWidget *, bool urgent)
{
    // Works with the window closed to the menu bar too; does nothing while
    // the app is active.
    [NSApp requestUserAttention:urgent ? NSCriticalRequest : NSInformationalRequest];
}

} // namespace tumbler::attention
