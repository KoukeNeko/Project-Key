#import "SettingsWindow.h"

#import "CandidatePanel.h"

@implementation PKSettingsWindow

+ (NSWindow *)window {
  static NSWindow *window = [] {
    NSPopUpButton *proof = [[NSPopUpButton alloc] init];
    [proof addItemsWithTitles:@[ @"永遠", @"有可疑字時", @"關閉" ]];
    // Same key and indices the input menu writes, so both stay in sync.
    [proof bind:NSSelectedIndexBinding
          toObject:NSUserDefaultsController.sharedUserDefaultsController
       withKeyPath:@"values.ProofPreview"
           options:nil];
    NSPopUpButton *appearance = [[NSPopUpButton alloc] init];
    [appearance addItemsWithTitles:@[ @"深色", @"Liquid Glass" ]];
    [appearance bind:NSSelectedIndexBinding
            toObject:NSUserDefaultsController.sharedUserDefaultsController
         withKeyPath:[@"values." stringByAppendingString:PKAppearanceKey]
             options:nil];
    appearance.enabled = PKGlassAvailable();  // Liquid Glass needs macOS 26
    NSGridView *grid = [NSGridView gridViewWithViews:@[
      @[ [NSTextField labelWithString:@"外觀"], appearance ],
      @[ [NSTextField labelWithString:@"整句預覽"], proof ],
    ]];
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    grid.rowAlignment = NSGridRowAlignmentFirstBaseline;
    grid.translatesAutoresizingMaskIntoConstraints = NO;

    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 360, 120)
                                                   styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];
    window.title = @"Project Key 設定";
    window.releasedWhenClosed = NO;
    [window.contentView addSubview:grid];
    [NSLayoutConstraint activateConstraints:@[
      [grid.topAnchor constraintEqualToAnchor:window.contentView.topAnchor constant:20],
      [grid.bottomAnchor constraintEqualToAnchor:window.contentView.bottomAnchor constant:-20],
      [grid.leadingAnchor constraintEqualToAnchor:window.contentView.leadingAnchor constant:20],
      [grid.trailingAnchor constraintLessThanOrEqualToAnchor:window.contentView.trailingAnchor constant:-20],
    ]];
    [window center];
    return window;
  }();
  return window;
}

+ (void)show {
  // The input method is a background app; it has to come forward for the window to take keys.
  [NSApp activateIgnoringOtherApps:YES];
  [self.window makeKeyAndOrderFront:nil];
}

@end
