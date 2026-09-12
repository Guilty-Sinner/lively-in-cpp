using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using CommandLine;
using Lively.Common;

namespace csharp_probe
{
    // Differential fuzzing oracle: parses candidate argv with the REAL
    // CommandLineParser + Lively.Common.CommandlineArgs verbs, then prints a
    // canonical outcome line: "OK <verb> key=value;..." or "ERR <sorted errors>".
    // tools/fuzz_differential.py generates the same candidate strings and feeds
    // them to the C++ parser (lively_fuzz_target), then diffs the outputs.
    public static class FuzzProbe
    {
        public static int Run(string[] args)
        {
            if (args.Length < 2)
            {
                Console.Error.WriteLine("usage: fuzz <args-file>");
                return 2;
            }
            Console.OutputEncoding = Encoding.UTF8;
            var cases = File.ReadAllLines(args[1]);
            foreach (var raw in cases)
            {
                var line = raw.Length >= 2 && raw[0] == '"' && raw[raw.Length - 1] == '"'
                    ? raw.Substring(1, raw.Length - 2)
                    : raw;
                Console.WriteLine($"{line}\t{ParseOne(line)}");
                Console.Out.Flush();
            }
            return 0;
        }

        private static string ParseOne(string line)
        {
            var argv = string.IsNullOrWhiteSpace(line)
                ? Array.Empty<string>()
                : line.Split(' ');
            string outcome = null;
            Parser.Default.ParseArguments<
                CommandlineArgs.AppOptions,
                CommandlineArgs.SetWallpaperOptions,
                CommandlineArgs.CloseWallpaperOptions,
                CommandlineArgs.SeekWallpaperOptions,
                CommandlineArgs.CustomiseWallpaperOptions,
                CommandlineArgs.ScreenSaverOptions,
                CommandlineArgs.ScreenshotOptions>(argv)
                .WithParsed<object>(opts => outcome = Describe(opts))
                .WithNotParsed(errors => outcome = "ERR " + DescribeErrors(errors));
            return outcome ?? "ERR unknown";
        }

        private static string Describe(object opts)
 => opts switch
        {
            CommandlineArgs.AppOptions o => Verb("app") + Opt("showApp", o.ShowApp)
                + Opt("showIcons", o.ShowIcons) + OptStr("volume", o.Volume)
                + Opt("play", o.Play) + Opt("startup", o.Startup)
                + Opt("shutdown", o.ShutdownApp) + Opt("restart", o.RestartApp)
                + OptStr("layout", o.WallpaperArrangement),
            CommandlineArgs.SetWallpaperOptions o => Verb("setwp") + OptStr("file", o.File)
                + Opt("monitor", o.Monitor),
            CommandlineArgs.CloseWallpaperOptions o => Verb("closewp") + Opt("monitor", o.Monitor),
            CommandlineArgs.SeekWallpaperOptions o => Verb("seekwp") + OptStr("value", o.Param)
                + Opt("monitor", o.Monitor),
            CommandlineArgs.CustomiseWallpaperOptions o => Verb("setprop") + OptStr("property", o.Param)
                + Opt("monitor", o.Monitor),
            CommandlineArgs.ScreenSaverOptions o => Verb("screensaver")
                + Opt("preview", o.Preview) + Opt("configure", o.Configure)
                + Opt("show", o.Show) + Opt("showExclusive", o.ShowExclusive)
                + Opt("fadeIn", o.IsFadeIn),
            CommandlineArgs.ScreenshotOptions o => Verb("screenshot") + OptStr("file", o.File)
                + Opt("monitor", o.Monitor),
            _ => "ERR unknown-type",
        };

        private static string Verb(string v) => "OK " + v;

        private static string Opt(string name, bool? v) =>
            v.HasValue ? $";{name}={v.Value.ToString().ToLowerInvariant()}" : "";

        private static string Opt(string name, int? v) =>
            v.HasValue ? $";{name}={v.Value.ToString(CultureInfo())}" : "";

        private static string OptStr(string name, string v) =>
            v != null ? $";{name}={v}" : "";

        private static System.IFormatProvider CultureInfo() =>
            System.Globalization.CultureInfo.InvariantCulture;

        private static string DescribeErrors(IEnumerable<Error> errors)
        {
            var names = errors.Select(e =>
            {
                var tag = e.Tag.ToString();
                // Include the offending token where the library exposes it.
                return e switch
                {
                    UnknownOptionError u => tag + ":" + u.Token,
                    _ => tag,
                };
            });
            return string.Join(",", names);
        }
    }
}
