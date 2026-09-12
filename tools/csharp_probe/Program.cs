// Golden fixture generator: instantiates every IpcMessage type from
// Lively.Models and prints the exact Newtonsoft.Json serialization the real
// C# code produces. Each line: name<TAB>json. The C++ golden tests must match
// these byte-for-byte (compact formatting = JsonConvert.SerializeObject).
//
// Mode "props": parses a real LivelyProperties.json (+ optional localization
// file) with the genuine LivelyControlModelConverter and prints one
// "key<TAB>describe" line per control, before and after localization —
// the oracle for the C++ LivelyProperty parser.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using Lively.Common.Helpers;
using Lively.Models.LivelyControls;
using Lively.Models.Message;
using Newtonsoft.Json;

namespace csharp_probe
{
    internal static class Program
    {
        private static int Main(string[] args)
        {
            Console.OutputEncoding = Encoding.UTF8;

            if (args.Length > 0 && args[0] == "props")
            {
                return RunPropsMode(args);
            }
            if (args.Length > 0 && args[0] == "fuzz")
            {
                return FuzzProbe.Run(args);
            }
            if (args.Length > 0 && args[0] == "grpcserver")
            {
                return GrpcServerProbe.Run(args);
            }
            if (args.Length > 0 && args[0] == "settings")
            {
                return SettingsProbe.Run();
            }
            if (args.Length > 0 && args[0] == "settings-roundtrip")
            {
                return SettingsProbe.RunRoundtrip();
            }
            if (args.Length > 0 && args[0] == "players")
            {
                return StartArgsProbe.Run(args);
            }

            Emit("LivelyCloseCmd.default", new LivelyCloseCmd());
            Emit("LivelySuspendCmd.default", new LivelySuspendCmd());
            Emit("LivelyResumeCmd.default", new LivelyResumeCmd());
            Emit("LivelyReloadCmd.default", new LivelyReloadCmd());

            var volume = new LivelyVolumeCmd();
            Emit("LivelyVolumeCmd.default", volume);
            volume.Volume = 42;
            Emit("LivelyVolumeCmd.v42", volume);

            var hwnd = new LivelyMessageHwnd();
            hwnd.Hwnd = 65535;
            Emit("LivelyMessageHwnd.65535", hwnd);

            var wploaded = new LivelyMessageWallpaperLoaded();
            wploaded.Success = true;
            Emit("LivelyMessageWallpaperLoaded.success", wploaded);

            var console = new LivelyMessageConsole();
            console.Message = "hello";
            console.Category = ConsoleMessageType.error;
            Emit("LivelyMessageConsole.error", console);
            Emit("LivelyMessageConsole.nullMessage", new LivelyMessageConsole());

            var slider = new LivelySlider();
            slider.Name = "Speed";
            slider.Value = 1.5;
            slider.Step = 0.5;
            Emit("LivelySlider.populated", slider);

            var textbox = new LivelyTextBox();
            textbox.Name = "Note";
            textbox.Value = "abc";
            Emit("LivelyTextBox.populated", textbox);

            var checkbox = new LivelyCheckbox();
            checkbox.Name = "Enabled";
            checkbox.Value = true;
            Emit("LivelyCheckbox.checked", checkbox);

            var dropdown = new LivelyDropdown();
            dropdown.Name = "Quality";
            dropdown.Value = 2;
            Emit("LivelyDropdown.populated", dropdown);

            var button = new LivelyButton();
            button.Name = "Apply";
            button.IsDefault = true;
            Emit("LivelyButton.default", button);

            var colorPicker = new LivelyColorPicker();
            colorPicker.Name = "Tint";
            colorPicker.Value = "#FFAA00";
            Emit("LivelyColorPicker.populated", colorPicker);

            // Unicode / escaping probe (CJK + quote + newline) on a console msg.
            var unicode = new LivelyMessageConsole();
            unicode.Message = "壁紙 \"test\"\nline2";
            unicode.Category = ConsoleMessageType.log;
            Emit("LivelyMessageConsole.unicode", unicode);

            return 0;
        }

        private static void Emit(string name, IpcMessage message)
        {
            // Compact, no spaces — same as JsonConvert.SerializeObject(msg).
            Console.WriteLine($"{name}\t{JsonConvert.SerializeObject(message)}");
        }

        // ---- props mode ----

        private static int RunPropsMode(string[] args)
        {
            // usage: props <properties.json> [loc.json:lang [loc.json:lang ...]]
            if (args.Length < 2)
            {
                Console.Error.WriteLine("usage: props <properties.json> [loc.json:lang ...]");
                return 2;
            }

            Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
            Thread.CurrentThread.CurrentUICulture = CultureInfo.InvariantCulture;

            var propertyPath = args[1];
            if (!File.Exists(propertyPath))
            {
                Console.Error.WriteLine($"missing: {propertyPath}");
                return 2;
            }

            var controls = LivelyPropertyUtil.GetControls(propertyPath);
            foreach (var pair in controls)
            {
                Console.WriteLine($"{pair.Key}\t{Describe(pair.Key, pair.Value)}");
            }

            // Localization passes: loc.json:lang
            for (int i = 2; i < args.Length; i++)
            {
                var split = args[i].IndexOf(':');
                if (split < 0)
                {
                    Console.Error.WriteLine($"bad loc arg: {args[i]}");
                    return 2;
                }
                var locPath = args[i].Substring(0, split);
                var lang = args[i].Substring(split + 1);
                if (!File.Exists(locPath))
                {
                    Console.Error.WriteLine($"missing: {locPath}");
                    return 2;
                }
                LivelyPropertyUtil.LocalizeControls(locPath, controls, lang);
                Console.WriteLine($"# localized {lang}");
                foreach (var pair in controls)
                {
                    Console.WriteLine($"{pair.Key}\t{Describe(pair.Key, pair.Value)}");
                }
            }

            return 0;
        }

        // Deterministic per-control description; mirrored by
        // lively::models::describe_control in C++.
        private static string Describe(string key, ControlModel control)
        {
            var sb = new StringBuilder();
            sb.Append(control.Type).Append("|name=").Append(control.Name)
              .Append("|text=").Append(control.Text ?? "")
              .Append("|help=").Append(control.Help ?? "");
            switch (control)
            {
                case SliderModel s:
                    sb.Append("|tick=").Append(s.Tick)
                      .Append("|min=").Append(s.Min.ToString(CultureInfo.InvariantCulture))
                      .Append("|max=").Append(s.Max.ToString(CultureInfo.InvariantCulture))
                      .Append("|value=").Append(s.Value.ToString(CultureInfo.InvariantCulture))
                      .Append("|step=").Append(s.Step.ToString(CultureInfo.InvariantCulture));
                    break;
                case DropdownModel d:
                    sb.Append("|value=").Append(d.Value).Append("|items=").Append(d.Items?.Length ?? 0);
                    break;
                case ScalerDropdownModel sd:
                    sb.Append("|value=").Append(sd.Value).Append("|items=").Append(sd.Items?.Length ?? 0);
                    break;
                case FolderDropdownModel f:
                    sb.Append("|value=").Append(f.Value ?? "")
                      .Append("|folder=").Append(f.Folder ?? "")
                      .Append("|filter=").Append(f.Filter ?? "");
                    break;
                case CheckboxModel c:
                    sb.Append("|value=").Append(c.Value ? "True" : "False");
                    break;
                case TextboxModel t:
                    sb.Append("|value=").Append(t.Value ?? "");
                    break;
                case ColorPickerModel cp:
                    sb.Append("|value=").Append(cp.Value ?? "");
                    break;
                case LabelModel l:
                    sb.Append("|value=").Append(l.Value ?? "");
                    break;
                case ButtonModel b:
                    sb.Append("|value=").Append(b.Value ?? "");
                    break;
            }
            _ = key;
            return sb.ToString();
        }
    }
}
