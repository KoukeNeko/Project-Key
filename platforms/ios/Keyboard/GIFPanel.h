// The GIF page of the keyboard's panel: GIPHY's trending GIFs or one of a few searches,
// a tap copying the GIF to paste, since a keyboard cannot put an image in the text.
#import <UIKit/UIKit.h>

@interface PKGIFPanel : UIView
// Loads the first time it shows, and again after a failure; GIPHY's beta keys allow 100 calls an hour.
- (void)show;
@property(nonatomic) BOOL hasFullAccess;  // the network and the pasteboard need it
@property(nonatomic, copy) void (^onTap)(void);  // the keyboard's click and haptic
@end
