using System;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Windows.Graphics;

namespace ProjectKey.Settings;

internal static class WindowFit {
  // Keeps the window as tall as root's laid-out content, at a fixed width, centered. Resizing
  // inside a layout event is overridden by that layout, so it waits for it to end.
  public static void Follow(Window window, FrameworkElement root, double width) {
    void Fit() => window.DispatcherQueue.TryEnqueue(() => {
      double scale = root.XamlRoot.RasterizationScale;
      // The XAML area is the content plus what is left over below it. The
      // outer size is set: ResizeClient lands 30px taller with the title bar extended.
      int spare = (int)Math.Round((root.XamlRoot.Size.Height - root.ActualHeight) * scale);
      int widthChange = (int)Math.Ceiling(width * scale) - window.AppWindow.ClientSize.Width;
      if (spare == 0 && widthChange == 0) return;
      window.AppWindow.Resize(new SizeInt32(window.AppWindow.Size.Width + widthChange, window.AppWindow.Size.Height - spare));
      DisplayArea area = DisplayArea.GetFromWindowId(window.AppWindow.Id, DisplayAreaFallback.Primary);
      window.AppWindow.Move(new PointInt32(area.WorkArea.X + (area.WorkArea.Width - window.AppWindow.Size.Width) / 2,
                                           area.WorkArea.Y + (area.WorkArea.Height - window.AppWindow.Size.Height) / 2));
    });
    root.SizeChanged += (_, _) => Fit();
    root.Loaded += (_, _) => root.XamlRoot.Changed += (_, _) => Fit();
  }
}
