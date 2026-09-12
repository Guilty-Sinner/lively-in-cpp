using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using CommandLine;
using Lively.Models.Enums;

namespace csharp_probe
{
    // Oracle for the per-player StartArgs parsing.
    //
    // The real Lively.Player.* projects target .NET Framework 4.7.2 + WinForms, so
    // they cannot be referenced from a net8.0 probe. The classes below therefore
    // transcribe their [Option] attributes verbatim (names, types, Required,
    // Default) from src/Lively/Lively.Player.*/StartArgs.cs; the *parsing*
    // semantics — which are what the port must reproduce — still come from the
    // genuine CommandLineParser 2.9.1 package.
    //
    // Modes:
    //   players describe <player> <args...>   -> one canonical line
    //   players fuzz <cases-file>             -> "<line>\t<outcome>" per line
    // where a cases-file line is "<player> <args...>".
    public static class StartArgsProbe
    {
        public static int Run(string[] args)
        {
            if (args.Length < 3)
            {
                Console.Error.WriteLine("usage: players describe <player> <args...> | players fuzz <cases-file>");
                return 2;
            }

            Console.OutputEncoding = Encoding.UTF8;

            if (args[1] == "describe")
            {
                var line = string.Join(' ', args.Skip(2));
                Console.WriteLine($"{line}\t{ParseOne(line)}");
                Console.Out.Flush();
                return 0;
            }

            if (args[1] == "fuzz")
            {
                foreach (var raw in File.ReadAllLines(args[2]))
                {
                    var line = raw.Length >= 2 && raw[0] == '"' && raw[raw.Length - 1] == '"'
                        ? raw.Substring(1, raw.Length - 2)
                        : raw;
                    Console.WriteLine($"{line}\t{ParseOne(line)}");
                    Console.Out.Flush();
                }
                return 0;
            }

            Console.Error.WriteLine($"unknown mode: {args[1]}");
            return 2;
        }

        private static string ParseOne(string line)
        {
            var parts = string.IsNullOrWhiteSpace(line) ? Array.Empty<string>() : line.Split(' ');
            if (parts.Length == 0)
            {
                return "ERR no-player";
            }

            var player = parts[0];
            var argv = parts.Skip(1).ToArray();
            string outcome = null;

            switch (player)
            {
                case "wmf":
                    Parser.Default.ParseArguments<WmfArgs>(argv)
                        .WithParsed(o => outcome = Describe(wmf: o))
                        .WithNotParsed(e => outcome = "ERR " + DescribeErrors(e));
                    break;
                case "vlc":
                    Parser.Default.ParseArguments<VlcArgs>(argv)
                        .WithParsed(o => outcome = Describe(vlc: o))
                        .WithNotParsed(e => outcome = "ERR " + DescribeErrors(e));
                    break;
                case "webview2":
                    Parser.Default.ParseArguments<WebView2Args>(argv)
                        .WithParsed(o => outcome = Describe(wv2: o))
                        .WithNotParsed(e => outcome = "ERR " + DescribeErrors(e));
                    break;
                case "cef":
                    Parser.Default.ParseArguments<CefArgs>(argv)
                        .WithParsed(o => outcome = Describe(cef: o))
                        .WithNotParsed(e => outcome = "ERR " + DescribeErrors(e));
                    break;
                default:
                    return "ERR unknown-player";
            }
            return outcome ?? "ERR unknown";
        }

        private static string Describe(WmfArgs wmf = null, VlcArgs vlc = null,
                                       WebView2Args wv2 = null, CefArgs cef = null)
        {
            if (wmf != null)
            {
                return "OK wmf"
                    + Txt("path", wmf.FilePath)
                    + Num("stretch", wmf.StretchMode)
                    + Num("volume", wmf.Volume)
                    + Txt("property", wmf.Properties)
                    + Bool("verbose-log", wmf.VerboseLog);
            }
            if (vlc != null)
            {
                return "OK vlc"
                    + Txt("wallpaper-path", vlc.FilePath)
                    + Num("wallpaper-volume", vlc.Volume)
                    + Bool("wallpaper-hardware-decoding", vlc.HardwareDecoding)
                    + Txt("wallpaper-property", vlc.Properties)
                    + Txt("wallpaper-geometry", vlc.Geometry)
                    + Num("wallpaper-color-scheme", (int)vlc.Theme)
                    + Bool("wallpaper-verbose-log", vlc.VerboseLog);
            }
            if (wv2 != null)
            {
                return "OK webview2"
                    + Txt("wallpaper-url", wv2.Url)
                    + Txt("wallpaper-property", wv2.Properties)
                    + Num("wallpaper-type", (int)wv2.Type)
                    + Txt("wallpaper-display", wv2.DisplayDevice)
                    + Txt("wallpaper-geometry", wv2.Geometry)
                    + OptNum("wallpaper-scale", wv2.Scale)
                    + Bool("wallpaper-audio", wv2.AudioVisualizer)
                    + Txt("wallpaper-audio-id", wv2.AudioVisualizerDeviceId)
                    + Txt("wallpaper-debug", wv2.DebugPort)
                    + Txt("wallpaper-user-data", wv2.UserDataPath)
                    + Num("wallpaper-volume", wv2.Volume)
                    + Bool("wallpaper-system-information", wv2.SysInfo)
                    + Bool("wallpaper-system-nowplaying", wv2.NowPlaying)
                    + Bool("wallpaper-pause-event", wv2.PauseEvent)
                    + Bool("wallpaper-pause-media", wv2.PauseWebMedia)
                    + Bool("wallpaper-verbose-log", wv2.VerboseLog)
                    + Num("wallpaper-color-scheme", (int)wv2.Theme);
            }
            if (cef != null)
            {
                return "OK cef"
                    + Txt("wallpaper-url", cef.Url)
                    + Txt("wallpaper-property", cef.Properties)
                    + Num("wallpaper-type", (int)cef.Type)
                    + Txt("wallpaper-display", cef.DisplayDevice)
                    + Txt("wallpaper-geometry", cef.Geometry)
                    + Bool("wallpaper-audio", cef.AudioVisualizer)
                    + Txt("wallpaper-audio-id", cef.AudioVisualizerDeviceId)
                    + Txt("wallpaper-debug", cef.DebugPort)
                    + Txt("wallpaper-cache", cef.CachePath)
                    + Num("wallpaper-volume", cef.Volume)
                    + Bool("wallpaper-system-information", cef.SysInfo)
                    + Bool("wallpaper-system-nowplaying", cef.NowPlaying)
                    + Bool("wallpaper-pause-event", cef.PauseEvent)
                    + Bool("wallpaper-verbose-log", cef.VerboseLog)
                    + Num("wallpaper-color-scheme", (int)cef.Theme);
            }
            return "ERR no-type";
        }

        private static string Txt(string name, string v) =>
            $";{name}=" + (v == null ? "<null>" : "[" + v + "]");

        private static string Num(string name, int v) =>
            $";{name}={v.ToString(CultureInfo.InvariantCulture)}";

        private static string OptNum(string name, double? v) =>
            v.HasValue ? $";{name}={v.Value.ToString("R", CultureInfo.InvariantCulture)}" : $";{name}=<null>";

        private static string Bool(string name, bool v) =>
            $";{name}={(v ? "true" : "false")}";

        private static string DescribeErrors(IEnumerable<Error> errors)
        {
            var names = errors.Select(e => e switch
            {
                UnknownOptionError u => e.Tag + ":" + u.Token,
                _ => e.Tag.ToString(),
            });
            return string.Join(",", names);
        }

        // ---- transcriptions (attributes verbatim from StartArgs.cs) ----

        public class WmfArgs
        {
            [Option("path", Required = true, HelpText = "The file/video stream path.")]
            public string FilePath { get; set; }

            [Option("stretch", Required = false, Default = 0, HelpText = "Video Scaling algorithm.")]
            public int StretchMode { get; set; }

            [Option("volume", Required = false, Default = 100, HelpText = "Audio volume")]
            public int Volume { get; set; }

            [Option("property", Required = false, Default = null, HelpText = "LivelyProperties.json filepath.")]
            public string Properties { get; set; }

            [Option("verbose-log", Required = false, HelpText = "Verbose Logging")]
            public bool VerboseLog { get; set; }
        }

        public class VlcArgs
        {
            [Option("wallpaper-path", Required = true, HelpText = "The file/video stream path.")]
            public string FilePath { get; set; }

            [Option("wallpaper-volume", Required = false, Default = 100, HelpText = "Audio volume")]
            public int Volume { get; set; }

            [Option("wallpaper-hardware-decoding", Default = true, HelpText = "Use hardware-decoding.)")]
            public bool HardwareDecoding { get; set; }

            [Option("wallpaper-property", Required = false, Default = null, HelpText = "LivelyProperties.json filepath.")]
            public string Properties { get; set; }

            [Option("wallpaper-geometry", Required = false, HelpText = "Window size (WxH).")]
            public string Geometry { get; set; }

            [Option("wallpaper-color-scheme", Required = false, HelpText = "Set preferred theme color.")]
            public AppTheme Theme { get; set; }

            [Option("wallpaper-verbose-log", Required = false, HelpText = "Verbose Logging")]
            public bool VerboseLog { get; set; }
        }

        public class WebView2Args
        {
            [Option("wallpaper-url", Required = true, HelpText = "The url/html-file to load.")]
            public string Url { get; set; }

            [Option("wallpaper-property", Required = false, Default = null, HelpText = "LivelyProperties.info filepath (SaveData/wpdata).")]
            public string Properties { get; set; }

            [Option("wallpaper-type", Required = true, HelpText = "Type of wallpaper.")]
            public WebPageType Type { get; set; }

            [Option("wallpaper-display", Required = false, HelpText = "Wallpaper running display.")]
            public string DisplayDevice { get; set; }

            [Option("wallpaper-geometry", Required = false, HelpText = "Window size (WxH).")]
            public string Geometry { get; set; }

            [Option("wallpaper-scale", Required = false, HelpText = "Wallpaper scale factor.")]
            public double? Scale { get; set; }

            [Option("wallpaper-audio", Default = false, HelpText = "Analyse system audio(visualiser data.)")]
            public bool AudioVisualizer { get; set; }

            [Option("wallpaper-audio-id", Required = false, HelpText = "Audio output device ID used for the visualizer.")]
            public string AudioVisualizerDeviceId { get; set; }

            [Option("wallpaper-debug", Required = false, HelpText = "Debugging port.")]
            public string DebugPort { get; set; }

            [Option("wallpaper-user-data", Required = false, HelpText = "WebView2 user data path")]
            public string UserDataPath { get; set; }

            [Option("wallpaper-volume", Required = false, Default = 100, HelpText = "Audio volume.")]
            public int Volume { get; set; }

            [Option("wallpaper-system-information", Default = false, Required = false, HelpText = "Lively hw monitor api.")]
            public bool SysInfo { get; set; }

            [Option("wallpaper-system-nowplaying", Default = false, Required = false)]
            public bool NowPlaying { get; set; }

            [Option("wallpaper-pause-event", Required = false, HelpText = "Wallpaper playback changed notify.")]
            public bool PauseEvent { get; set; }

            [Option("wallpaper-pause-media", Required = false, HelpText = "Try to pause all webpage media when wallpaper pause")]
            public bool PauseWebMedia { get; set; }

            [Option("wallpaper-verbose-log", Required = false, HelpText = "Verbose Logging.")]
            public bool VerboseLog { get; set; }

            [Option("wallpaper-color-scheme", Required = false, HelpText = "Set PreferredColorScheme.")]
            public AppTheme Theme { get; set; }
        }

        public class CefArgs
        {
            [Option("wallpaper-url", Required = true, HelpText = "The url/html-file to load.")]
            public string Url { get; set; }

            [Option("wallpaper-property", Required = false, Default = null, HelpText = "LivelyProperties.info filepath (SaveData/wpdata).")]
            public string Properties { get; set; }

            [Option("wallpaper-type", Required = true, HelpText = "Type of wallpaper.")]
            public WebPageType Type { get; set; }

            [Option("wallpaper-display", Required = true, HelpText = "Wallpaper running display.")]
            public string DisplayDevice { get; set; }

            [Option("wallpaper-geometry", Required = false, HelpText = "Window size (WxH).")]
            public string Geometry { get; set; }

            [Option("wallpaper-audio", Default = false, HelpText = "Analyse system audio(visualiser data.)")]
            public bool AudioVisualizer { get; set; }

            [Option("wallpaper-audio-id", Required = false, HelpText = "Audio output device ID used for the visualizer.")]
            public string AudioVisualizerDeviceId { get; set; }

            [Option("wallpaper-debug", Required = false, HelpText = "Debugging port")]
            public string DebugPort { get; set; }

            [Option("wallpaper-cache", Required = false, HelpText = "disk cache path")]
            public string CachePath { get; set; }

            [Option("wallpaper-volume", Required = false, Default = 100, HelpText = "Audio volume")]
            public int Volume { get; set; }

            [Option("wallpaper-system-information", Default = false, Required = false, HelpText = "Lively hw monitor api")]
            public bool SysInfo { get; set; }

            [Option("wallpaper-system-nowplaying", Default = false, Required = false)]
            public bool NowPlaying { get; set; }

            [Option("wallpaper-pause-event", Required = false, HelpText = "Wallpaper playback changed notify")]
            public bool PauseEvent { get; set; }

            [Option("wallpaper-verbose-log", Required = false, HelpText = "Verbose Logging")]
            public bool VerboseLog { get; set; }

            [Option("wallpaper-color-scheme", Required = false, HelpText = "Set PreferredColorScheme.")]
            public AppTheme Theme { get; set; }
        }
    }
}
