// Oracle for the wallpaper runtime (Lively/Core):
//   * the full mpv command line VideoMpvPlayer builds in its constructor,
//   * the mpv JSON IPC command strings GetMpvCommand produces,
//   * the scaler -> property-message sequences (UpdateScaler),
//   * the Lively -> Windows DesktopWallpaperPosition mapping (PictureWinApi).
//
// Provenance of each part — this matters, because the strength of the evidence
// differs and the fixture says so:
//
//   * `mpv.cmd.*`  is produced by the REAL serializer: the private MpvCommand
//     shape ([JsonProperty("command")] List<object>) is redeclared here and
//     serialized with Newtonsoft, which is the same call GetMpvCommand makes.
//     So int-vs-double rendering, bool casing and escaping are genuinely pinned.
//   * `mpv.args.*` and the two scaler tables are VERBATIM re-expressions of code
//     that cannot be reached from a probe: VideoMpvPlayer lives in the `Lively`
//     app project (net9.0-windows, WPF), which a console probe cannot reference,
//     and PictureWinApi's scaler switch sits inside a constructor that does COM
//     and enumerates the real monitors. The builder's decision order, its
//     literal fragments and its quoting are copied character for character; the
//     transcript is the contract the port is compared against.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using Lively.Models.Enums;
using Newtonsoft.Json;

namespace csharp_probe
{
    internal static class WallpaperProbe
    {
        // Exactly the private nested type in VideoMpvPlayer.
        private class MpvCommand
        {
            [JsonProperty("command")]
            public List<object> Command { get; } = new List<object>();
        }

        public static int Run()
        {
            Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
            Thread.CurrentThread.CurrentUICulture = CultureInfo.InvariantCulture;

            EmitMpvCommands();
            EmitMpvArgs();
            EmitScalerMessages();
            EmitPictureScaler();
            return 0;
        }

        // ------------------------------------------------------------- mpv IPC json

        // VideoMpvPlayer.GetMpvCommand: JsonConvert.SerializeObject(obj) + NewLine.
        private static string GetMpvCommand(params object[] parameters)
        {
            var obj = new MpvCommand();
            obj.Command.AddRange(parameters);
            return JsonConvert.SerializeObject(obj) + Environment.NewLine;
        }

        private static void EmitMpvCommands()
        {
            void Cmd(string label, params object[] p)
                => Console.WriteLine($"mpv.cmd.{label}\t{Escape(GetMpvCommand(p))}");

            Cmd("volume.int", "set_property", "volume", 50);
            Cmd("volume.zero", "set_property", "volume", 0);
            Cmd("pause.true", "set_property", "pause", true);
            Cmd("pause.false", "set_property", "pause", false);
            Cmd("slider.float", "set_property", "speed", 1.5f);
            Cmd("slider.wholefloat", "set_property", "speed", 1.0f);
            Cmd("slider.int32", "set_property", "count", Convert.ToInt32(1.0f));
            Cmd("aid.no", "set_property", "aid", "no");
            Cmd("vid.int", "set_property", "vid", 1);
            Cmd("seek.abs", "seek", 12.5f, "absolute-percent");
            Cmd("seek.rel", "seek", -3f, "relative-percent");
            Cmd("name.quoted", "set_property", "a\"b", "c\\d");
            Cmd("screenshot", "screenshot-to-file", @"C:\tmp\shot.jpg");
            Cmd("scaler.keepaspect.yes", "set_property", "keepaspect", "yes");
        }

        private static string Escape(string s)
            => s.Replace("\r", "\\r").Replace("\n", "\\n");

        // ---------------------------------------------------------------- mpv args

        private static void EmitMpvArgs()
        {
            var cases = new[]
            {
                new object[] { "video",           WallpaperType.video,       @"C:\wp\clip.mp4",      true,  false, TargetColorspaceHintMode.target,        StreamQualitySuggestion.Highest },
                new object[] { "video.windowed",  WallpaperType.video,       @"C:\wp\clip.mp4",      true,  true,  TargetColorspaceHintMode.target,        StreamQualitySuggestion.Highest },
                new object[] { "video.nohw",      WallpaperType.video,       @"C:\wp\clip.mp4",      false, false, TargetColorspaceHintMode.source,        StreamQualitySuggestion.Low },
                new object[] { "gif",             WallpaperType.gif,         @"C:\wp\anim.gif",      true,  false, TargetColorspaceHintMode.sourceDynamic, StreamQualitySuggestion.Highest },
                new object[] { "stream",          WallpaperType.videostream, "https://youtu.be/x",   true,  false, TargetColorspaceHintMode.target,        StreamQualitySuggestion.Medium },
                new object[] { "stream.high",     WallpaperType.videostream, "https://youtu.be/x",   true,  false, TargetColorspaceHintMode.target,        StreamQualitySuggestion.Highest },
                new object[] { "stream.lowest",   WallpaperType.videostream, "https://youtu.be/x",   true,  false, TargetColorspaceHintMode.target,        StreamQualitySuggestion.Lowest },
                new object[] { "web",             WallpaperType.web,         "https://example.com/", true,  false, TargetColorspaceHintMode.target,        StreamQualitySuggestion.Highest },
                new object[] { "picture",         WallpaperType.picture,     @"C:\wp\still.jpg",    true,  false, TargetColorspaceHintMode.target,        StreamQualitySuggestion.Highest },
                new object[] { "quotes",          WallpaperType.video,       "C:\\wp\\a b\"c.mp4",   true,  false, TargetColorspaceHintMode.target,        StreamQualitySuggestion.Highest },
            };

            foreach (var c in cases)
            {
                EmitArgs((string)c[0], (WallpaperType)c[1], (string)c[2], (bool)c[3],
                         (bool)c[4], (TargetColorspaceHintMode)c[5], (StreamQualitySuggestion)c[6]);
            }

            // The `--config-dir` branch: GetConfigDir() returns the first existing
            // directory of {<base>\plugins\mpv\portable_config, <TempVideoDir>\portable_config}.
            var portable = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "plugins", "mpv", "portable_config");
            var created = false;
            try
            {
                Directory.CreateDirectory(portable);
                created = true;
                EmitArgs("configdir", WallpaperType.video, @"C:\wp\clip.mp4", true,
                         false, TargetColorspaceHintMode.target, StreamQualitySuggestion.Highest);
            }
            catch (Exception ex)
            {
                Console.WriteLine("mpv.args.ERROR\t" + ex.GetType().Name + ": " + ex.Message);
            }
            finally
            {
                if (created)
                    Directory.Delete(portable, true);
            }
        }

        // Verbatim from VideoMpvPlayer's constructor, with the two host-dependent
        // inputs (the base directory and the portable config dir) supplied rather
        // than computed.
        private static void EmitArgs(string label, WallpaperType type, string path,
            bool isHwAccel, bool isWindowed, TargetColorspaceHintMode colorSpaceMode,
            StreamQualitySuggestion streamQuality)
        {
            var baseDir = AppDomain.CurrentDomain.BaseDirectory;
            var configDir = GetConfigDir();

            var cmdArgs = new StringBuilder();
            // Startup volume will be 0
            cmdArgs.Append("--volume=0 ");
            // Disable progress message, ref: https://mpv.io/manual/master/#options-msg-level
            cmdArgs.Append("--msg-level=all=info ");
            // Alternative: --loop-file=inf
            cmdArgs.Append("--loop-file ");
            // Do not close after media end
            cmdArgs.Append("--keep-open ");
            // Disable SystemMediaTransportControls, Jul 2024 change: https://github.com/mpv-player/mpv/pull/14338
            cmdArgs.Append("--media-controls=no ");
            //Open window at (-9999,0)
            cmdArgs.Append("--geometry=-9999:0 ");
            // Always create gui window
            cmdArgs.Append("--force-window=yes ");
            // Don't move the window when clicking
            cmdArgs.Append("--no-window-dragging ");
            // Don't hide cursor after sometime.
            cmdArgs.Append("--cursor-autohide=no ");
            // Start without focused
            cmdArgs.Append("--window-minimized=yes ");
            // Allow windows screensaver
            cmdArgs.Append("--stop-screensaver=no ");
            // Disable mpv default (built-in) key bindings
            cmdArgs.Append("--input-default-bindings=no ");
            // Win11 24H2 and new mpv builds alignment fix, ref: https://github.com/rocksdanister/lively/issues/2415
            cmdArgs.Append(!isWindowed ? "--no-border " : "--border=yes ");
            // Permit mpv to receive pointer events reported by the video output driver.
            cmdArgs.Append("--input-cursor=no ");
            // On-screen-controller visibility
            cmdArgs.Append("--no-osc ");
            // Alternative: --input-ipc-server=\\.\pipe\
            cmdArgs.Append("--input-ipc-server=" + "mpvsocket" + Path.GetRandomFileName() + " ");
            // Integer scaler for sharpness
            cmdArgs.Append(type == WallpaperType.gif ? "--scale=nearest " : " ");
            // GPU decode preference
            cmdArgs.Append(isHwAccel ? "--hwdec=auto-safe " : "--hwdec=no ");
            // Select which metadata to use for the --target-colorspace-hint, requires gpu-next vo.
            cmdArgs.Append($"--target-colorspace-hint-mode={GetMpvTargetColorSpace(colorSpaceMode)} ");
            // Avoid global config file %APPDATA%\mpv\mpv.conf
            cmdArgs.Append(configDir is not null ? "--config-dir=" + "\"" + configDir + "\" " : "--no-config ");
            // File or online video stream path
            cmdArgs.Append(type == WallpaperType.videostream ? GetYtDlMpvArg(streamQuality, path) : "\"" + path + "\"");

            Console.WriteLine($"mpv.args.{label}\t{Normalize(cmdArgs.ToString(), baseDir)}");
        }

        private static string GetConfigDir()
        {
            string[] dirs = {
                Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "plugins", "mpv", "portable_config"),
            };
            foreach (var d in dirs)
                if (Directory.Exists(d))
                    return d;
            return null;
        }

        private static string Normalize(string s, string baseDir)
        {
            s = s.Replace(baseDir.TrimEnd('\\', '/'), "<BASE>");
            s = Regex.Replace(s, @"--input-ipc-server=\S+", "--input-ipc-server=mpvsocket<RANDOM>");
            return s;
        }

        // Verbatim from VideoMpvPlayer.
        private static string GetMpvTargetColorSpace(TargetColorspaceHintMode color)
        {
            return color switch
            {
                TargetColorspaceHintMode.target => "target",
                TargetColorspaceHintMode.source => "source",
                TargetColorspaceHintMode.sourceDynamic => "source-dynamic",
                _ => throw new ArgumentOutOfRangeException(nameof(color), $"Unsupported color space target: {color}")
            };
        }

        // Verbatim from VideoMpvPlayer — including the operator-precedence shape:
        // `link + qualitySuggestion switch {...}` parses as `link + (switch)`, so
        // the link is NOT wrapped in quotes and the ytdl option is glued on after.
        private static string GetYtDlMpvArg(StreamQualitySuggestion qualitySuggestion, string link)
        {
            return link + qualitySuggestion switch
            {
                StreamQualitySuggestion.Lowest => " --ytdl-format=bestvideo[height<=144]+bestaudio/best",
                StreamQualitySuggestion.Low => " --ytdl-format=bestvideo[height<=240]+bestaudio/best",
                StreamQualitySuggestion.LowMedium => " --ytdl-format=bestvideo[height<=360]+bestaudio/best",
                StreamQualitySuggestion.Medium => " --ytdl-format=bestvideo[height<=480]+bestaudio/best",
                StreamQualitySuggestion.MediumHigh => " --ytdl-format=bestvideo[height<=720]+bestaudio/best",
                StreamQualitySuggestion.High => " --ytdl-format=bestvideo[height<=1080]+bestaudio/best",
                StreamQualitySuggestion.Highest => " --ytdl-format=bestvideo+bestaudio/best",
                _ => string.Empty,
            };
        }

        // ---------------------------------------------------------- scaler messages

        // Verbatim from VideoMpvPlayer.UpdateScaler (private instance method).
        private static readonly (string Name, string[] Messages)[] ScalerTable =
        {
            ("none", new[] { "keepaspect|yes", "video-unscaled|yes" }),
            ("fill", new[] { "video-unscaled|no", "keepaspect|no" }),
            ("uniform", new[] { "panscan|0.0", "video-unscaled|no", "keepaspect|yes" }),
            ("uniformFill", new[] { "video-unscaled|no", "keepaspect|yes", "panscan|1.0" }),
            // No `auto` case in the C# switch -> no message at all.
            ("auto", Array.Empty<string>()),
        };

        private static void EmitScalerMessages()
        {
            foreach (var (name, messages) in ScalerTable)
            {
                var sb = new StringBuilder();
                foreach (var m in messages)
                {
                    var parts = m.Split('|');
                    var cmd = GetMpvCommand("set_property", parts[0], parts[1]);
                    if (sb.Length > 0)
                        sb.Append(' ');
                    sb.Append(Escape(cmd));
                }
                Console.WriteLine($"mpv.scaler.{name}\t{sb}");
            }
        }

        // --------------------------------------------------------- picture scaler

        // Verbatim from PictureWinApi's constructor (the `desktopScaler` switch).
        private static readonly (WallpaperScaler Scaler, string Windows)[] PictureTable =
        {
            (WallpaperScaler.none, "Center"),
            (WallpaperScaler.fill, "Stretch"),
            (WallpaperScaler.uniform, "Fit"),
            (WallpaperScaler.uniformFill, "Fill"),
            (WallpaperScaler.auto, "Fill"),
        };

        private static void EmitPictureScaler()
        {
            foreach (var (scaler, windows) in PictureTable)
                Console.WriteLine($"picture.scaler.{scaler}\t{windows}");
            // Show(): the span arrangement sends Span, ignoring the scaler.
            Console.WriteLine("picture.span\tSpan");
        }
    }
}
