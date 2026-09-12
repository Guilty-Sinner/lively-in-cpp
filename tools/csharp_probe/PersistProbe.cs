using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using Lively.Common.Helpers;
using Lively.Common.Helpers.Storage;
using Lively.Models;
using Lively.Models.Enums;
using Lively.Models.Gallery.API;

namespace csharp_probe
{
    // Oracle for the remaining persisted files that the port writes:
    //
    //   Tokens.dat                 JsonStorage<byte[]> inside EncryptUtil (DPAPI)
    //   AppRules.json              JsonStorage<List<ApplicationRulesModel>>
    //   MusicAppExclusionRules.json JsonStorage<List<AppMusicExclusionRuleModel>>
    //
    // EncryptUtil<T> is `JsonStorage<byte[]>.StoreData(path, ProtectedData.Protect(...))`,
    // so Tokens.dat is a *JSON string* holding base64 of a DPAPI blob. The blob
    // itself is non-deterministic (DPAPI salts), so only the outer shape and the
    // round-tripped values can be compared — that is what this emits.
    public static class PersistProbe
    {
        private static readonly StringBuilder Out = new StringBuilder();

        public static int Run()
        {
            Console.OutputEncoding = Encoding.UTF8;
            var root = Path.Combine(Path.GetTempPath(), "lively_persist_probe");
            if (Directory.Exists(root)) Directory.Delete(root, true);
            Directory.CreateDirectory(root);

            // byte[] → base64 JSON string (deterministic).
            var bytes = new byte[] { 1, 2, 3, 255, 0, 16, 128 };
            var bytesPath = Path.Combine(root, "Bytes.json");
            JsonStorage<byte[]>.StoreData(bytesPath, bytes);
            Line("jsonstorage.bytes", Escaped(File.ReadAllText(bytesPath)));

            var reloaded = JsonStorage<byte[]>.LoadData(bytesPath);
            Line("jsonstorage.bytes.reload", Hex(reloaded));

            JsonStorage<byte[]>.StoreData(Path.Combine(root, "Empty.json"), Array.Empty<byte>());
            Line("jsonstorage.bytes.empty", Escaped(File.ReadAllText(Path.Combine(root, "Empty.json"))));

            // AppRules.json
            var rules = new List<ApplicationRulesModel>
            {
                new ApplicationRulesModel("vlc.exe", AppRules.pause),
                new ApplicationRulesModel("game.exe", AppRules.kill),
                new ApplicationRulesModel("whatever", AppRules.ignore),
            };
            var rulesPath = Path.Combine(root, "AppRules.json");
            JsonStorage<List<ApplicationRulesModel>>.StoreData(rulesPath, rules);
            Line("apprules.serialize", Escaped(File.ReadAllText(rulesPath)));
            var rulesReloaded = JsonStorage<List<ApplicationRulesModel>>.LoadData(rulesPath);
            Line("apprules.reload", string.Join(";", rulesReloaded.Select(r => $"{r.AppName}={(int)r.Rule}")));

            // MusicAppExclusionRules.json — written with the same storage.
            var music = new List<AppMusicExclusionRuleModel>
            {
                new AppMusicExclusionRuleModel("Spotify", @"C:\Users\x\AppData\Roaming\Spotify\Spotify.exe"),
                new AppMusicExclusionRuleModel("Ünïcode ✓", null),
            };
            var musicPath = Path.Combine(root, "MusicAppExclusionRules.json");
            JsonStorage<List<AppMusicExclusionRuleModel>>.StoreData(musicPath, music);
            Line("musicexclusion.serialize", Escaped(File.ReadAllText(musicPath)));

            // Tokens.dat — the real encrypted path.
            var tokens = new TokensModel
            {
                AccessToken = "access-token-!@#$%^&*()",
                RefreshToken = "refresh-token",
                Provider = "google",
                Expiration = new DateTime(2030, 6, 1, 12, 30, 45, DateTimeKind.Utc),
            };
            // The plaintext that gets encrypted: EncryptUtil.Protect<T> is
            // UTF8(JsonConvert.SerializeObject(data)), and that IS deterministic.
            Line("tokenstore.plaintext", Escaped(Newtonsoft.Json.JsonConvert.SerializeObject(tokens)));
            Line("tokenstore.plaintext.empty",
                Escaped(Newtonsoft.Json.JsonConvert.SerializeObject(new TokensModel())));

            var tokensPath = Path.Combine(root, "Tokens.dat");
            try
            {
                EncryptUtil.Store(tokens, tokensPath);
                var raw = File.ReadAllText(tokensPath);
                Line("tokenstore.shape", raw.StartsWith("\"") && raw.EndsWith("\"") ? "quoted-string" : "other");
                Line("tokenstore.single-line", raw.Contains("\n") ? "multiline" : "single-line");
                var back = EncryptUtil.Load<TokensModel>(tokensPath);
                Line("tokenstore.reload", Describe(back));
            }
            catch (Exception ex)
            {
                // DPAPI needs a user profile; report rather than fail the probe.
                Line("tokenstore.shape", "unavailable:" + ex.GetType().Name);
            }

            // An empty token store (Clear()) must still round-trip.
            try
            {
                EncryptUtil.Store(new TokensModel(), Path.Combine(root, "TokensEmpty.dat"));
                var empty = EncryptUtil.Load<TokensModel>(Path.Combine(root, "TokensEmpty.dat"));
                Line("tokenstore.empty", Describe(empty));
            }
            catch (Exception ex)
            {
                Line("tokenstore.empty", "unavailable:" + ex.GetType().Name);
            }

            // theme.json — JsonStorage<ThemeModel>. AppVersion comes from the
            // entry assembly (the probe's is 0.0.0.0, same convention the port
            // uses for SettingsModel), and IsEditable is [JsonIgnore] so it must
            // NOT appear.
            var theme = new ThemeModel(
                file: "picture.png", preview: "preview.png", type: ThemeType.picture,
                name: "My Theme", description: "desc", contact: null, license: null,
                accentColor: "#FFAA00", tags: new List<string> { "dark", "minimal" })
            {
                IsEditable = true,
                // AppVersion defaults to the ENTRY ASSEMBLY's version, which is
                // host-dependent (the probe's csproj yields 1.0.0.0, the real app
                // declares 2.2.1.5). Stamped to a fixed sentinel the same way
                // SettingsProbe does, so the fixture pins the member ORDER and the
                // null handling rather than the probe's build metadata.
                AppVersion = "0.0.0.0",
            };
            var themePath = Path.Combine(root, "theme.json");
            JsonStorage<ThemeModel>.StoreData(themePath, theme);
            Line("theme.serialize", Escaped(File.ReadAllText(themePath)));
            var themeBack = JsonStorage<ThemeModel>.LoadData(themePath);
            Line("theme.reload", DescribeTheme(themeBack));
            Line("theme.reload.appversion", themeBack.AppVersion ?? "<null>");

            // The all-null shape: everything nullable stays JSON null, and Tags
            // is null rather than [].
            var barePath = Path.Combine(root, "theme_bare.json");
            JsonStorage<ThemeModel>.StoreData(barePath, new ThemeModel { AppVersion = "0.0.0.0" });
            Line("theme.serialize.bare", Escaped(File.ReadAllText(barePath)));

            // AppThemeFactory: create a theme from a file, then read it back from
            // its directory. The generated directory name is random, so the
            // report substitutes it — the interesting bytes are theme.json's
            // relative filenames and the absolute paths CreateFromDirectory
            // hands back.
            var source = Path.Combine(root, "source.png");
            File.WriteAllBytes(source, new byte[] { 137, 80, 78, 71 });
            var themesDir = Path.Combine(root, "themes");
            Directory.CreateDirectory(themesDir);
            var created = CreateThemeInDirectory(themesDir, source, "Wallpaper Theme", "a description");
            Line("theme.factory.created", DescribeTheme(created.Model, created.Dir, root));
            var loaded = LoadThemeFromDirectory(created.Dir);
            Line("theme.factory.loaded", DescribeTheme(loaded.Model, created.Dir, root));
            Line("theme.factory.editable", loaded.Model.IsEditable ? "True" : "False");
            Line("theme.factory.stored",
                Escaped(File.ReadAllText(Path.Combine(created.Dir, "theme.json"))).Replace(HostVersion, "<HOST>"));

            Console.Write(Out.ToString());
            Console.Out.Flush();
            return 0;
        }

        // Local re-implementation of AppThemeFactory's two methods so the probe
        // does not need the WinUI-facing Lively.UI.Shared reference. The bodies
        // are copied verbatim from AppThemeFactory.cs.
        private sealed class ThemeResult
        {
            public ThemeModel Model;
            public string Dir;
        }

        private static ThemeResult CreateThemeInDirectory(string themesDir, string filePath, string name, string description)
        {
            var themeDir = Path.Combine(themesDir, Path.GetRandomFileName());
            Directory.CreateDirectory(themeDir);
            var copyFile = Path.Combine(themeDir, Path.GetFileName(filePath));
            File.Copy(filePath, Path.Combine(themeDir, copyFile));
            var theme = new ThemeModel(file: copyFile, preview: copyFile, name: name,
                type: ThemeType.picture, description: description, contact: null,
                license: null, accentColor: null, tags: null)
            { IsEditable = true, AppVersion = "0.0.0.0" };
            JsonStorage<ThemeModel>.StoreData(Path.Combine(themeDir, "theme.json"),
                new ThemeModel(theme) { File = Path.GetFileName(theme.File), Preview = Path.GetFileName(theme.Preview) });
            return new ThemeResult { Model = theme, Dir = themeDir };
        }

        private static ThemeResult LoadThemeFromDirectory(string themeDir)
        {
            var metadata = Path.Combine(themeDir, "theme.json");
            if (!File.Exists(metadata)) throw new FileNotFoundException();
            var theme = JsonStorage<ThemeModel>.LoadData(metadata);
            return new ThemeResult
            {
                Model = new ThemeModel(theme)
                {
                    File = Path.Combine(themeDir, theme.File),
                    Preview = Path.Combine(themeDir, theme.Preview),
                    IsEditable = true,
                },
                Dir = themeDir,
            };
        }

        // The entry assembly's version, i.e. the value ThemeModel's AppVersion
        // field initializer produces on this host. Masked out of the fixture: it
        // is build metadata (the probe is 1.0.0.0, the real app declares
        // 2.2.1.5), not behaviour.
        private static string HostVersion =>
            System.Reflection.Assembly.GetEntryAssembly().GetName().Version.ToString();

        // The created directory name is Path.GetRandomFileName(), so it cannot be
        // part of a fixture. `dir` is masked out and separators are normalized so
        // the remaining text is machine-independent.
        private static string DescribeTheme(ThemeModel theme, string dir = null, string root = null)
        {
            if (theme == null) return "<null>";
            var text = DescribeThemeRaw(theme);
            if (!string.IsNullOrEmpty(dir)) text = text.Replace(dir, "<THEME_DIR>");
            if (!string.IsNullOrEmpty(root)) text = text.Replace(root, "<TEMP>");
            text = text.Replace("appVersion=" + HostVersion, "appVersion=<HOST>");
            return text.Replace('\\', '/');
        }

        private static string DescribeThemeRaw(ThemeModel theme)
        {
            if (theme == null) return "<null>";
            return string.Join(";",
                "appVersion=" + (theme.AppVersion ?? "<null>"),
                "name=" + (theme.Name ?? "<null>"),
                "description=" + (theme.Description ?? "<null>"),
                "contact=" + (theme.Contact ?? "<null>"),
                "license=" + (theme.License ?? "<null>"),
                "file=" + (theme.File ?? "<null>"),
                "preview=" + (theme.Preview ?? "<null>"),
                "accentColor=" + (theme.AccentColor ?? "<null>"),
                "type=" + (int)theme.Type,
                "tags=" + (theme.Tags == null ? "<null>" : string.Join("|", theme.Tags)),
                "isEditable=" + (theme.IsEditable ? "True" : "False"));
        }

        private static string Describe(TokensModel tokens)
        {
            if (tokens == null) return "<null>";
            return string.Join(";",
                "access=" + (tokens.AccessToken ?? "<null>"),
                "refresh=" + (tokens.RefreshToken ?? "<null>"),
                "provider=" + (tokens.Provider ?? "<null>"),
                "expiration=" + tokens.Expiration.ToString("yyyy-MM-ddTHH:mm:ss.fffffffZ"));
        }

        private static string Hex(byte[] data) => string.Concat(data.Select(b => b.ToString("x2")));

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
