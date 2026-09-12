using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using Lively.Common;
using Lively.Common.Helpers.Storage;
using Lively.Models;
using Lively.Models.Enums;

namespace csharp_probe
{
    // Oracle for the wallpaper/screensaver layout files:
    //
    //   WallpaperLayout.json     JsonStorage<List<WallpaperLayoutModel>>
    //   ScreenSaverLayout.json   JsonStorage<List<ScreenSaverLayoutModel>>
    //
    // Both are persisted exactly like important.json — Formatting.Indented,
    // NullValueHandling.Include — so this pins the nested DisplayMonitor shape
    // too, which the settings fixture never covered (its SelectedDisplay is
    // null). DisplayMonitor has a public *field* (`isStale`) alongside its
    // properties, and Newtonsoft's default contract serializes public fields, so
    // whether it leaks into the file is a real question this answers.
    public static class LayoutProbe
    {
        private static readonly StringBuilder Out = new StringBuilder();

        public static int Run()
        {
            Console.OutputEncoding = Encoding.UTF8;
            var root = Path.Combine(Path.GetTempPath(), "lively_layout_probe");
            if (Directory.Exists(root)) Directory.Delete(root, true);
            Directory.CreateDirectory(root);

            var primary = new DisplayMonitor
            {
                DeviceId = @"\\?\DISPLAY#DEL41B9#5&2f0b1a4e&0&UID4352#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}",
                DeviceName = @"\\.\DISPLAY1",
                DisplayName = "Dell U2720Q",
                HMonitor = (IntPtr)0x10001,
                IsPrimary = true,
                Index = 0,
                Bounds = new System.Drawing.Rectangle(-1920, 0, 1920, 1080),
                WorkingArea = new System.Drawing.Rectangle(-1920, 0, 1920, 1040),
            };
            var secondary = new DisplayMonitor
            {
                DeviceId = @"\\?\DISPLAY#GSM5B08#4&1a2b3c4d&0&UID256",
                DeviceName = @"\\.\DISPLAY2",
                DisplayName = "LG UltraGear",
                HMonitor = (IntPtr)0x20002,
                IsPrimary = false,
                Index = 1,
                Bounds = new System.Drawing.Rectangle(0, 0, 2560, 1440),
                WorkingArea = new System.Drawing.Rectangle(0, 0, 2560, 1400),
            };
            secondary.isStale = true; // public field: does it reach JSON?

            // DisplayMonitor JSON on its own.
            Line("display.serialize.plain", Escaped(JsonConvert(primary)));
            Line("display.serialize.stale", Escaped(JsonConvert(secondary)));

            // WallpaperLayout.json — a JSON array at the root.
            var wallpapers = new List<WallpaperLayoutModel>
            {
                new WallpaperLayoutModel(primary, @"C:\wallpapers\one"),
                new WallpaperLayoutModel(secondary, null),
            };
            Line("wallpaperlayout.serialize", Escaped(Store(root, "WallpaperLayout.json", wallpapers)));

            var reloaded = JsonStorage<List<WallpaperLayoutModel>>.LoadData(
                Path.Combine(root, "WallpaperLayout.json"));
            for (var i = 0; i < reloaded.Count; i++)
            {
                Line($"wallpaperlayout.reloaded.{i}", Describe(reloaded[i]));
            }

            // ScreenSaverLayout.json — a list of {Layout, Wallpapers}.
            var screenSaver = new List<ScreenSaverLayoutModel>
            {
                new ScreenSaverLayoutModel
                {
                    Layout = WallpaperArrangement.per,
                    Wallpapers = new List<WallpaperLayoutModel>
                    {
                        new WallpaperLayoutModel(primary, @"C:\wallpapers\one"),
                    },
                },
                new ScreenSaverLayoutModel
                {
                    Layout = WallpaperArrangement.span,
                    Wallpapers = new List<WallpaperLayoutModel>
                    {
                        new WallpaperLayoutModel(primary, @"C:\wallpapers\two"),
                        new WallpaperLayoutModel(secondary, @"C:\wallpapers\three"),
                    },
                },
                new ScreenSaverLayoutModel
                {
                    Layout = WallpaperArrangement.duplicate,
                    Wallpapers = null,
                },
            };
            Line("screensaverlayout.serialize", Escaped(Store(root, "ScreenSaverLayout.json", screenSaver)));

            // DisplayMonitor equality, which the core's orphan detection uses.
            var sameId = new DisplayMonitor { DeviceId = primary.DeviceId, DeviceName = @"\\.\DISPLAY9" };
            var otherId = new DisplayMonitor { DeviceId = "different" };
            Line("display.equals.same-id", primary.Equals(sameId).ToString().ToLowerInvariant());
            Line("display.equals.other-id", primary.Equals(otherId).ToString().ToLowerInvariant());
            Line("display.equals.self", primary.Equals(primary).ToString().ToLowerInvariant());

            Console.Write(Out.ToString());
            Console.Out.Flush();
            return 0;
        }

        private static string Describe(WallpaperLayoutModel layout) => string.Join(";",
            "livelyinfopath=" + V(layout.LivelyInfoPath),
            "display=" + (layout.Display == null ? "<null>" : layout.Display.DeviceId + "|" + layout.Display.Index));

        private static string V(string s) => s == null ? "<null>" : "[" + s + "]";

        private static string JsonConvert(DisplayMonitor display) =>
            Newtonsoft.Json.JsonConvert.SerializeObject(display);

        private static string Store<T>(string root, string name, T value)
        {
            var path = Path.Combine(root, name);
            JsonStorage<T>.StoreData(path, value);
            return File.ReadAllText(path);
        }

        private static string Escaped(string payload)
        {
            var sb = new StringBuilder();
            foreach (var c in payload)
            {
                switch (c)
                {
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    case '"': sb.Append("\\\""); break;
                    default:
                        if (c < 0x20) sb.Append("\\x").Append(((int)c).ToString("x2"));
                        else sb.Append(c);
                        break;
                }
            }
            return sb.ToString();
        }

        private static void Line(string key, string value) => Out.Append(key).Append('\t').Append(value).Append('\n');
    }
}
