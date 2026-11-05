// Drives PKInputController, the IMK side of the shared Controller, with real
// key events against a fake text client, covering what a user does: type a
// sentence, fix a doubtful word, commit.
// Expects data.txt next to the executable (NSBundle.mainBundle's resources).
#import <Carbon/Carbon.h>
#import <InputMethodKit/InputMethodKit.h>

#import "../Sources/CandidatePanel.h"

#include <cassert>
#include <cstdio>

@interface PKInputController : IMKInputController
@end

@interface FakeClient : NSObject
@property(nonatomic, strong) NSMutableString *committed;
@property(nonatomic, copy) NSString *marked;
@property(nonatomic, copy) NSString *bundle;
@end

@implementation FakeClient
- (instancetype)init {
  if ((self = [super init])) {
    _committed = [NSMutableString string];
    _marked = @"";
    _bundle = @"org.projectkey.test";
  }
  return self;
}
- (void)insertText:(id)string replacementRange:(NSRange)range {
  NSString *text = [string isKindOfClass:NSAttributedString.class] ? [string string] : string;
  if (range.location == NSNotFound) {
    [_committed appendString:text];
  } else {
    [_committed replaceCharactersInRange:range withString:text];
  }
  _marked = @"";
}
- (NSRange)selectedRange {
  return NSMakeRange(_committed.length, 0);
}
- (void)setMarkedText:(id)string selectionRange:(NSRange)selection replacementRange:(NSRange)range {
  if (range.location != NSNotFound) [_committed deleteCharactersInRange:range];  // the marked text takes its place
  _marked = [string isKindOfClass:NSAttributedString.class] ? [string string] : string;
}
- (NSAttributedString *)attributedSubstringFromRange:(NSRange)range {
  return [[NSAttributedString alloc] initWithString:[_committed substringWithRange:range]];
}
- (NSDictionary *)attributesForCharacterIndex:(NSUInteger)index lineHeightRectangle:(NSRect *)rect {
  *rect = NSMakeRect(200, 600, 0, 20);
  return @{};
}
- (NSString *)bundleIdentifier {
  return _bundle;
}
@end

namespace {

NSEvent *Key(NSString *characters, unsigned short keyCode = 0, NSEventModifierFlags flags = 0) {
  return [NSEvent keyEventWithType:NSEventTypeKeyDown
                          location:NSZeroPoint
                     modifierFlags:flags
                         timestamp:0
                      windowNumber:0
                           context:nil
                        characters:characters
       charactersIgnoringModifiers:characters
                         isARepeat:NO
                           keyCode:keyCode];
}

NSEvent *CapsLock(BOOL on, NSTimeInterval timestamp) {
  return [NSEvent keyEventWithType:NSEventTypeFlagsChanged
                          location:NSZeroPoint
                     modifierFlags:on ? NSEventModifierFlagCapsLock : 0
                         timestamp:timestamp
                      windowNumber:0
                           context:nil
                        characters:@""
       charactersIgnoringModifiers:@""
                         isARepeat:NO
                           keyCode:kVK_CapsLock];
}

NSEvent *Shift(BOOL down) {
  return [NSEvent keyEventWithType:NSEventTypeFlagsChanged
                          location:NSZeroPoint
                     modifierFlags:down ? NSEventModifierFlagShift : 0
                         timestamp:0
                      windowNumber:0
                           context:nil
                        characters:@""
       charactersIgnoringModifiers:@""
                         isARepeat:NO
                           keyCode:kVK_Shift];
}

// Caps Lock pressed, leaving the lock `on`; the system reports the change twice.
void PressCapsLock(PKInputController *controller, FakeClient *client, BOOL on) {
  static NSTimeInterval now = 100;
  now += 10;
  [controller handleEvent:CapsLock(on, now) client:client];
  [controller handleEvent:CapsLock(on, now + 0.002) client:client];
}

void Type(PKInputController *controller, FakeClient *client, NSString *keys) {
  for (NSUInteger i = 0; i < keys.length; i++) {
    NSString *key = [keys substringWithRange:NSMakeRange(i, 1)];
    NSEvent *event = [key isEqualToString:@" "] ? Key(key, kVK_Space) : Key(key);
    assert([controller handleEvent:event client:client]);
  }
}

}  // namespace

int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSUserDefaults.standardUserDefaults removeObjectForKey:@"ProofPreview"];
    // These steps choose after the cursor; macOS defaults to before it.
    [NSUserDefaults.standardUserDefaults setInteger:0 forKey:@"CandidatePosition"];
    FakeClient *client = [[FakeClient alloc] init];
    PKInputController *controller = [[PKInputController alloc] init];

    // 我們把舊的程式重構成模組: the engine guesses 城市 and nothing is committed yet.
    Type(controller, client, @"ji3ap7183ru.42k7t/6g4tj/6e.4t/6ai6yj3");
    assert(client.committed.length == 0);
    assert([client.marked containsString:@"城市"]);

    // Space opens candidates on the doubtful 城市; 2 picks the second one, 程式.
    assert([controller handleEvent:Key(@" ", kVK_Space) client:client]);
    assert([controller handleEvent:Key(@"2") client:client]);
    assert([client.marked containsString:@"的程式"]);

    // Enter commits the sentence as shown.
    assert([controller handleEvent:Key(@"\r", kVK_Return) client:client]);
    assert([client.committed containsString:@"的程式重"] && client.marked.length == 0);
    NSUInteger before = client.committed.length;

    // Punctuation without Shift: ' and \ both give 、, [ and ] the corner brackets.
    Type(controller, client, @"'\\[]");
    assert([client.marked isEqualToString:@"、、「」"]);
    assert([controller handleEvent:Key(@"\x1b", kVK_Escape) client:client]);
    assert(client.marked.length == 0);

    // Tone 1 via space, shifted punctuation, Delete, Esc.
    Type(controller, client, @"rup wu0 ");
    assert([controller handleEvent:Key(@"<", 0, NSEventModifierFlagShift) client:client]);
    assert([client.marked isEqualToString:@"今天，"]);
    assert([controller handleEvent:Key(@"\x7f", kVK_Delete) client:client]);
    assert([client.marked isEqualToString:@"今天"]);
    assert([controller handleEvent:Key(@"\x1b", kVK_Escape) client:client]);
    assert(client.marked.length == 0 && client.committed.length == before);

    // With the cursor inside the phrase 今天, Space offers the syllables from
    // the cursor on, so its second character can be replaced alone.
    Type(controller, client, @"rup wu0 ");
    assert([controller handleEvent:Key(@"", kVK_Home) client:client]);
    assert([controller handleEvent:Key(@"", kVK_RightArrow) client:client]);
    assert([controller handleEvent:Key(@" ", kVK_Space) client:client]);
    NSString *second = PKCandidatePanel.shared.content.rows[0][1];
    assert(second.length == 1);
    assert([controller handleEvent:Key(@"2") client:client]);
    assert([client.marked hasSuffix:second] && client.marked.length == 2);
    while (client.marked.length) assert([controller handleEvent:Key(@"\x1b", kVK_Escape) client:client]);


    // Arrows move the cursor between syllables; typing and Forward Delete act there.
    Type(controller, client, @"rup wu0 ");
    [NSUserDefaults.standardUserDefaults setInteger:0 forKey:@"ProofPreview"];  // always show the proof row
    assert([controller handleEvent:Key(@"", kVK_LeftArrow) client:client]);
    assert(PKCandidatePanel.shared.content.caret == 1);
    Type(controller, client, @"ji");
    assert(PKCandidatePanel.shared.content.caret == 1 && PKCandidatePanel.shared.content.pendingReading.length == 2);
    Type(controller, client, @"3");
    assert(PKCandidatePanel.shared.content.caret == 2);
    assert([client.marked isEqualToString:@"今我天"]);
    assert([controller handleEvent:Key(@"", kVK_ForwardDelete) client:client]);
    assert([client.marked isEqualToString:@"今我"]);
    assert([controller handleEvent:Key(@"", kVK_Home) client:client]);
    assert([controller handleEvent:Key(@"", kVK_ForwardDelete) client:client]);
    assert([client.marked isEqualToString:@"我"]);
    assert([controller handleEvent:Key(@"\x1b", kVK_Escape) client:client]);

    [NSUserDefaults.standardUserDefaults removeObjectForKey:@"ProofPreview"];

    // Down expands the rows into a grid; Up walks back up, folds the grid on
    // its first row, and closes the candidates after that.
    Type(controller, client, @"rup wu0 ");
    assert([controller handleEvent:Key(@" ", kVK_Space) client:client]);
    assert(PKCandidatePanel.shared.content.rows.count == 1);
    for (NSArray<NSString *> *row in PKCandidatePanel.shared.content.rows) {
      NSInteger columns = 0;
      for (NSString *candidate in row) columns += [PKCandidatePanel columnsSpannedBy:candidate];
      assert(columns <= PKGridColumns);
    }
    assert([controller handleEvent:Key(@"", kVK_DownArrow) client:client]);
    PKPanelContent *grid = PKCandidatePanel.shared.content;
    assert(grid.rows.count > 1 && grid.selectedRow == 1 && [grid.pageLabel hasPrefix:@"2/"]);
    assert([controller handleEvent:Key(@"", kVK_UpArrow) client:client]);
    assert(PKCandidatePanel.shared.content.rows.count > 1 && PKCandidatePanel.shared.content.selectedRow == 0);
    assert([controller handleEvent:Key(@"", kVK_UpArrow) client:client]);
    assert(PKCandidatePanel.shared.content.rows.count == 1);
    assert([controller handleEvent:Key(@"", kVK_UpArrow) client:client]);
    assert((!PKCandidatePanel.shared.visible || PKCandidatePanel.shared.content.rows == nil) && [client.marked isEqualToString:@"今天"]);

    // In the grid, number keys pick from the selection's row.
    assert([controller handleEvent:Key(@" ", kVK_Space) client:client]);
    assert([controller handleEvent:Key(@"", kVK_DownArrow) client:client]);
    NSArray<NSString *> *secondRow = PKCandidatePanel.shared.content.rows[1];  // its first is the Zhuyin, which commits
    assert([controller handleEvent:Key([NSString stringWithFormat:@"%lu", (unsigned long)secondRow.count]) client:client]);
    assert([client.marked hasPrefix:secondRow.lastObject]);
    assert([controller handleEvent:Key(@"\x1b", kVK_Escape) client:client]);
    assert(client.marked.length == 0 && client.committed.length == before);

    // With nothing composed, editing keys belong to the app.
    assert(![controller handleEvent:Key(@"\x7f", kVK_Delete) client:client]);
    assert(![controller handleEvent:Key(@"\r", kVK_Return) client:client]);

    // A shortcut commits the composition first and passes through.
    // (Not checking for 今: the grid test above taught the user model another ㄐㄧㄣ.)
    NSUInteger committed = client.committed.length;
    Type(controller, client, @"rup ");
    assert(![controller handleEvent:Key(@"c", kVK_ANSI_C, NSEventModifierFlagCommand) client:client]);
    assert(client.committed.length == committed + 1 && client.marked.length == 0);

    // Tapping Shift switches to English: keys pass through; tapping again switches back.
    [controller handleEvent:Shift(YES) client:client];
    [controller handleEvent:Shift(NO) client:client];
    assert(![controller handleEvent:Key(@"r") client:client]);
    [controller handleEvent:Shift(YES) client:client];
    [controller handleEvent:Shift(NO) client:client];
    assert([controller handleEvent:Key(@"r") client:client]);
    while (client.marked.length) [controller handleEvent:Key(@"\x1b", kVK_Escape) client:client];

    // Zhuyin typed in English mode: Tab turns the whole syllables at
    // the end (ji3cl3, 我好) into a composition and leaves the English alone.
    [controller handleEvent:Shift(YES) client:client];
    [controller handleEvent:Shift(NO) client:client];
    for (NSString *key in @[ @"h", @"e", @"l", @"l", @"o", @" ", @"j", @"i", @"3", @"c", @"l", @"3" ]) {
      assert(![controller handleEvent:Key(key) client:client]);
      [client.committed appendString:key];  // what the app does with a key passed through
      // No preview for plain English; with a tone key in it, the preview shows the result and ⇥.
      if ([key isEqualToString:@"o"]) assert(!PKCandidatePanel.shared.visible);
    }
    PKPanelContent *hint = PKCandidatePanel.shared.content;
    assert(PKCandidatePanel.shared.visible && [hint.keyHint isEqualToString:@"⇥"]);
    assert([[hint.segments componentsJoinedByString:@""] isEqualToString:@"我好"] && [hint.replacedText isEqualToString:@"ji3cl3"]);
    assert([controller handleEvent:Key(@"\t", kVK_Tab) client:client]);
    assert([client.committed hasSuffix:@"hello "] && [client.marked isEqualToString:@"我好"]);
    assert(!PKCandidatePanel.shared.visible || PKCandidatePanel.shared.content.keyHint == nil);
    assert([controller handleEvent:Key(@"r") client:client]);  // back in 中
    while (client.marked.length) [controller handleEvent:Key(@"\x1b", kVK_Escape) client:client];

    // A terminal cannot take back what it sent to the shell: no preview, and Tab stays a Tab.
    client.bundle = @"com.googlecode.iterm2";
    [controller handleEvent:Shift(YES) client:client];
    [controller handleEvent:Shift(NO) client:client];
    for (NSString *key in @[ @"j", @"i", @"3" ]) {
      assert(![controller handleEvent:Key(key) client:client]);
      [client.committed appendString:key];
    }
    assert(!PKCandidatePanel.shared.visible);
    assert(![controller handleEvent:Key(@"\t", kVK_Tab) client:client]);
    assert([client.committed hasSuffix:@"ji3"] && client.marked.length == 0);
    [controller handleEvent:Shift(YES) client:client];
    [controller handleEvent:Shift(NO) client:client];
    client.bundle = @"org.projectkey.test";

    // The reverse: English typed in 中 shows what Tab turns it into,
    // and Tab puts the English in place of the composition and switches to 英.
    while (client.marked.length) [controller handleEvent:Key(@"\x1b", kVK_Escape) client:client];
    Type(controller, client, @"hello");
    PKPanelContent *english = PKCandidatePanel.shared.content;
    assert(PKCandidatePanel.shared.visible && [english.keyHint isEqualToString:@"⇥"]);
    assert([english.segments.firstObject isEqualToString:@"hello"] && [english.replacedText isEqualToString:client.marked]);
    assert([controller handleEvent:Key(@"\t", kVK_Tab) client:client]);
    assert([client.committed hasSuffix:@"hello"] && client.marked.length == 0);
    assert(![controller handleEvent:Key(@"r") client:client]);  // now in 英
    [controller handleEvent:Shift(YES) client:client];
    [controller handleEvent:Shift(NO) client:client];
    // Turned off in settings: no preview.
    [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"ZhuyinToEnglish"];
    Type(controller, client, @"hello");
    assert(!PKCandidatePanel.shared.visible || PKCandidatePanel.shared.content.keyHint == nil);
    while (client.marked.length) [controller handleEvent:Key(@"\x1b", kVK_Escape) client:client];
    [NSUserDefaults.standardUserDefaults removeObjectForKey:@"ZhuyinToEnglish"];
    // Real Zhuyin is left alone, even when it spells something: "su " is 妮 as well as "su".
    Type(controller, client, @"su ");
    assert(!PKCandidatePanel.shared.visible || PKCandidatePanel.shared.content.keyHint == nil);
    while (client.marked.length) [controller handleEvent:Key(@"\x1b", kVK_Escape) client:client];

    // With its setting on (off by default), each Caps Lock press switches 中/英; the lock it
    // leaves on does not capitalize.
    [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"CapsLockToggle"];
    PressCapsLock(controller, client, YES);
    NSUInteger beforeCaps = client.committed.length;
    assert([controller handleEvent:Key(@"R", 0, NSEventModifierFlagCapsLock) client:client]);
    assert([client.committed hasSuffix:@"r"] && client.committed.length == beforeCaps + 1);
    assert(![controller handleEvent:Key(@"R", 0, NSEventModifierFlagCapsLock | NSEventModifierFlagShift) client:client]);
    PressCapsLock(controller, client, NO);
    assert([controller handleEvent:Key(@"r") client:client] && client.marked.length);  // back in 中
    while (client.marked.length) [controller handleEvent:Key(@"\x1b", kVK_Escape) client:client];
    // Back in 中 with the lock on (after Shift switched), letters are still Zhuyin keys.
    PressCapsLock(controller, client, YES);
    [controller handleEvent:Shift(YES) client:client];
    [controller handleEvent:Shift(NO) client:client];
    assert([controller handleEvent:Key(@"R", 0, NSEventModifierFlagCapsLock) client:client] && [client.marked isEqualToString:@"ㄐ"]);
    while (client.marked.length) [controller handleEvent:Key(@"\x1b", kVK_Escape) client:client];
    PressCapsLock(controller, client, NO);
    [controller handleEvent:Shift(YES) client:client];
    [controller handleEvent:Shift(NO) client:client];  // leave it in 中
    [NSUserDefaults.standardUserDefaults removeObjectForKey:@"CapsLockToggle"];

    [NSUserDefaults.standardUserDefaults removeObjectForKey:@"CandidatePosition"];
    std::printf("IMKControllerTest passed\n");
  }
  return 0;
}
