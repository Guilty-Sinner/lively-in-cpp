using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using Lively.Common;
using Lively.Common.Extensions;
using Lively.Common.Factories;
using Lively.Common.Helpers;
using Lively.Common.Helpers.Files;
using Lively.Common.Helpers.Storage;
using Lively.Models;
using Lively.Models.Enums;

namespace csharp_probe
{
    // Oracle for the Lively.Common "library" tranche:
    //
    //   Models      LivelyInfoModel (livelyinfo.json), LibraryModel, FileTypeModel
    //   Helpers     FileTypes, FileUtil, LinkUtil, LivelyInfoUtil, Languages,
    //               WindowClassExclusions
    //   Extensions  WallpaperExtensions
    //   Factories   WallpaperLibraryFactory
    //
    // Emits one `key<TAB>value` line per check in a fixed order; the C++ port
    // reproduces the same transcript (tests/test_library.cpp). Filesystem checks
    // build their own fixture tree under a temp root and relativize every path to
    // it, so the transcript carries no machine-specific text.
    //
    // Culture is pinned to invariant: FileUtil.SizeSuffix uses "{0:n1}" which is
    // culture-sensitive (group/decimal separators), and a golden file cannot
    // depend on the machine's locale.
    public static class LibraryProbe
    {
        private static readonly StringBuilder Out = new StringBuilder();

        public static int Run()
        {
            Console.OutputEncoding = Encoding.UTF8;
            var previous = CultureInfo.CurrentCulture;
            var previousUi = CultureInfo.CurrentUICulture;
            CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
            // LivelyInfoUtil falls back to CultureInfo.CurrentUICulture.Name when
            // no language code is given, so it is pinned too (fr-FR is absent from
            // the fixture file) — otherwise this check would be machine-dependent.
            CultureInfo.CurrentUICulture = new CultureInfo("fr-FR");
            // Fresh fixture tree so repeated runs are identical.
            if (Directory.Exists(ScratchDir)) Directory.Delete(ScratchDir, true);
            Directory.CreateDirectory(ScratchDir);
            try
            {
                EmitModels();
                EmitFileTypes();
                EmitLinkUtil();
                EmitFileUtil();
                EmitWallpaperExtensions();
                EmitLanguages();
                EmitWindowClassExclusions();
                EmitLocalization();
                EmitLibraryFactory();
            }
            finally
            {
                CultureInfo.CurrentCulture = previous;
                CultureInfo.CurrentUICulture = previousUi;
            }
            Console.Write(Out.ToString());
            Console.Out.Flush();
            return 0;
        }

        // ---------------------------------------------------------------- models

        private static void EmitModels()
        {
            // Informational: the value the real Lively.Models assembly reports for
            // Assembly.GetExecutingAssembly().GetName().Version.
            var defaultModel = new LivelyInfoModel { AppVersion = CanonicalAppVersion };
            Line("livelyinfo.appversion", new LivelyInfoModel().AppVersion);

            Line("livelyinfo.default.serialize", Escaped(JsonOf(defaultModel)));

            var full = new LivelyInfoModel
            {
                AppVersion = CanonicalAppVersion,
                Title = "A \"quoted\" title",
                Thumbnail = "thumb.png",
                Preview = null,
                Desc = "line1\nline2\ttabbed",
                Author = "Ünïcode ✓",
                License = "GPL-3.0",
                Contact = "https://example.com/u/1",
                Type = WallpaperType.video,
                FileName = "video.mp4",
                Arguments = "--loop true",
                IsAbsolutePath = true,
                Id = "abc123",
                Tags = new List<string> { "tag1", "タグ2", "tag/3" },
                Version = 4,
            };
            Line("livelyinfo.full.serialize", Escaped(JsonOf(full)));

            // Parse-back: what the C# deserializer makes of the full model.
            var reparsed = JsonStorage<LivelyInfoModel>.LoadData(WriteTemp("reparse.json", JsonOf(full)));
            Line("livelyinfo.reparsed", DescribeInfo(reparsed));

            // Unknown/missing members and a null Tags list.
            var sparse = JsonStorage<LivelyInfoModel>.LoadData(
                WriteTemp("sparse.json", "{\"Title\":\"only-title\"}"));
            Line("livelyinfo.sparse", DescribeInfo(sparse));

            // LibraryModel title coercion: blank/whitespace -> "---".
            Line("librarymodel.blank-title", new LibraryModel { Title = "  " }.Title ?? "<null>");
            Line("librarymodel.null-title", new LibraryModel { Title = null }.Title ?? "<null>");
            Line("librarymodel.set-title", new LibraryModel { Title = "Real" }.Title);
            Line("librarymodel.defaults", string.Join(";",
                new LibraryModel().IsReadyToSet ? "ready" : "not-ready",
                new LibraryModel().DownloadingProgressText ?? "<null>",
                new LibraryModel().IsSubscribed ? "sub" : "unsub"));
        }

        private static string DescribeInfo(LivelyInfoModel m) => string.Join(";",
            "title=" + V(m.Title),
            "thumb=" + V(RelPath(m.Thumbnail)),
            "preview=" + V(RelPath(m.Preview)),
            "desc=" + V(m.Desc),
            "author=" + V(m.Author),
            "license=" + V(m.License),
            "contact=" + V(RelPath(m.Contact)),
            "type=" + (int)m.Type,
            "file=" + V(RelPath(m.FileName)),
            "args=" + V(m.Arguments),
            "abs=" + (m.IsAbsolutePath ? "true" : "false"),
            "id=" + V(m.Id),
            "tags=" + (m.Tags == null ? "<null>" : string.Join("|", m.Tags.Select(Text))),
            "version=" + m.Version);

        // Only the machine-specific prefixes are stripped; the rest of the value
        // (including the original separators) is preserved.
        private static string RelPath(string s) => s == null ? null : PathRoots(s);

        private const string CanonicalAppVersion = "0.0.0.0";

        private static string JsonOf(LivelyInfoModel m) =>
            File.ReadAllText(WriteTemp("model.json", null, m));

        private static string WriteTemp(string name, string text, LivelyInfoModel model = null)
        {
            var path = Path.Combine(ScratchDir, name);
            if (model != null)
            {
                // Exactly what the app writes to livelyinfo.json.
                JsonStorage<LivelyInfoModel>.StoreData(path, model);
            }
            else
            {
                File.WriteAllText(path, text);
            }
            return path;
        }

        // ------------------------------------------------------------ filetypes

        private static void EmitFileTypes()
        {
            var extensions = new[]
            {
                ".wmv", ".avi", ".flv", ".m4v", ".mkv", ".mov", ".mp4", ".mp4v", ".mpeg4", ".mpg",
                ".webm", ".ogm", ".ogv", ".ogx",
                ".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp", ".jfif",
                ".gif", ".html", ".exe", ".zip", ".txt", ".heic", ".MP4", ".PNG", ".Exe", ".GIF",
                "", ".", ".htm",
            };
            foreach (var ext in extensions)
            {
                Line("filetypes.gettype." + (ext.Length == 0 ? "<none>" : ext),
                    ((int)FileTypes.GetFileType("wallpaper" + ext)).ToString(CultureInfo.InvariantCulture));
            }

            foreach (var name in new[] { "a.zip", "a.ZIP", "a.Zip", "a.7z", "zip", "a.zipx" })
            {
                Line("filetypes.ispackext." + name, FileTypes.IsWallpaperPackageExtension(name).ToString().ToLowerInvariant());
            }

            // IsWallpaperPackage: real archives written by SharpZipLib itself, so
            // the port's central-directory reader is compared against the library
            // upstream actually uses (including the ignoreCase entry lookup).
            foreach (var (label, entries) in new[]
            {
                ("ok", new[] { "LivelyInfo.json", "index.html" }),
                ("nested", new[] { "sub/LivelyInfo.json" }),
                ("case", new[] { "livelyinfo.JSON" }),
                ("nometa", new[] { "index.html" }),
            })
            {
                var archive = Path.Combine(ScratchDir, "pkg_" + label + ".zip");
                using (var file = File.Create(archive))
                using (var zip = new ICSharpCode.SharpZipLib.Zip.ZipOutputStream(file))
                {
                    foreach (var entryName in entries)
                    {
                        var entry = new ICSharpCode.SharpZipLib.Zip.ZipEntry(entryName)
                        {
                            DateTime = new DateTime(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc),
                        };
                        zip.PutNextEntry(entry);
                        var bytes = Encoding.UTF8.GetBytes("x");
                        zip.Write(bytes, 0, bytes.Length);
                    }
                    zip.Finish();
                }
                Line("filetypes.ispkg." + label + ".zip",
                    FileTypes.IsWallpaperPackage(archive).ToString().ToLowerInvariant());
            }
            var notZip = Path.Combine(ScratchDir, "pkg_garbage.zip");
            File.WriteAllBytes(notZip, Encoding.ASCII.GetBytes("this is not a zip archive at all"));
            Line("filetypes.ispkg.garbage.zip", FileTypes.IsWallpaperPackage(notZip).ToString().ToLowerInvariant());
            Line("filetypes.ispkg.missing.zip",
                FileTypes.IsWallpaperPackage(Path.Combine(ScratchDir, "nope.zip")).ToString().ToLowerInvariant());

            // SupportedFormats table: order + contents (the "first match wins" rule).
            for (var i = 0; i < FileTypes.SupportedFormats.Length; i++)
            {
                var f = FileTypes.SupportedFormats[i];
                Line($"filetypes.supported.{i}", $"{(int)f.Type}:{string.Join(",", f.Extentions)}");
            }
        }

        // ------------------------------------------------------------- linkutil

        private static void EmitLinkUtil()
        {
            var urls = new[]
            {
                "https://example.com/",
                "https://example.com",
                "https://example.com//",
                "https://www.example.com/",
                "https://www.example.com/a/b.html",
                "http://example.com/path/to/file.gif?v=2",
                "https://example.com/dir/",
                "https://example.com/a?x=1",
                "https://example.com/#frag",
                "https://example.com/a//b",
                "HTTPS://EXAMPLE.COM/Path/File.HTML",
                "not a url",
                "",
                "/local/path/file.html",
                "local/path/file.html",
                "/file.html",
                "C:\\local\\path\\file.html",
                "file:///C:/local/path/file.html",
            };
            foreach (var u in urls)
            {
                Line("linkutil.lastsegment." + Escape(u), "[" + LinkUtil.GetLastSegmentUrl(u) + "]");
            }

            foreach (var p in new[]
            {
                "C:\\wallpapers\\one\\file.html",
                "C:\\wallpapers\\two\\file.html",
                "C:\\wallpapers\\two\\nested\\file.html",
                "relative\\dir\\file.html",
            })
            {
                Line("linkutil.stablehost." + Escape(p), LinkUtil.GetStableHostName(p));
            }

            foreach (var u in new[]
            {
                "example.com/page", "https://example.com/page", "http://example.com",
                "https://example.com:8080/x", "/abs/path", "  ",
            })
            {
                var ok = LinkUtil.TrySanitizeUrl(u, out var uri);
                Line("linkutil.sanitize." + Escape(u),
                    ok ? uri.AbsoluteUri : "<invalid>");
            }
        }

        // ------------------------------------------------------------- fileutil

        private static void EmitFileUtil()
        {
            foreach (var name in new[]
            {
                "plain", "a/b", "a\\b", "a:b", "a*b?c", "a<b>c|d\"e", "dot.name.txt",
                "con", "имя файла", "trailing ", " leading",
            })
            {
                Line("fileutil.safefilename." + Escape(name), "[" + FileUtil.GetSafeFilename(name) + "]");
            }

            // NextAvailableFilename: known collision set, queried in a scratch dir.
            var dir = Path.Combine(ScratchDir, "nextavail");
            Directory.CreateDirectory(dir);
            foreach (var f in new[] { "a.txt", "a (1).txt", "a (2).txt", "b", "b (1)", "c.txt" })
            {
                File.WriteAllText(Path.Combine(dir, f), "x");
            }
            foreach (var q in new[] { "a.txt", "b", "c.txt", "d.txt" })
            {
                Line("fileutil.nextavailable." + q, Path.GetFileName(FileUtil.NextAvailableFilename(Path.Combine(dir, q))));
            }

            const string sample = "hello world";
            var samplePath = Path.Combine(ScratchDir, "checksum.txt");
            File.WriteAllText(samplePath, sample);
            Line("fileutil.sha256.helloworld", FileUtil.GetChecksumSHA256(samplePath));

            foreach (var v in new long[]
            {
                0, 1, 999, 1000, 1023, 1024, 1536, 999999, 1048576, 1234567890, 1099511627776,
                -1, -1536,
            })
            {
                Line("fileutil.sizesuffix." + v.ToString(CultureInfo.InvariantCulture), "[" + FileUtil.SizeSuffix(v) + "]");
            }
            foreach (var places in new[] { 0, 1, 2, 3 })
            {
                Line("fileutil.sizesuffix.1536." + places, "[" + FileUtil.SizeSuffix(1536, places) + "]");
            }
            // 2560 bytes == 2.5 KB exactly: distinguishes banker's rounding
            // (Math.Round on a decimal) from away-from-zero.
            Line("fileutil.sizesuffix.2560.0", "[" + FileUtil.SizeSuffix(2560, 0) + "]");
            Line("fileutil.sizesuffix.3584.0", "[" + FileUtil.SizeSuffix(3584, 0) + "]");

            foreach (var size in new long[] { 0, 5, 6 })
            {
                Line("fileutil.isgreater.5." + size, FileUtil.IsFileGreater(samplePath, size).ToString().ToLowerInvariant());
            }

            // GetFiles: '|' separated search patterns, sorted.
            var globDir = Path.Combine(ScratchDir, "glob");
            Directory.CreateDirectory(Path.Combine(globDir, "sub"));
            foreach (var f in new[] { "b.gif", "a.png", "c.html", "sub/d.png", "note.txt" })
            {
                var full = Path.Combine(globDir, f.Replace('/', Path.DirectorySeparatorChar));
                Directory.CreateDirectory(Path.GetDirectoryName(full));
                File.WriteAllText(full, "x");
            }
            Line("fileutil.getfiles.topmost", string.Join(",", FileUtil.GetFiles(globDir, "*.png|*.gif", SearchOption.TopDirectoryOnly).Select(Path.GetFileName)));
            Line("fileutil.getfiles.all", string.Join(",", FileUtil.GetFiles(globDir, "*.png|*.gif|*.html", SearchOption.AllDirectories).Select(Path.GetFileName)));
            Line("fileutil.getfiles.none", string.Join(",", FileUtil.GetFiles(globDir, "*.webp", SearchOption.TopDirectoryOnly).Select(Path.GetFileName)));

            Line("fileutil.dirsize", FileUtil.GetDirectorySize(globDir).ToString(CultureInfo.InvariantCulture));

            // JsonUtil.Write: JObject.ToString() (indented) written with no
            // trailing newline — pins the byte shape for JsonUtil callers.
            var jobj = new Newtonsoft.Json.Linq.JObject
            {
                ["b"] = 1,
                ["a"] = new Newtonsoft.Json.Linq.JArray(1, 2),
                ["nested"] = new Newtonsoft.Json.Linq.JObject { ["x"] = null },
                ["text"] = "line1\nline2",
            };
            var jsonUtilPath = Path.Combine(ScratchDir, "jsonutil.json");
            JsonUtil.Write(jsonUtilPath, jobj);
            Line("jsonutil.write", Escaped(File.ReadAllText(jsonUtilPath)));
        }

        // -------------------------------------------------- wallpaper extensions

        private static void EmitWallpaperExtensions()
        {
            foreach (WallpaperType t in Enum.GetValues(typeof(WallpaperType)))
            {
                var prefix = "wallpaperext." + t;
                Line(prefix + ".online", t.IsOnlineWallpaper().ToString().ToLowerInvariant());
                Line(prefix + ".web", t.IsWebWallpaper().ToString().ToLowerInvariant());
                Line(prefix + ".video", t.IsVideoWallpaper().ToString().ToLowerInvariant());
                Line(prefix + ".app", t.IsApplicationWallpaper().ToString().ToLowerInvariant());
                Line(prefix + ".media", t.IsMediaWallpaper().ToString().ToLowerInvariant());
                Line(prefix + ".localweb", t.IsLocalWebWallpaper().ToString().ToLowerInvariant());
                Line(prefix + ".directoryproject", t.IsDirectoryProject().ToString().ToLowerInvariant());
                Line(prefix + ".deviceinput", t.IsDeviceInputAllowed().ToString().ToLowerInvariant());
            }
        }

        // --------------------------------------------------------------- data

        private static void EmitLanguages()
        {
            var langs = Languages.SupportedLanguages;
            Line("languages.count", langs.Count.ToString(CultureInfo.InvariantCulture));
            for (var i = 0; i < langs.Count; i++)
            {
                Line($"languages.{i}", langs[i].DisplayName + "|" + langs[i].Code);
            }
        }

        private static void EmitWindowClassExclusions()
        {
            var classes = WindowClassExclusions.DesktopClasses.OrderBy(x => x, StringComparer.Ordinal).ToList();
            Line("windowclasses.count", classes.Count.ToString(CultureInfo.InvariantCulture));
            for (var i = 0; i < classes.Count; i++)
            {
                Line($"windowclasses.{i}", classes[i]);
            }
            Line("windowclasses.case-insensitive", WindowClassExclusions.DesktopClasses.Contains("workerw").ToString().ToLowerInvariant());
        }

        // -------------------------------------------------------- localization

        private static void EmitLocalization()
        {
            var locPath = Path.Combine(ScratchDir, "LivelyInfo.loc.json");
            File.WriteAllText(locPath, @"{
  ""Languages"": {
    ""en-US"": { ""Title"": ""English title"", ""Desc"": ""English desc"" },
    ""zh-CN"": { ""Title"": ""中文标题"" },
    ""zh"":    { ""Title"": ""base zh"" }
  }
}");

            // C# would read CultureInfo.CurrentUICulture.Name for the empty code;
            // the port takes the OS UI language instead (lively::common::ui_language).
            // Both sides use the pinned code here so the golden stays stable.
            foreach (var lang in new[] { "fr-FR", "en-US", "zh-CN", "ZH-CN", "zh", "zh-Hans", "es-ES" })
            {
                var info = LivelyInfoUtil.GetLocalized(locPath, lang);
                Line("livelyinfoutil.localized." + lang,
                    info == null ? "<null>" : V(info.Title) + "|" + V(info.Desc));
            }

            Line("livelyinfoutil.missing", LivelyInfoUtil.GetLocalized(Path.Combine(ScratchDir, "nope.json"), "en-US") == null ? "<null>" : "value");
            var corrupt = Path.Combine(ScratchDir, "corrupt.loc.json");
            File.WriteAllText(corrupt, "{ not json");
            Line("livelyinfoutil.corrupt", LivelyInfoUtil.GetLocalized(corrupt, "en-US") == null ? "<null>" : "value");
        }

        // ------------------------------------------------------ library factory

        private static void EmitLibraryFactory()
        {
            var factory = new WallpaperLibraryFactory();

            foreach (var (caseName, type, absolute, file, preview, thumb) in new[]
            {
                ("absapp", WallpaperType.app, true, "main.exe", "prev.png", "thumb.png"),
                ("relweb", WallpaperType.web, false, "index.html", "prev.png", null),
                // Online wallpapers keep the URL itself in FileName, so the
                // metadata is *not* an absolute local path.
                ("online", WallpaperType.url, false, "https://example.com/wall", null, null),
                ("video", WallpaperType.video, false, "clip.mp4", null, "thumb.png"),
                ("mediaonly", WallpaperType.picture, false, "pic.png", "prev.png", null),
                ("missing-all", WallpaperType.web, false, "index.html", "nope.png", "nope.png"),
            })
            {
                var dir = Path.Combine(ScratchDir, "lib", caseName);
                Directory.CreateDirectory(dir);
                foreach (var f in new[] { file, preview, thumb })
                {
                    // "nope.png" entries stay absent on purpose: they are the
                    // existence-verification branch.
                    if (string.IsNullOrEmpty(f) || f.StartsWith("http") || f.StartsWith("nope")) continue;
                    var name = Path.GetFileName(f);
                    var full = Path.Combine(dir, name);
                    File.WriteAllText(full, "x");
                    if (name == "main.exe") File.WriteAllText(Path.Combine(dir, "LivelyProperties.json"), "{}");
                }

                var meta = new LivelyInfoModel
                {
                    Title = caseName,
                    Type = type,
                    IsAbsolutePath = absolute,
                    FileName = absolute && !file.StartsWith("http", StringComparison.Ordinal)
                        ? Path.Combine(dir, file)
                        : file,
                    Desc = "d",
                    Author = "a",
                    Preview = preview,
                    Thumbnail = thumb,
                };
                JsonStorage<LivelyInfoModel>.StoreData(Path.Combine(dir, "LivelyInfo.json"), meta);

                var lib = factory.CreateFromDirectory(dir);
                Line("factory.createdirectory." + caseName, DescribeLibrary(lib));
                if (type == WallpaperType.app) Line("factory.app-props-exists." + caseName,
                    File.Exists(lib.LivelyPropertyPath) ? "file" : "<missing>");
            }

            Line("factory.metadata.missing",
                Throws(() => factory.GetMetadata(Path.Combine(ScratchDir, "nope-dir"))) ?? "<none>");

            var badJsonDir = Path.Combine(ScratchDir, "lib", "badjson");
            Directory.CreateDirectory(badJsonDir);
            File.WriteAllText(Path.Combine(badJsonDir, "LivelyInfo.json"), "not json");
            Line("factory.metadata.corrupt",
                Throws(() => factory.GetMetadata(badJsonDir)) ?? "<none>");

            // CreateFromMetadata: image falls back to the thumbnail when there is no preview.
            var fromMetaNoPreview = factory.CreateFromMetadata(new LivelyInfoModel
            {
                Title = "t", Desc = "d", Author = "a", Preview = null, Thumbnail = "thumb.png",
            });
            Line("factory.frommetadata.nopreview",
                $"{V(fromMetaNoPreview.Title)}|{V(fromMetaNoPreview.ImagePath)}|{V(fromMetaNoPreview.FilePath)}");

            // CreateWallpaperPackage: online vs local, and the already-packaged guard.
            foreach (var (label, path, type) in new[]
            {
                ("online", "https://example.com/gallery/wall-42", WallpaperType.url),
                // Scratch-relative on purpose: the emitted LivelyInfo.json below is
                // byte-compared, so it must not contain a machine-specific root.
                ("videofile", "newvideo/my clip.mp4", WallpaperType.video),
            })
            {
                var dest = Path.Combine(ScratchDir, "packages", label);
                var meta = factory.CreateWallpaperPackage(path, dest, type);
                Line("factory.createpackage." + label, DescribeInfo(meta));
                Line("factory.createpackage." + label + ".file",
                    Escaped(File.ReadAllText(Path.Combine(dest, "LivelyInfo.json"))));
            }

            var packagedDir = Path.Combine(ScratchDir, "lib", "absapp");
            Line("factory.createpackage.already-packaged",
                Throws(() => factory.CreateWallpaperPackage(
                    Path.Combine(packagedDir, "main.exe"), Path.Combine(ScratchDir, "packages", "dupe"),
                    WallpaperType.app)) ?? "<none>");
        }

        private static string DescribeLibrary(LibraryModel lib) => string.Join(";",
            "title=" + V(lib.Title),
            "desc=" + V(lib.Desc),
            "author=" + V(lib.Author),
            "subscribed=" + (lib.IsSubscribed ? "true" : "false"),
            "file=" + Rel(lib.FilePath),
            "folder=" + Rel(lib.LivelyInfoFolderPath),
            "locpath=" + Rel(lib.LivelyInfoLocalizationPath),
            "proploc=" + Rel(lib.LivelyPropertyLocalizationPath),
            "prop=" + Rel(lib.LivelyPropertyPath),
            "preview=" + Rel(lib.PreviewClipPath),
            "thumb=" + Rel(lib.ThumbnailPath),
            "image=" + Rel(lib.ImagePath));

        // |brackets| make empty-vs-null visible in the transcript; Text() keeps
        // each check on exactly one line.
        private static string V(string s) => s == null ? "<null>" : "[" + Text(s) + "]";

        private static string Rel(string s) => s == null ? "<null>" : "[" + Text(PathRoots(s)) + "]";

        internal static string Text(string s)
        {
            var sb = new StringBuilder();
            foreach (var c in s)
            {
                switch (c)
                {
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default: sb.Append(c); break;
                }
            }
            return sb.ToString();
        }

        // Relativize both the probe scratch dir and the app's own LOCALAPPDATA
        // paths (Constants.CommonPaths.TempVideoDir leaks into media wallpapers).
        internal static string PathRoots(string p)
        {
            var result = p;
            if (result.StartsWith(ScratchDir, StringComparison.OrdinalIgnoreCase))
            {
                result = "<root>" + result.Substring(ScratchDir.Length);
            }
            var local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            if (result.StartsWith(local, StringComparison.OrdinalIgnoreCase))
            {
                result = "<lap>" + result.Substring(local.Length);
            }
            return result.Replace('\\', '/');
        }

        private static string Throws(Action action)
        {
            try
            {
                action();
                return null;
            }
            catch (Exception ex)
            {
                return ex.GetType().Name;
            }
        }

        // ---------------------------------------------------------- transcript

        private static string ScratchDir =>
            _scratch ??= Path.Combine(Path.GetTempPath(), "lively_library_probe");

        private static string _scratch;

        private static void Line(string key, string value) => Out.Append(key).Append('\t').Append(value).Append('\n');

        private static string Escape(string s)
        {
            var sb = new StringBuilder();
            foreach (var c in s)
            {
                switch (c)
                {
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    case '|': sb.Append("\\p"); break;
                    default:
                        if (c < 0x20) sb.Append("\\x").Append(((int)c).ToString("x2"));
                        else sb.Append(c);
                        break;
                }
            }
            return sb.Length == 0 ? "<empty>" : sb.ToString();
        }

        // Payloads (JSON documents) are escaped onto one line so the golden file
        // stays diffable: \r\n shows up literally as \r\n.
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
    }
}
