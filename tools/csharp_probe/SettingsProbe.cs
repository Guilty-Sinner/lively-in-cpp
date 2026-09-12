// Settings oracle: prints the real C# SettingsModel default-ctor serialization
// (JsonConvert.SerializeObject) so the C++ SettingsModel::to_json_string can be
// byte-compared against it. Skips machine-dependent fields (AppVersion assembly
// version, WallpaperDir profile path) via canonical overrides.
using System;
using System.Collections.Generic;
using System.IO;
using Lively.Models;
using Newtonsoft.Json;

namespace csharp_probe
{
    internal static class SettingsProbe
    {
        public static int Run()
        {
            var m = new SettingsModel();
            // Canonicalize machine-dependent values (the C++ test overrides the
            // same two fields before comparing).
            m.AppVersion = "0.0.0.0";
            m.WallpaperDir = @"C:\Users\canon\AppData\Local\Lively Wallpaper\Library";
            Console.WriteLine(JsonConvert.SerializeObject(m));
            return 0;
        }

        public static int RunRoundtrip()
        {
            // Parse->serialize loop: proves field-name fidelity of the JSON keys.
            var m = new SettingsModel();
            m.AppVersion = "0.0.0.0";
            m.WallpaperDir = @"C:\Users\canon\AppData\Local\Lively Wallpaper\Library";
            var json = JsonConvert.SerializeObject(m);
            var back = JsonConvert.DeserializeObject<SettingsModel>(json);
            Console.WriteLine(JsonConvert.SerializeObject(back));
            return 0;
        }
    }
}
