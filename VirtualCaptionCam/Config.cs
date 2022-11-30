using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using System.IO;

namespace VirtualCaptionCam
{
    internal class Config
    {
        internal const string MY_NAME = "VirtualCaptionCam";
        internal const string CONFIG_FEXTENSION = ".cfg";

        private Dictionary<string, string> items;

        #region シングルトン

        private Config()
        {
            MakeDefault();
        }

        private static Config _instance = new Config();

        #endregion

        private void MakeDefault()
        {
            items = new Dictionary<string, string>
            {
                { "Uri", "https://sksthrs.github.io/CaptionCam/#app" },
            };
        }

        /// <summary>
        /// Uri設定値（設定がない場合は空文字列）
        /// </summary>
        internal static string Uri
        {
            get => _instance.items?.GetIfExists("Uri") ?? "";
        }

        /// <summary>
        /// 設定ファイルのパスを返す。
        /// </summary>
        internal static string GetConfigPath()
        {
            return Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), 
                MY_NAME,
                MY_NAME + CONFIG_FEXTENSION);
        }

        /// <summary>
        /// 設定ファイルを読み込む。
        /// </summary>
        /// <param name="path">[オプション] ファイルパス（省略時は<see cref="GetConfigPath"/>を用いる）</param>
        /// <returns>設定読み込みが成功した場合はtrue</returns>
        internal static bool TryLoad(string path = null)
        {
            var loadPath = string.IsNullOrWhiteSpace(path) ? GetConfigPath() : path;
            Log.Info($"Loading config from [{loadPath}]");
            try
            {
                if (File.Exists(loadPath) != true)
                {
                    Log.Info("config does not exists.");
                    return false;
                }
                var text = File.ReadAllText(loadPath);
                var lines = text.Replace("\r\n", "\n").Split('\n');
                var newConfig = new Config();
                foreach (var line in lines)
                {
                    ReadConfigLine(newConfig, line);
                }
                _instance = newConfig;
                return true;
            }
            catch (Exception ex)
            {
                Log.Error($"Exception ({ex.GetType()}) in loading config. Message:{ex.Message}");
                return false;
            }
        }

        private static void ReadConfigLine(Config config, string line)
        {
            if (config == null) return;
            if (string.IsNullOrWhiteSpace(line)) return;
            if (line.StartsWith("//")) return;
            var tokens = line.Split(new char[] { '=' }, 2);
            if (tokens.Length< 2) return;

            var key = tokens[0].Trim();
            var value = tokens[1].Trim();
            if (config.items.ContainsKey(key))
            {
                config.items[key] = value;
            }
        }
    }
}
