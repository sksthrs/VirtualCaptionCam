using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Controls;

using System.IO;
using System.Reflection;

namespace VirtualCaptionCam
{
    public static class Util
    {
        /// <summary>
        /// ディクショナリにキーがあれば対応する値を、なければデフォルト値を返す拡張メソッド。
        /// 「本来は入っているはず」の値を取得するのに用いることを想定している。
        /// </summary>
        /// <param name="dict">ディクショナリ</param>
        /// <param name="key">キー</param>
        /// <param name="defaultValue">[オプション] デフォルト値（ディクショナリにキーがない場合に用いられる）</param>
        /// <returns>キーに対応する値、もしくはデフォルト値</returns>
        public static TV GetIfExists<TK, TV>(this Dictionary<TK, TV> dict, TK key, TV defaultValue = default(TV))
        {
            if (dict != null && dict.ContainsKey(key)) return dict[key];
            return defaultValue;
        }

        /// <summary>
        /// 実行ファイルのパスを返す。
        /// https://dobon.net/vb/dotnet/vb6/apppath.html
        /// </summary>
        public static string GetExecuteFolder()
        {
            return Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
        }
    }
}
