#import "GIFPanel.h"

#import <ImageIO/ImageIO.h>

#include <vector>

namespace {

// The searches offered, after 熱門 (GIPHY's trending).
NSArray<NSString *> *Searches() {
  return @[ @"哈哈", @"謝謝", @"拜託", @"累", @"讚", @"生日快樂", @"晚安", @"抱抱" ];
}

constexpr NSUInteger kLimit = 30;
constexpr CGFloat kChipsHeight = 36, kCellHeight = 92;

// One GIF: a small rendition for the grid and a downsized one to paste. GIPHY's terms ask that
// the URLs be used as they come.
struct GIF {
  NSURL *preview;
  NSURL *paste;
};

}  // namespace

@interface PKGIFCell : UICollectionViewCell
@property(nonatomic, readonly) UIImageView *imageView;
@property(nonatomic) NSUInteger token;  // changes on reuse, so an old load or animation stops
@end

@implementation PKGIFCell
- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _imageView = [[UIImageView alloc] init];
    _imageView.contentMode = UIViewContentModeScaleAspectFill;
    _imageView.clipsToBounds = YES;
    _imageView.backgroundColor = UIColor.tertiarySystemFillColor;
    _imageView.layer.cornerRadius = 6;
    [self.contentView addSubview:_imageView];
  }
  return self;
}

- (void)layoutSubviews {
  [super layoutSubviews];
  _imageView.frame = self.contentView.bounds;
}

- (void)prepareForReuse {
  [super prepareForReuse];
  _token++;
  _imageView.image = nil;
}
@end

@interface PKGIFPanel () <UICollectionViewDataSource, UICollectionViewDelegateFlowLayout>
@end

@implementation PKGIFPanel {
  NSURLSession *_session;  // ephemeral: GIPHY's terms forbid caching what it serves
  UIScrollView *_chips;
  UILabel *_attribution, *_status, *_toast;
  UIButton *_retry;
  UICollectionView *_grid;
  std::vector<GIF> _gifs;
  NSInteger _search;  // -1 for 熱門, else the index in Searches()
  BOOL _loaded;
  NSURLSessionDataTask *_task;
}

- (instancetype)initWithFrame:(CGRect)frame {
  if (!(self = [super initWithFrame:frame])) return nil;
  _session = [NSURLSession sessionWithConfiguration:NSURLSessionConfiguration.ephemeralSessionConfiguration];
  _search = -1;

  _chips = [[UIScrollView alloc] init];
  _chips.showsHorizontalScrollIndicator = NO;
  NSArray<NSString *> *titles = [@[ @"熱門" ] arrayByAddingObjectsFromArray:Searches()];
  for (NSUInteger i = 0; i < titles.count; i++) {
    UIButton *chip = [UIButton buttonWithType:UIButtonTypeSystem];
    [chip setTitle:titles[i] forState:UIControlStateNormal];
    chip.titleLabel.font = [UIFont systemFontOfSize:14];
    chip.layer.cornerRadius = 13;
    chip.layer.cornerCurve = kCACornerCurveContinuous;
    chip.tag = static_cast<NSInteger>(i) - 1;
    [chip addTarget:self action:@selector(pickChip:) forControlEvents:UIControlEventTouchUpInside];
    [_chips addSubview:chip];
  }
  [self addSubview:_chips];

  _attribution = [[UILabel alloc] init];  // GIPHY requires its mark where its GIFs show
  _attribution.text = @"Powered by GIPHY";
  _attribution.font = [UIFont systemFontOfSize:10 weight:UIFontWeightSemibold];
  _attribution.textColor = UIColor.secondaryLabelColor;
  [self addSubview:_attribution];

  UICollectionViewFlowLayout *layout = [[UICollectionViewFlowLayout alloc] init];
  layout.minimumInteritemSpacing = layout.minimumLineSpacing = 4;
  layout.sectionInset = UIEdgeInsetsMake(0, 8, 8, 8);
  _grid = [[UICollectionView alloc] initWithFrame:CGRectZero collectionViewLayout:layout];
  _grid.backgroundColor = UIColor.clearColor;
  _grid.showsVerticalScrollIndicator = NO;
  _grid.dataSource = self;
  _grid.delegate = self;
  [_grid registerClass:PKGIFCell.class forCellWithReuseIdentifier:@"gif"];
  [self addSubview:_grid];
  if (@available(iOS 26.0, *)) {  // the default edge blur covers the chips and the top row
    for (UIScrollView *scroll in @[ _chips, _grid ]) {
      scroll.topEdgeEffect.hidden = scroll.bottomEdgeEffect.hidden = YES;
      scroll.leftEdgeEffect.hidden = scroll.rightEdgeEffect.hidden = YES;
    }
  }

  _status = [[UILabel alloc] init];
  _status.font = [UIFont systemFontOfSize:15];
  _status.textColor = UIColor.secondaryLabelColor;
  _status.textAlignment = NSTextAlignmentCenter;
  _status.numberOfLines = 0;
  [self addSubview:_status];
  _retry = [UIButton buttonWithType:UIButtonTypeSystem];
  [_retry setTitle:@"重試" forState:UIControlStateNormal];
  [_retry addTarget:self action:@selector(load) forControlEvents:UIControlEventTouchUpInside];
  _retry.hidden = YES;
  [self addSubview:_retry];

  _toast = [[UILabel alloc] init];
  _toast.font = [UIFont systemFontOfSize:14];
  _toast.textColor = UIColor.labelColor;
  _toast.textAlignment = NSTextAlignmentCenter;
  _toast.backgroundColor = UIColor.secondarySystemBackgroundColor;
  _toast.layer.cornerRadius = 15;
  _toast.clipsToBounds = YES;
  _toast.hidden = YES;
  [self addSubview:_toast];
  [self updateChips];
  return self;
}

- (void)layoutSubviews {
  [super layoutSubviews];
  CGFloat width = CGRectGetWidth(self.bounds);
  [_attribution sizeToFit];
  CGFloat mark = CGRectGetWidth(_attribution.bounds);
  _attribution.frame = CGRectMake(width - 10 - mark, 6, mark, kChipsHeight - 12);
  _chips.frame = CGRectMake(0, 0, width - mark - 16, kChipsHeight);
  CGFloat x = 8;
  for (UIButton *chip in _chips.subviews) {
    if (![chip isKindOfClass:UIButton.class]) continue;
    [chip sizeToFit];
    chip.frame = CGRectMake(x, 5, CGRectGetWidth(chip.bounds) + 20, kChipsHeight - 10);
    x += CGRectGetWidth(chip.frame) + 6;
  }
  _chips.contentSize = CGSizeMake(x + 2, kChipsHeight);
  _grid.frame = CGRectMake(0, kChipsHeight + 4, width, CGRectGetHeight(self.bounds) - kChipsHeight - 4);
  [_grid.collectionViewLayout invalidateLayout];
  _status.frame = CGRectMake(20, CGRectGetMidY(_grid.frame) - 40, width - 40, 44);
  _retry.frame = CGRectMake(width / 2 - 40, CGRectGetMaxY(_status.frame), 80, 36);
  [_toast sizeToFit];
  CGFloat toast = CGRectGetWidth(_toast.bounds) + 28;
  _toast.frame = CGRectMake((width - toast) / 2, CGRectGetMidY(_grid.frame) - 15, toast, 30);
}

- (void)show {
  if (!_loaded) [self load];
}

- (void)pickChip:(UIButton *)chip {
  if (_onTap) _onTap();
  _search = chip.tag;
  [self updateChips];
  [self load];
}

- (void)updateChips {
  for (UIButton *chip in _chips.subviews) {
    if (![chip isKindOfClass:UIButton.class]) continue;
    BOOL selected = chip.tag == _search;
    chip.backgroundColor = selected ? UIColor.labelColor : UIColor.tertiarySystemFillColor;
    chip.tintColor = selected ? UIColor.systemBackgroundColor : UIColor.labelColor;
  }
}

- (void)showStatus:(NSString *)status retry:(BOOL)retry {
  _status.text = status;
  _status.hidden = status == nil;
  _retry.hidden = !retry;
}

- (void)load {
  [_task cancel];
  _gifs.clear();
  [_grid reloadData];
  _grid.contentOffset = CGPointZero;
  if (!_hasFullAccess) {
    [self showStatus:@"需要完整取用才能載入 GIF" retry:NO];
    return;
  }
  NSString *key = [NSBundle bundleForClass:PKGIFPanel.class].infoDictionary[@"PKGiphyKey"];
  if (!key.length) {
    NSLog(@"Project Key: no GIPHY key; build with GIPHY_API_KEY set");
    [self showStatus:@"無法載入 GIF" retry:NO];
    return;
  }
  [self showStatus:nil retry:NO];
  NSURLComponents *url = [NSURLComponents componentsWithString:_search < 0 ? @"https://api.giphy.com/v1/gifs/trending"
                                                                           : @"https://api.giphy.com/v1/gifs/search"];
  NSMutableArray<NSURLQueryItem *> *query = [NSMutableArray arrayWithArray:@[
    [NSURLQueryItem queryItemWithName:@"api_key" value:key],
    [NSURLQueryItem queryItemWithName:@"limit" value:@(kLimit).stringValue],
    [NSURLQueryItem queryItemWithName:@"rating" value:@"pg"],
  ]];
  if (_search >= 0) {
    [query addObject:[NSURLQueryItem queryItemWithName:@"q" value:Searches()[static_cast<NSUInteger>(_search)]]];
    [query addObject:[NSURLQueryItem queryItemWithName:@"lang" value:@"zh-TW"]];
  }
  url.queryItems = query;
  __weak PKGIFPanel *weakSelf = self;
  _task = [_session dataTaskWithURL:url.URL completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
    std::vector<GIF> gifs;
    BOOL ok = !error && [(NSHTTPURLResponse *)response statusCode] == 200 && data;
    NSDictionary *json = ok ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    NSArray *items = [json isKindOfClass:NSDictionary.class] ? json[@"data"] : nil;
    if ([items isKindOfClass:NSArray.class]) {
      for (NSDictionary *item in items) {
        if (![item isKindOfClass:NSDictionary.class]) continue;
        NSDictionary *images = item[@"images"];
        if (![images isKindOfClass:NSDictionary.class]) continue;
        auto link = [&](NSString *rendition) -> NSURL * {
          NSDictionary *image = images[rendition];
          NSString *string = [image isKindOfClass:NSDictionary.class] ? image[@"url"] : nil;
          return [string isKindOfClass:NSString.class] ? [NSURL URLWithString:string] : nil;
        };
        NSURL *preview = link(@"fixed_width_small"), *paste = link(@"downsized");
        if (preview && paste) gifs.push_back({preview, paste});
      }
    } else {
      ok = NO;
    }
    if (error.code == NSURLErrorCancelled) return;
    if (!ok) NSLog(@"Project Key: GIPHY failed: %@ %@", error, response);
    dispatch_async(dispatch_get_main_queue(), ^{
      PKGIFPanel *panel = weakSelf;
      if (!panel) return;
      panel->_loaded = ok;
      panel->_gifs = gifs;
      [panel->_grid reloadData];
      if (!ok) {
        [panel showStatus:@"無法載入 GIF" retry:YES];
      } else if (gifs.empty()) {
        [panel showStatus:@"沒有 GIF" retry:NO];
      }
    });
  }];
  [_task resume];
}

- (NSInteger)collectionView:(UICollectionView *)collectionView numberOfItemsInSection:(NSInteger)section {
  return static_cast<NSInteger>(_gifs.size());
}

- (CGSize)collectionView:(UICollectionView *)collectionView layout:(UICollectionViewLayout *)layout sizeForItemAtIndexPath:(NSIndexPath *)indexPath {
  return CGSizeMake(floor((CGRectGetWidth(collectionView.bounds) - 16 - 8) / 3), kCellHeight);
}

// Plays the small rendition as it comes; ImageIO decodes one frame at a time, which a
// keyboard's small memory allows.
- (UICollectionViewCell *)collectionView:(UICollectionView *)collectionView cellForItemAtIndexPath:(NSIndexPath *)indexPath {
  PKGIFCell *cell = [collectionView dequeueReusableCellWithReuseIdentifier:@"gif" forIndexPath:indexPath];
  NSUInteger token = cell.token;
  __weak PKGIFCell *weakCell = cell;
  [[_session dataTaskWithURL:_gifs[static_cast<size_t>(indexPath.item)].preview
           completionHandler:^(NSData *data, NSURLResponse *, NSError *error) {
             if (!data || error) return;
             dispatch_async(dispatch_get_main_queue(), ^{
               if (!weakCell || weakCell.token != token) return;
               CGAnimateImageDataWithBlock((__bridge CFDataRef)data, nil, ^(size_t, CGImageRef image, bool *stop) {
                 PKGIFCell *shown = weakCell;
                 if (!shown || shown.token != token || !shown.window) {
                   *stop = true;
                   return;
                 }
                 shown.imageView.image = [UIImage imageWithCGImage:image];
               });
             });
           }] resume];
  return cell;
}

- (void)collectionView:(UICollectionView *)collectionView didSelectItemAtIndexPath:(NSIndexPath *)indexPath {
  [collectionView deselectItemAtIndexPath:indexPath animated:NO];
  if (_onTap) _onTap();
  __weak PKGIFPanel *weakSelf = self;
  [[_session dataTaskWithURL:_gifs[static_cast<size_t>(indexPath.item)].paste
           completionHandler:^(NSData *data, NSURLResponse *, NSError *error) {
             if (error) NSLog(@"Project Key: cannot fetch the GIF: %@", error);
             dispatch_async(dispatch_get_main_queue(), ^{
               if (data && !error) [UIPasteboard.generalPasteboard setData:data forPasteboardType:@"com.compuserve.gif"];
               [weakSelf showToast:data && !error ? @"已複製，可貼上" : @"無法複製 GIF"];
             });
           }] resume];
}

- (void)showToast:(NSString *)text {
  _toast.text = text;
  _toast.hidden = NO;
  [self setNeedsLayout];
  [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(hideToast) object:nil];
  [self performSelector:@selector(hideToast) withObject:nil afterDelay:1.5];
}

- (void)hideToast {
  _toast.hidden = YES;
}
@end
