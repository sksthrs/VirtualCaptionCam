using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;

using Microsoft.Web.WebView2.Core;

namespace VirtualCaptionCam
{
    /// <summary>
    /// Browser window
    /// </summary>
    public partial class MainWindow : Window
    {
        #region Win32 API呼び出し（システムメニュー用）

        [DllImport("user32.dll")]
        private static extern IntPtr GetSystemMenu(IntPtr hWnd, int bRevert);

        [DllImport("user32.dll")]
        private static extern int AppendMenu(
                  IntPtr hMenu, int Flagsw, int IDNewItem, string lpNewItem);
        private HwndSource hwndSource = null;
        private const int WM_SYSCOMMAND = 0x112;
        private const int MF_SEPARATOR = 0x0800;
        private const int MENU_ID_HIDE_TITLEBAR = 0x0001;
        private const int MENU_ID_OPEN_URL = 0x0002;

        #endregion

        /// <summary>
        /// WebView2のユーザフォルダ名（一時フォルダ下の前提）
        /// </summary>
        private readonly string USERDATA_FOLDER_NAME = "VirtualCaptionCamera_webview2";

        /// <summary>
        /// <see cref="EnablePageTransition"/>のためのフラグで、最初にページを開く時を分離するために用いる。
        /// </summary>
        private bool isFirstNavigationCompleted = false;

        /// <summary>
        /// ページ遷移を認めるかを示すフラグ
        /// </summary>
        public bool EnablePageTransition { get; private set; } = true;

        /// <summary>
        /// メインウインドウの表示スタイルのデフォルト値（トグルの際に元のスタイルを保持するのに用いる）
        /// </summary>
        private WindowStyle defaultWindowStyle = WindowStyle.SingleBorderWindow; // 実際はLoadedイベントで上書きされる。

        public MainWindow()
        {
            Log.Info("Application begins.");
            InitializeComponent();
        }

        /// <summary>
        /// Loadedイベントハンドラ
        /// </summary>
        private async void MainWindow_Loaded(object sender, RoutedEventArgs e)
        {
            var initResult = await InitWebView2WithCacheAsync();
            // WebView2が準備できない場合はアプリケーションを終了する。
            if (initResult != true)
            {
                Application.Current.Shutdown(); // https://qiita.com/yunO66/items/3cd6ff7d3be1821b7c65
            }

            AddSystemMenu();
            defaultWindowStyle = this.WindowStyle;

            Config.TryLoad();
            var uri = Config.Uri;
            Log.Info($"startup URI:{uri}");
            webView.CoreWebView2.Navigate(uri);
        }

        private void MainWindow_Closed(object sender, EventArgs e)
        {
            Log.Info("Application finished.");
            Log.Info(Environment.NewLine);
        }

        #region システムメニュー関連処理

        /// <summary>
        /// システムメニューに項目を追加する。
        /// （参考）
        /// https://ameblo.jp/kani-tarou/entry-10240156672.html
        /// </summary>
        private void AddSystemMenu()
        {
            IntPtr hwnd = new WindowInteropHelper(this).Handle;
            IntPtr menu = GetSystemMenu(hwnd, 0);
            AppendMenu(menu, MF_SEPARATOR, 0, null);
            AppendMenu(menu, 0, MENU_ID_HIDE_TITLEBAR, "ウインドウ枠を隠す／再表示する（F1キー）");
            AppendMenu(menu, 0, MENU_ID_OPEN_URL, "URLを開く...");
        }

        protected override void OnSourceInitialized(EventArgs e)
        {
            base.OnSourceInitialized(e);
            // フックを追加
            hwndSource = PresentationSource.FromVisual(this) as HwndSource;
            if (hwndSource != null)
            {
                hwndSource.AddHook(new HwndSourceHook(this.hwndSourceHook));
            }
        }
        private IntPtr hwndSourceHook(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
        {
            if (msg == WM_SYSCOMMAND)
            {
                if (wParam.ToInt32() == MENU_ID_HIDE_TITLEBAR)
                {
                    ToggleWindowState();
                }
                else if (wParam.ToInt32() == MENU_ID_OPEN_URL)
                {
                    InputNewUri();
                }
            }
            return IntPtr.Zero;
        }

        #endregion

        /// <summary>
        /// 1行入力ダイアログを表示してURIの入力を促し、正当っぽいURIならWebView2を遷移させる。
        /// </summary>
        private void InputNewUri()
        {
            var currentUri = webView.CoreWebView2.Source;
            var uri = Microsoft.VisualBasic.Interaction.InputBox("表示するURLを指定してください。", "URL指定", currentUri);
            Log.Info($"InputNewUri() current:[{currentUri}] new:[{uri}]");
            if (string.IsNullOrEmpty(uri)) { return; }
            if (uri == currentUri) { return; }

            if (uri.StartsWith("https://") != true && uri.StartsWith("http://") != true)
            {
                MessageBox.Show("URLは「https://」か「http://」で始まるものだけ受け付けます。", "エラー");
                return;
            }
            Log.Info($"InputNewUri() navigate to: {uri}");
            webView.CoreWebView2.Navigate(uri);
        }

        /// <summary>
        /// ウインドウの枠の表示／非表示を切り替える。
        /// </summary>
        private void ToggleWindowState()
        {
            if (this.WindowStyle == WindowStyle.None)
            {
                this.WindowStyle = defaultWindowStyle;
            }
            else
            {
                this.WindowStyle = WindowStyle.None;
            }
        }

        /// <summary>
        /// メインウインドウにおけるキー入力イベントハンドラ（Previewなので内側での処理に先立って実行される点に注意）
        /// </summary>
        private void MainWindow_PreviewKeyUp(object sender, KeyEventArgs e)
        {
            // F1キーを押すたびにタイトルバーなどの表示／非表示を切り替える。
            if (e.Key == Key.F1)
            {
                ToggleWindowState();
            }
        }

        /// <summary>
        /// Initialize WebView2, especially setting user-data folder.
        /// (because it is automatically set under executable folder unless devs set explicitly)
        /// If failed, this shows MessageBox and returns false.
        /// </summary>
        /// <returns>Result (true if succeed)</returns>
        private async Task<bool> InitWebView2WithCacheAsync()
        {
            try
            {
                var userDataPath = System.IO.Path.Combine(System.IO.Path.GetTempPath(), USERDATA_FOLDER_NAME);
                Log.Info($"user-data folder : {userDataPath}");
                System.IO.Directory.CreateDirectory(userDataPath);
                var webview2Env = await CoreWebView2Environment.CreateAsync(null, userDataPath);
                await webView.EnsureCoreWebView2Async(webview2Env);

                // Disable unused features for security
                webView.CoreWebView2.Settings.AreHostObjectsAllowed = false;
                webView.CoreWebView2.Settings.IsWebMessageEnabled = false;

                // Disable page transition
                webView.CoreWebView2.NavigationCompleted += CoreWebView2_NavigationCompleted;
                webView.CoreWebView2.NavigationStarting += CoreWebView2_NavigationStarting;
                webView.CoreWebView2.NewWindowRequested += CoreWebView2_NewWindowRequested;

                return true;
            }
            catch(Exception ex)
            {
                Log.Error($"Exception ({ex.GetType()}) in initializing WebView2. Message:{ex.Message}");
                MessageBox.Show($"ブラウザの準備で異常がありました。\r\n【エラーメッセージ】{ex.Message}", "実行時エラー");
                return false;
            }
        }

        private void CoreWebView2_NavigationCompleted(object sender, CoreWebView2NavigationCompletedEventArgs e)
        {
            isFirstNavigationCompleted= true;
        }

        private void CoreWebView2_NewWindowRequested(object sender, CoreWebView2NewWindowRequestedEventArgs e)
        {
            // Inhibit creating new window
            e.Handled = true;

            // Show the URI in first window
            webView.CoreWebView2.Navigate(e.Uri);
        }

        private void CoreWebView2_NavigationStarting(object sender, CoreWebView2NavigationStartingEventArgs e)
        {
            Log.Info($"CoreWebView2.NavigationStarting URI={e.Uri}");
            // Disable navigation if property is set so
            if (isFirstNavigationCompleted && !EnablePageTransition)
            {
                e.Cancel = true;
            }
        }
    }
}
