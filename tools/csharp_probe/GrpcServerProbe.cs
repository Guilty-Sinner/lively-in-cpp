// Cross-language oracle: runs a REAL C# gRPC server built from Lively's own
// generated bindings (Lively.Grpc.Common → Grpc.Tools) — the same
// CommandsService.CommandsServiceBase that Lively/RPC/CommandsServer.cs
// overrides — but records wire requests to stdout instead of driving a desktop.
//
// The C++ ported CommandsClient (tests/test_rpc.cpp, [.crosslang]) connects to
// this server and must observe identical request content as with its own
// in-process C++ server. That proves the proto contract port is wire-compatible
// with the original C# ecosystem, in both directions.
using System;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using Lively.Grpc.Common.Proto.Commands;

namespace csharp_probe
{
    internal sealed class ProbeCommandsServer : CommandsService.CommandsServiceBase
    {
        // Canonical request descriptions — identical format to the C++
        // RecordingService in tests/test_rpc.cpp.
        private readonly StreamWriter record;

        public ProbeCommandsServer(string recordPath)
        {
            record = new StreamWriter(recordPath, append: false) { AutoFlush = true };
        }

        private void Record(string entry) => record.WriteLine("REC " + entry);

        public override Task<Empty> ShowUI(Empty _, ServerCallContext context)
        { Record("ShowUI"); return Task.FromResult(new Empty()); }

        public override Task<Empty> CloseUI(Empty _, ServerCallContext context)
        { Record("CloseUI"); return Task.FromResult(new Empty()); }

        public override Task<Empty> RestartUI(Empty _, ServerCallContext context)
        { Record("RestartUI"); return Task.FromResult(new Empty()); }

        public override Task<Empty> RestartUIWithArgs(RestartRequest request, ServerCallContext context)
        { Record("RestartUIWithArgs:" + request.StartArgs); return Task.FromResult(new Empty()); }

        public override Task<Empty> ShowDebugger(Empty _, ServerCallContext context)
        { Record("ShowDebugger"); return Task.FromResult(new Empty()); }

        public override Task<Empty> Screensaver(ScreensaverRequest request, ServerCallContext context)
        {
            Record("Screensaver:" + (int)request.State + ":" + request.PreviewHwnd + ":" + (request.FadeIn ? "1" : "0"));
            return Task.FromResult(new Empty());
        }

        public override Task<Empty> ShutDown(Empty _, ServerCallContext context)
        { Record("ShutDown"); return Task.FromResult(new Empty()); }

        public override Task<Empty> AutomationCommand(AutomationCommandRequest request, ServerCallContext context)
        {
            Record("AutomationCommand:" + string.Join("|", request.Args));
            return Task.FromResult(new Empty());
        }

        public override Task<Empty> SaveRectUI(Empty _, ServerCallContext context)
        { Record("SaveRectUI"); return Task.FromResult(new Empty()); }
    }

    internal static class GrpcServerProbe
    {
        public static int Run(string[] args)
        {
            // usage: grpcserver <portFile> [recordFile]
            var portFile = "grpc_port.txt";
            var recordFile = "grpc_records.txt";
            if (args.Length > 1) portFile = args[1];
            if (args.Length > 2) recordFile = args[2];
            foreach (var f in new[] { portFile, portFile + ".stop", recordFile })
            {
                if (File.Exists(f)) File.Delete(f);
            }

            var server = new Grpc.Core.Server
            {
                Services = { CommandsService.BindService(new ProbeCommandsServer(recordFile)) },
                Ports = { new ServerPort("127.0.0.1", 0, ServerCredentials.Insecure) }
            };
            server.Start();
            var port = server.Ports.Single().BoundPort;
            File.WriteAllText(portFile, port.ToString());
            Console.WriteLine("probe grpc server on 127.0.0.1:" + port);
            Console.Out.Flush();

            // Exit when the harness writes <portFile>.stop, or after 120s max
            // (safety) — the test harness relies on deterministic shutdown.
            var stopFile = portFile + ".stop";
            var deadline = DateTime.UtcNow.AddSeconds(120);
            while (!File.Exists(stopFile) && DateTime.UtcNow < deadline)
            {
                Thread.Sleep(100);
            }
            try { File.Delete(stopFile); File.Delete(portFile); } catch (IOException) { }
            server.ShutdownAsync().Wait();
            return 0;
        }
    }
}
