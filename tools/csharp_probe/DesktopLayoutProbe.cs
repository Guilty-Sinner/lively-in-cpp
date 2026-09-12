using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Lively.Models;
using Lively.Models.Enums;

namespace csharp_probe
{
    // Oracle for the *decision* half of Lively/Core/WinDesktopCore.cs.
    //
    // The C# logic itself is not callable here — it lives inside WinDesktopCore
    // behind WorkerW parenting, SetWindowPos and the wallpaper factory. So this
    // probe re-expresses the same rules, using the LINQ expressions COPIED
    // VERBATIM from the source (FindAll / Find / FirstOrDefault / RemoveAll with
    // the same predicates, in the same order). That is what makes the comparison
    // meaningful: the C++ port implements the rules with ordinary loops, so the
    // two implementations differ mechanically even though the predicates match.
    // A transcription error in the predicates here is the residual risk, which is
    // why each block names its source line.
    //
    // Scenarios are synthetic DisplayMonitor/WallpaperSlot sets; nothing touches
    // Win32.
    public static class DesktopLayoutProbe
    {
        private static readonly StringBuilder Out = new StringBuilder();

        private sealed class Slot
        {
            public DisplayMonitor Screen;
            public string Path;
        }

        // DisplayMonitor.Equals compares DeviceId only (IEquatable).
        private static DisplayMonitor Disp(string id, int x, int y, int w, int h, bool primary = false)
        {
            return new DisplayMonitor
            {
                DeviceId = id,
                DeviceName = id,
                DisplayName = id,
                IsPrimary = primary,
                Bounds = new System.Drawing.Rectangle(x, y, w, h),
                WorkingArea = new System.Drawing.Rectangle(x, y, w, h),
            };
        }

        private static Slot Wp(DisplayMonitor screen, string path) => new Slot { Screen = screen, Path = path };

        // A fresh 3-screen wallpaper list — RefreshWallpaper removes orphans from it,
        // so scenarios must not share one.
        private static List<Slot> NewWallpapers() => new List<Slot>
        {
            Wp(Disp("P", 0, 0, 1920, 1080, primary: true), "wp-a"),
            Wp(Disp("S", 1920, 0, 1920, 1080), "wp-b"),
            Wp(Disp("T", 3840, 0, 1280, 1024), "wp-c"),
        };

        public static int Run()
        {
            Console.OutputEncoding = Encoding.UTF8;

            var primary = Disp("P", 0, 0, 1920, 1080, primary: true);
            var secondary = Disp("S", 1920, 0, 1920, 1080);
            var third = Disp("T", 3840, 0, 1280, 1024);

            // ---- RefreshWallpaper(): one screen removed, per arrangement --------
            foreach (var arrangement in new[] { WallpaperArrangement.per, WallpaperArrangement.span, WallpaperArrangement.duplicate })
            {
                // NOTE: RefreshWallpaper MUTATES `wallpapers` (RemoveAll), so every
                // scenario gets its own list — reusing one would be testing
                // idempotence, not the rule under test.
                var visible = new List<DisplayMonitor> { primary, secondary };  // third unplugged

                Line($"refresh.removed.{arrangement}", RefreshReport(
                    NewWallpapers(), visible, new List<WallpaperLayoutModel>(), arrangement, secondary));

                // The same, but the orphan's display is ALREADY queued: the dedupe
                // must suppress a second entry.
                Line($"refresh.removed.dedup.{arrangement}", RefreshReport(
                    NewWallpapers(), visible,
                    new List<WallpaperLayoutModel> { new WallpaperLayoutModel(third, "wp-c") },
                    arrangement, secondary));

                // The dedupe compares DeviceId only, so a queue entry for the same
                // screen with DIFFERENT bounds still suppresses the new one.
                Line($"refresh.removed.dedup-stale-bounds.{arrangement}", RefreshReport(
                    NewWallpapers(), visible,
                    new List<WallpaperLayoutModel> { new WallpaperLayoutModel(Disp("T", 0, 0, 1, 1), "wp-old") },
                    arrangement, secondary));

                // Nothing changes: no orphans.
                Line($"refresh.stable.{arrangement}", RefreshReport(
                    NewWallpapers(), new List<DisplayMonitor> { primary, secondary, third },
                    new List<WallpaperLayoutModel>(), arrangement, secondary));

                // Repeated calls on the SAME live list: the orphan is gone after the
                // first pass, so the second pass does nothing.
                var live = NewWallpapers();
                RefreshReport(live, visible, new List<WallpaperLayoutModel>(), arrangement, secondary);
                Line($"refresh.twice.{arrangement}", RefreshReport(
                    live, visible, new List<WallpaperLayoutModel>(), arrangement, secondary));
            }

            // The selected display was itself unplugged -> falls back to primary.
            {
                var wallpapers = new List<Slot> { Wp(primary, "wp-a") };
                Line("refresh.selected.fallback",
                    RefreshReport(wallpapers, new List<DisplayMonitor> { primary }, new List<WallpaperLayoutModel>(),
                        WallpaperArrangement.per, third));
                Line("refresh.selected.kept",
                    RefreshReport(wallpapers, new List<DisplayMonitor> { primary, secondary }, new List<WallpaperLayoutModel>(),
                        WallpaperArrangement.per, secondary));
            }

            // ---- UpdateWallpaperRect() -----------------------------------------
            foreach (var arrangement in new[] { WallpaperArrangement.per, WallpaperArrangement.span, WallpaperArrangement.duplicate })
            {
                var single = new List<Slot> { Wp(primary, "wp-a") };
                Line($"rect.single.{arrangement}", RectReport(single, new List<DisplayMonitor> { primary }, arrangement));

                var two = new List<Slot> { Wp(primary, "wp-a"), Wp(secondary, "wp-b") };
                Line($"rect.two.{arrangement}", RectReport(two, new List<DisplayMonitor> { primary, secondary }, arrangement));

                // An orphan still in the list must not produce an update.
                var withOrphan = new List<Slot> { Wp(primary, "wp-a"), Wp(third, "wp-c") };
                Line($"rect.orphan.{arrangement}", RectReport(withOrphan, new List<DisplayMonitor> { primary }, arrangement));
            }

            // A reconnected screen with different bounds: the rect is rebased on the
            // virtual-screen origin, so a negative-origin layout matters.
            {
                var movedPrimary = Disp("P", -1920, -200, 1920, 1080, primary: true);
                var movedSecondary = Disp("S", 0, 0, 2560, 1440);
                var wallpapers = new List<Slot> { Wp(movedPrimary, "wp-a"), Wp(movedSecondary, "wp-b") };
                var displayed = new List<DisplayMonitor> { movedPrimary, movedSecondary };
                Line("rect.negative-origin.per", RectReport(wallpapers, displayed, WallpaperArrangement.per));
                Line("rect.negative-origin.span", RectReport(wallpapers, displayed, WallpaperArrangement.span));
            }

            // ---- RestoreDisconnectedWallpapers() --------------------------------
            foreach (var arrangement in new[] { WallpaperArrangement.per, WallpaperArrangement.span, WallpaperArrangement.duplicate })
            {
                var wallpapers = new List<Slot> { Wp(primary, "wp-a") };
                var displayed = new List<DisplayMonitor> { primary, secondary, third };
                var disconnected = new List<WallpaperLayoutModel>
                {
                    new WallpaperLayoutModel(secondary, "wp-b"),
                    new WallpaperLayoutModel(Disp("GONE", 5000, 0, 800, 600), "wp-x"),
                };
                Line($"restoreDisconnected.{arrangement}", RestoreDisconnectedReport(wallpapers, displayed, disconnected, arrangement));
            }

            // duplicate with every screen already covered -> no call.
            {
                var wallpapers = new List<Slot> { Wp(primary, "wp-a"), Wp(secondary, "wp-b") };
                Line("restoreDisconnected.duplicate.full",
                    RestoreDisconnectedReport(wallpapers, new List<DisplayMonitor> { primary, secondary },
                        new List<WallpaperLayoutModel>(), WallpaperArrangement.duplicate));
                // and with no wallpapers at all -> the `Wallpapers.Count != 0` guard.
                Line("restoreDisconnected.duplicate.empty",
                    RestoreDisconnectedReport(new List<Slot>(), new List<DisplayMonitor> { primary, secondary },
                        new List<WallpaperLayoutModel>(), WallpaperArrangement.duplicate));
            }

            // ---- RestoreWallpaper(layout) --------------------------------------
            //
            // The decisive detail: WallpaperLayoutModel does not override Equals, so
            // `wallpapersDisconnected.Contains(layout)` and `.Remove(layout)` are
            // REFERENCE comparisons. The two call sites hand in different kinds of
            // object — RestoreDisconnectedWallpapers passes the queue's own entries,
            // while the startup path passes freshly deserialized ones — so the same
            // layout file behaves differently depending on how it was reached. Each
            // scenario gets a fresh queue so one cannot affect the next.
            var attached = new List<DisplayMonitor> { primary, secondary };
            var missing = new List<DisplayMonitor> { primary, secondary };  // no `third`

            // (a) sources ARE the queue's own objects, screen missing.
            {
                var queue = new List<WallpaperLayoutModel> { new WallpaperLayoutModel(third, "wp-t") };
                Line("restoreApply.identity.missing",
                    RestoreApplyReport(queue.ToList(), queue, missing, _ => true));
            }
            // (b) identical contents, fresh references -> Contains misses, so a
            //     duplicate is queued.
            {
                var queue = new List<WallpaperLayoutModel> { new WallpaperLayoutModel(third, "wp-t") };
                var fresh = new List<WallpaperLayoutModel> { new WallpaperLayoutModel(third, "wp-t") };
                Line("restoreApply.fresh.missing", RestoreApplyReport(fresh, queue, missing, _ => true));
            }
            // (c) screen CONNECTED, entry is already queued by reference -> restored
            //     and removed.
            {
                var queue = new List<WallpaperLayoutModel> { new WallpaperLayoutModel(secondary, "wp-b") };
                Line("restoreApply.identity.connected",
                    RestoreApplyReport(queue.ToList(), queue, attached, _ => true));
            }
            // (d) screen CONNECTED, fresh reference -> restored, but Remove misses so
            //     the stale queue entry survives.
            {
                var queue = new List<WallpaperLayoutModel> { new WallpaperLayoutModel(secondary, "wp-b") };
                var fresh = new List<WallpaperLayoutModel> { new WallpaperLayoutModel(secondary, "wp-b") };
                Line("restoreApply.fresh.connected", RestoreApplyReport(fresh, queue, attached, _ => true));
            }
            // (e) unreadable library directory -> dropped from the queue.
            {
                var queue = new List<WallpaperLayoutModel> { new WallpaperLayoutModel(secondary, "wp-bad") };
                Line("restoreApply.unreadable",
                    RestoreApplyReport(queue.ToList(), queue, attached, p => p != "wp-bad"));
            }
            // (f) a null Display — `x.Equals(entry.Display)` dereferences null, so the
            //     C# throws. Recorded so the port's (deliberately gentler) behaviour is
            //     a documented deviation rather than a silent difference.
            {
                var queue = new List<WallpaperLayoutModel>();
                try
                {
                    Line("restoreApply.nullDisplay",
                        RestoreApplyReport(new List<WallpaperLayoutModel> { new WallpaperLayoutModel(null, "wp-null") },
                            queue, attached, _ => true));
                }
                catch (Exception ex)
                {
                    Line("restoreApply.nullDisplay", "threw:" + ex.GetType().Name);
                }
            }

            // ---- SaveWallpaperLayout() -----------------------------------------
            foreach (var arrangement in new[] { WallpaperArrangement.per, WallpaperArrangement.span, WallpaperArrangement.duplicate })
            {
                var wallpapers = new List<Slot> { Wp(primary, "wp-a"), Wp(secondary, "wp-b") };
                var disconnected = new List<WallpaperLayoutModel> { new WallpaperLayoutModel(third, "wp-t") };
                Line($"compose.{arrangement}", ComposeReport(wallpapers, disconnected, arrangement));
                // Duplicate entries are NOT deduped (the upstream dedupe is commented out).
                var dup = new List<WallpaperLayoutModel>
                {
                    new WallpaperLayoutModel(secondary, "wp-b"),
                    new WallpaperLayoutModel(third, "wp-t"),
                };
                Line($"compose.dupes.{arrangement}", ComposeReport(wallpapers, dup, arrangement));
            }

            // ---- RestoreWallpaper() (the public, startup one) -------------------
            {
                var layout = new List<WallpaperLayoutModel>
                {
                    new WallpaperLayoutModel(primary, "wp-a"),
                    new WallpaperLayoutModel(secondary, "wp-b"),
                };
                foreach (var arrangement in new[] { WallpaperArrangement.per, WallpaperArrangement.span, WallpaperArrangement.duplicate })
                {
                    Line($"startup.{arrangement}", StartupReport(layout, arrangement, primary));
                }
                Line("startup.empty.span", StartupReport(new List<WallpaperLayoutModel>(), WallpaperArrangement.span, primary));
            }

            Console.Write(Out.ToString());
            Console.Out.Flush();
            return 0;
        }

        // ---- the transcribed rules ------------------------------------------

        private static string RefreshReport(List<Slot> wallpapers, List<DisplayMonitor> allScreens,
            List<WallpaperLayoutModel> wallpapersDisconnected, WallpaperArrangement arrangement,
            DisplayMonitor selectedDisplay)
        {
            var sb = new StringBuilder();

            var orphanWallpapers = wallpapers.FindAll(
                wallpaper => allScreens.Find(screen => wallpaper.Screen.Equals(screen)) == null);

            var effectiveSelected = allScreens.Find(x => selectedDisplay.Equals(x)) ?? allScreens.First(x => x.IsPrimary);

            var queued = new List<WallpaperLayoutModel>();
            switch (arrangement)
            {
                case WallpaperArrangement.per:
                    if (orphanWallpapers.Count != 0)
                    {
                        var newOrphans = orphanWallpapers.FindAll(
                            oldOrphan => wallpapersDisconnected.Find(
                                newOrphan => newOrphan.Display.Equals(oldOrphan.Screen)) == null);
                        foreach (var item in newOrphans)
                        {
                            queued.Add(new WallpaperLayoutModel(item.Screen, item.Path));
                        }
                        wallpapers.RemoveAll(x => orphanWallpapers.Contains(x));
                    }
                    break;
                case WallpaperArrangement.duplicate:
                    if (orphanWallpapers.Count != 0)
                    {
                        wallpapers.RemoveAll(x => orphanWallpapers.Contains(x));
                    }
                    break;
                case WallpaperArrangement.span:
                    break;
            }

            sb.Append("selected=").Append(effectiveSelected.DeviceId);
            sb.Append(";orphans=").Append(orphanWallpapers.Count);
            foreach (var item in orphanWallpapers) sb.Append('[').Append(item.Screen.DeviceId).Append('|').Append(item.Path).Append(']');
            sb.Append(";queued=").Append(queued.Count);
            foreach (var item in queued) sb.Append('[').Append(item.Display.DeviceId).Append('|').Append(item.LivelyInfoPath).Append(']');
            sb.Append(";refreshDesktop=True");
            return sb.ToString();
        }

        private static string RectReport(List<Slot> wallpapers, List<DisplayMonitor> allScreens,
            WallpaperArrangement arrangement)
        {
            var sb = new StringBuilder();
            var isMulti = allScreens.Count > 1;
            var virtualBounds = Union(allScreens);
            var primary = allScreens.FirstOrDefault(x => x.IsPrimary) ?? allScreens[0];

            if (isMulti && arrangement == WallpaperArrangement.span)
            {
                sb.Append("span=True");
                if (wallpapers.Count != 0)
                {
                    sb.Append(";spanUpdate=0@").Append(primary.DeviceId).Append(":")
                      .Append($"0,0,{virtualBounds.Width},{virtualBounds.Height}");
                }
                else
                {
                    sb.Append(";spanUpdate=<none>");
                }
                sb.Append(";updates=0");
                return sb.ToString();
            }

            sb.Append("span=False;spanUpdate=<none>");
            var updates = new List<string>();
            foreach (var screen in allScreens.ToList())
            {
                var i = wallpapers.FindIndex(x => x.Screen.Equals(screen));
                if (i == -1) continue;
                updates.Add($"[{i}@{screen.DeviceId}:{screen.Bounds.X - virtualBounds.X},{screen.Bounds.Y - virtualBounds.Y},{screen.Bounds.Width},{screen.Bounds.Height}]");
            }
            sb.Append(";updates=").Append(updates.Count).Append(string.Concat(updates));
            return sb.ToString();
        }

        private static System.Drawing.Rectangle Union(List<DisplayMonitor> allScreens)
        {
            if (allScreens.Count == 0) return System.Drawing.Rectangle.Empty;
            var x = allScreens.Min(s => s.Bounds.X);
            var y = allScreens.Min(s => s.Bounds.Y);
            var r = allScreens.Max(s => s.Bounds.Right);
            var b = allScreens.Max(s => s.Bounds.Bottom);
            return new System.Drawing.Rectangle(x, y, r - x, b - y);
        }

        private static string RestoreDisconnectedReport(List<Slot> wallpapers, List<DisplayMonitor> allScreens,
            List<WallpaperLayoutModel> wallpapersDisconnected, WallpaperArrangement arrangement)
        {
            var sb = new StringBuilder();
            switch (arrangement)
            {
                case WallpaperArrangement.per:
                    var wallpapersToRestore = wallpapersDisconnected.FindAll(wallpaper => allScreens.FirstOrDefault(
                        screen => wallpaper.Display.Equals(screen)) != null);
                    sb.Append("toRestore=").Append(wallpapersToRestore.Count);
                    foreach (var item in wallpapersToRestore)
                        sb.Append('[').Append(item.Display.DeviceId).Append('|').Append(item.LivelyInfoPath).Append(']');
                    sb.Append(";duplicateScreen=<none>");
                    return sb.ToString();
                case WallpaperArrangement.span:
                    sb.Append("toRestore=0;duplicateScreen=<none>");
                    return sb.ToString();
                case WallpaperArrangement.duplicate:
                    sb.Append("toRestore=0;duplicateScreen=<none>");
                    if ((allScreens.Count > wallpapers.Count) && wallpapers.Count != 0)
                    {
                        var newScreen = allScreens.FirstOrDefault(screen => wallpapers.FirstOrDefault(
                            wp => wp.Screen.Equals(screen)) == null);
                        if (newScreen != null)
                        {
                            sb.Clear();
                            sb.Append("toRestore=0;duplicateScreen=").Append(newScreen.DeviceId)
                              .Append(";duplicateSourceIndex=0");
                        }
                    }
                    return sb.ToString();
            }
            return sb.ToString();
        }

        private static string RestoreApplyReport(List<WallpaperLayoutModel> layout,
            List<WallpaperLayoutModel> queue, List<DisplayMonitor> allScreens,
            Func<string, bool> readable)
        {
            // Mirrors WinDesktopCore.RestoreWallpaper. `queue` is the live
            // wallpapersDisconnected list and is mutated in place; membership and
            // removal are REFERENCE comparisons (no Equals override), so
            // `queue.IndexOf(entry)` is what actually decides each branch.
            var sb = new StringBuilder();
            var removed = new List<int>();

            foreach (var entry in layout)
            {
                sb.Append('[').Append(entry.LivelyInfoPath ?? "<null>");

                // Source order: CreateFromDirectory first, then the screen lookup.
                if (!readable(entry.LivelyInfoPath))
                {
                    sb.Append("|screen=<unreadable>");
                    sb.Append("|unreadable=True|queued=False|restored=False");
                    var idx0 = queue.IndexOf(entry);
                    sb.Append("|removed=").Append(idx0 != -1 ? "True" : "False");
                    if (idx0 != -1) { removed.Add(idx0); queue.RemoveAt(idx0); }
                    sb.Append(']');
                    continue;
                }

                var screen = allScreens.FirstOrDefault(x => x.Equals(entry.Display));
                sb.Append("|screen=").Append(screen == null ? "<missing>" : screen.DeviceId);
                sb.Append("|unreadable=False");
                if (screen == null)
                {
                    var contained = queue.Contains(entry);
                    sb.Append("|queued=").Append(!contained ? "True" : "False");
                    sb.Append("|restored=False");
                    sb.Append("|removed=False");
                    if (!contained) queue.Add(new WallpaperLayoutModel(entry.Display, entry.LivelyInfoPath));
                }
                else
                {
                    sb.Append("|queued=False|restored=True");
                    var idx = queue.IndexOf(entry);
                    sb.Append("|removed=").Append(idx != -1 ? "True" : "False");
                    if (idx != -1) { removed.Add(idx); queue.RemoveAt(idx); }
                }
                sb.Append(']');
            }

            sb.Append(";removedIndices=").Append(removed.Count);
            foreach (var i in removed) sb.Append('[').Append(i).Append(']');
            sb.Append(";queue=").Append(queue.Count);
            foreach (var item in queue)
                sb.Append('[').Append(item.Display?.DeviceId ?? "<none>").Append('|').Append(item.LivelyInfoPath ?? "<null>").Append(']');
            return sb.ToString();
        }

        private static string ComposeReport(List<Slot> wallpapers,
            List<WallpaperLayoutModel> wallpapersDisconnected, WallpaperArrangement arrangement)
        {
            var layout = new List<WallpaperLayoutModel>();
            foreach (var wallpaper in wallpapers)
                layout.Add(new WallpaperLayoutModel(wallpaper.Screen, wallpaper.Path));
            if (arrangement == WallpaperArrangement.per)
                layout.AddRange(wallpapersDisconnected);

            var sb = new StringBuilder();
            sb.Append(layout.Count);
            foreach (var item in layout)
                sb.Append('[').Append(item.Display?.DeviceId ?? "<none>").Append('|').Append(item.LivelyInfoPath ?? "<null>").Append(']');
            return sb.ToString();
        }

        private static string StartupReport(List<WallpaperLayoutModel> layout,
            WallpaperArrangement arrangement, DisplayMonitor primary)
        {
            var sb = new StringBuilder();
            if (arrangement == WallpaperArrangement.span || arrangement == WallpaperArrangement.duplicate)
            {
                sb.Append("replayEach=False;single=");
                if (layout.Count != 0) sb.Append(layout[0].LivelyInfoPath).Append(";screen=").Append(primary.DeviceId);
                else sb.Append("<none>");
            }
            else
            {
                sb.Append("replayEach=True;single=<none>");
            }
            return sb.ToString();
        }

        private static void Line(string key, string value) => Out.Append(key).Append('\t').Append(value).Append('\n');
    }
}
