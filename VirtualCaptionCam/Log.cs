using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using System.Runtime.CompilerServices;
using System.IO;

namespace VirtualCaptionCam
{
    internal static class Log
    {
        internal static int MAX_LOG_FILE_SIZE = 1 * 1024 * 1024;
        static Log()
        {
            BackupLogIfLarge();
        }

        private static void BackupLogIfLarge()
        {
            var logPath = GetLogFilePath();
            if (File.Exists(logPath) != true) return;

            var logInfo = new FileInfo(logPath);
            if (logInfo.Length < MAX_LOG_FILE_SIZE) return;

            var dest = GetLogbackPath();
            try
            {
                File.Move(logPath, dest);
            }
            catch (Exception ex)
            {
                Write("Error", $"Exception ({ex.GetType()}) in moving file ([{logPath}]->[{dest}]). Message={ex.Message}");
            }
        }

        internal static string GetLogFolderPath()
        {
            return Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                Config.MY_NAME);
        }

        internal static string GetLogFilePath()
        {
            return Path.Combine(GetLogFolderPath(), Config.MY_NAME + ".log");
        }

        internal static string GetLogbackPath()
        {
            return GetLogFilePath() + ".bak";
        }

        private static void WriteLogIntoFile(string line)
        {
            var logFolderPath = GetLogFolderPath();
            try
            {
                Directory.CreateDirectory(logFolderPath); // 既に存在する場合は単に何もしない。
                var logPath = GetLogFilePath();
                File.AppendAllText(logPath, line + Environment.NewLine);
            }
            catch(Exception ex)
            {
                // ログファイル出力時エラーなのでファイルには出せない。
                System.Diagnostics.Trace.WriteLine($"Exception ({ex.GetType()}) in writing log. Message:{ex.Message}");
            }
        }

        internal static void Write(
            string level,
            string message,
            [CallerMemberName] string memberName = "",
            [CallerFilePathAttribute] string fileName = "",
            [CallerLineNumber] int lineNumber = 0)
        {
            var now = DateTimeOffset.Now;
            var timestamp = now.ToString("yyyy-MM-dd HH:mm:ss.fff zzz");
            var msg = (string.IsNullOrWhiteSpace(message)) 
                ? message 
                : $"{timestamp} [{level}] {message} ({memberName} in {fileName}:{lineNumber})";
            System.Diagnostics.Debug.WriteLine(msg);
            WriteLogIntoFile(msg);
        }

        internal static void Error(
            string message,
            [CallerMemberName] string memberName = "",
            [CallerFilePathAttribute] string fileName = "",
            [CallerLineNumber] int lineNumber = 0)

        {
            Write("Error", message, memberName, fileName, lineNumber);
        }

        internal static void Info(
            string message, 
            [CallerMemberName] string memberName = "",
            [CallerFilePathAttribute] string fileName = "",
            [CallerLineNumber] int lineNumber = 0)
        {
            Write("Info", message, memberName, fileName, lineNumber);
        }
    }
}
