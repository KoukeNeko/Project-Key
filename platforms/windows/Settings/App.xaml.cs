using Microsoft.UI.Xaml;

namespace ProjectKey.Settings;

public partial class App : Application {
  private Window? window_;

  public App() {
    InitializeComponent();
  }

  protected override void OnLaunched(LaunchActivatedEventArgs args) {
    window_ = new MainWindow();
    window_.Activate();
  }
}
