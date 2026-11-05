using System;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;

namespace ProjectKey.Settings;

public partial class App : Application {
  private Window? window_;

  public App() {
    InitializeComponent();
  }

  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  private static extern IntPtr FindWindow(string? className, string title);

  [DllImport("user32.dll")]
  private static extern bool SetForegroundWindow(IntPtr hwnd);

  protected override void OnLaunched(LaunchActivatedEventArgs args) {
    // Opened again while open: bring the open window forward instead.
    IntPtr open = FindWindow(null, "Project Key 設定");
    if (open != IntPtr.Zero) {
      SetForegroundWindow(open);
      Exit();
      return;
    }
    window_ = new MainWindow();
    window_.Activate();
  }
}
