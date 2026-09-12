#!/usr/bin/env python3
"""Differential fuzzing driver: C# CommandLineParser (oracle) vs lively::utility::ParseCommandLine.

Generates a deterministic pseudo-random corpus of candidate command lines,
runs both parsers (csharp_probe fuzz mode / lively_fuzz_target), and diffs
their canonical outcome lines. Exit 0 = equivalent over the corpus.

Usage: python tools/fuzz_differential.py [N_CASES] [SEED]
"""
import os
import random
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROBE = os.path.join(ROOT, "tools", "csharp_probe")
CPP_TARGET = os.path.join(ROOT, "build", "lively_fuzz_target.exe")

VERBS = ["app", "setwp", "closewp", "seekwp", "setprop", "screensaver", "screenshot", "bogus"]
OPTIONS = {
    "app": ["showApp", "showIcons", "volume", "play", "startup", "shutdown", "restart", "layout"],
    "setwp": ["file", "monitor"],
    "closewp": ["monitor"],
    "seekwp": ["value", "monitor"],
    "setprop": ["property", "monitor"],
    "screensaver": ["preview", "configure", "show", "showExclusive", "fadeIn"],
    "screenshot": ["file", "monitor"],
    "bogus": ["anything"],
}
BOOL_VALUES = ["true", "false", "True", "FALSE", "1", "yes", ""]
NUM_VALUES = ["0", "1", "-1", "42", "2147483647", "2147483648", "999999999999", "abc", "-"]
STR_VALUES = ["random", "reload", "C:\\x\\y", "key=value", "saturation=100", "", "+10", "-5"]
# Adversarial tokens: the first group was added after the player StartArgs oracle
# exposed two tokenizer rules this corpus had never exercised — "--=" produces the
# NAME "=" (not BadFormatTokenError) and a single dash takes exactly one character
# as the name ("-abc" -> Name("a") + Value("bc")).
RAND_TOKENS = ["--", "-", "x", "0x052C", "zh-CN", "LIVELY:DESKTOPWALLPAPERSYSTEM",
               "--=", "--=x", "-abc", "-Infinity", "-5", "--x=", "--monitor=",
               "--file=", "--volume==b", "--showApp=false", "--layout=SPAN"]


def gen_case(rng):
    n_parts = rng.randint(0, 6)
    parts = []
    verb = rng.choice(VERBS)
    if rng.random() < 0.9:
        parts.append(verb)
    for _ in range(n_parts):
        if rng.random() < 0.15:
            parts.append(rng.choice(RAND_TOKENS))
            continue
        if rng.random() < 0.85:
            name = rng.choice(OPTIONS[verb]) if verb in OPTIONS else rng.choice(
                [o for opts in OPTIONS.values() for o in opts])
        else:
            name = rng.choice(["unknownOpt", "monitor", "file", "show"])
        style = rng.random()
        if style < 0.3:
            parts.append(f"--{name}={rng.choice(BOOL_VALUES + NUM_VALUES + STR_VALUES)}")
        else:
            parts.append(f"--{name}")
            if rng.random() < 0.85:
                parts.append(rng.choice(BOOL_VALUES + NUM_VALUES + STR_VALUES))
    # Keep single spaces; empty parts removed to emulate shell arg splitting.
    parts = [p for p in parts if p != ""]
    return " ".join(parts)


def run_csharp(cases):
    case_file = os.path.join(ROOT, "build", "fuzz_cases.txt")
    with open(case_file, "w", encoding="utf-8") as f:
        f.write("\n".join(cases) + "\n")
    out = subprocess.run(
        ["dotnet", "run", "--project", PROBE, "-c", "Release", "--no-build", "--",
         "fuzz", case_file],
        capture_output=True, text=True, encoding="utf-8", timeout=600)
    if out.returncode != 0:
        print(out.stdout, out.stderr)
        sys.exit("csharp probe failed")
    return [line.split("\t", 1)[1] if "\t" in line else "" for line in out.stdout.splitlines()]


def run_cpp(cases):
    case_file = os.path.join(ROOT, "build", "fuzz_cases.txt")
    out = subprocess.run([CPP_TARGET, case_file], capture_output=True, text=True,
                         encoding="utf-8", timeout=600)
    if out.returncode != 0:
        print(out.stdout, out.stderr)
        sys.exit("c++ fuzz target failed")
    return [line.split("\t", 1)[1] if "\t" in line else "" for line in out.stdout.splitlines()]


def main():
    n_cases = int(sys.argv[1]) if len(sys.argv) > 1 else 500
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260911
    rng = random.Random(seed)
    cases = [gen_case(rng) for _ in range(n_cases)]

    cs = run_csharp(cases)
    cpp = run_cpp(cases)

    divergences = 0
    shown = 0
    for case, a, b in zip(cases, cs, cpp):
        if a != b:
            divergences += 1
            if shown < 10:
                shown += 1
                print(f"DIVERGE: '{case}'\n  csharp: {a}\n  c++   : {b}")
    print(f"{n_cases - divergences}/{n_cases} equivalent ({divergences} divergences), seed={seed}")
    sys.exit(1 if divergences else 0)


if __name__ == "__main__":
    main()
