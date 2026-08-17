"""
tools.py

Standalone tool functions for the chess engine profiling agent.
These wrap the real headless_engine binary (compiled from headless_main.cpp)
and the gprof-based profiling workflow we already verified by hand.

IMPORTANT: test each function directly from this file (see the __main__ block
at the bottom) BEFORE ever wiring this up to an LLM. If a tool is broken, debug
it here first -- an agent loop on top of a broken tool just produces confusing
garbage and makes the wrong thing look like the LLM's fault.
"""

import subprocess
import json
import re
import os

# ---------------------------------------------------------------------------
# Configuration -- adjust these paths to match your WSL setup.
# ---------------------------------------------------------------------------
ENGINE_DIR = os.path.expanduser("~/chess-agent/Chess Engine")
HEADLESS_BINARY = os.path.join(ENGINE_DIR, "headless_engine")
HEADLESS_PROFILED_BINARY = os.path.join(ENGINE_DIR, "headless_engine_profiled")
SOURCE_FILES = ["Search.cpp", "Board.cpp", "MoveGen.cpp", "Zobrist.cpp", "Type.h"]


def run_benchmark(fen: str, depth: int) -> dict:
    """
    Runs the compiled headless chess engine on a given FEN position to a fixed
    depth, and returns timing + move info.

    This is a thin wrapper around the headless_engine binary we already built
    and verified by hand -- it just runs the subprocess and parses its JSON
    stdout output.
    """
    try:
        result = subprocess.run(
            [HEADLESS_BINARY, fen, str(depth)],
            capture_output=True,
            text=True,
            timeout=30,  # safety net: don't let a runaway search hang the agent loop forever
        )
    except subprocess.TimeoutExpired:
        return {"error": "Benchmark timed out after 30 seconds."}
    except FileNotFoundError:
        return {"error": f"Binary not found at {HEADLESS_BINARY}. Did you compile it?"}

    if result.returncode != 0:
        return {"error": f"Engine exited with error: {result.stderr.strip()}"}

    try:
        return json.loads(result.stdout.strip())
    except json.JSONDecodeError:
        return {"error": f"Could not parse engine output: {result.stdout!r}"}


def run_profiler(fen: str, depth: int, top_n: int = 5) -> dict:
    """
    Runs the gprof-instrumented build on a position/depth, then parses the
    resulting flat profile down to just the top N hottest functions.

    Deliberately returns ONLY the top N rows, not the full gprof report --
    full reports can be hundreds of lines and would burn through Groq's
    free-tier token budget fast for no real benefit to the agent's reasoning.
    """
    gmon_path = os.path.join(ENGINE_DIR, "gmon.out")

    try:
        bench_result = subprocess.run(
            [HEADLESS_PROFILED_BINARY, fen, str(depth)],
            capture_output=True,
            text=True,
            cwd=ENGINE_DIR,  # gmon.out is written to the CURRENT directory, must match
            timeout=30,
        )
    except subprocess.TimeoutExpired:
        return {"error": "Profiled run timed out after 30 seconds."}
    except FileNotFoundError:
        return {"error": f"Profiled binary not found at {HEADLESS_PROFILED_BINARY}. Did you compile with -pg?"}

    if not os.path.exists(gmon_path):
        return {"error": "gmon.out was not created. The profiled binary may not have run correctly."}

    try:
        gprof_result = subprocess.run(
            ["gprof", HEADLESS_PROFILED_BINARY, gmon_path],
            capture_output=True,
            text=True,
            cwd=ENGINE_DIR,
            timeout=30,
        )
    except subprocess.TimeoutExpired:
        return {"error": "gprof itself timed out."}

    hot_functions = _parse_gprof_flat_profile(gprof_result.stdout, top_n)
    return {"top_functions": hot_functions, "fen": fen, "depth": depth}


def _parse_gprof_flat_profile(gprof_output: str, top_n: int) -> list:
    """
    Parses gprof's flat profile text into a short list of dicts:
    [{"function": "...", "percent_time": ..., "calls": ...}, ...]

    This is intentionally a simple line-based parser matched to the real
    gprof output format we already saw, not a general-purpose gprof parser.
    """
    lines = gprof_output.splitlines()
    results = []

    # Flat profile data rows look like:
    #  22.81      0.13     0.13   521102     0.00     0.00  Search::evaluatePawnStructure(Board const&)
    row_pattern = re.compile(
        r"^\s*(\d+\.\d+)\s+([\d.]+)\s+([\d.]+)\s+(\d+)?\s*([\d.]+)?\s*([\d.]+)?\s+(.+)$"
    )

    in_flat_profile = False
    for line in lines:
        if "Flat profile" in line:
            in_flat_profile = True
            continue
        if not in_flat_profile:
            continue
        match = row_pattern.match(line)
        if match:
            percent_time = float(match.group(1))
            calls = match.group(4)
            func_name = match.group(7).strip()
            results.append({
                "function": func_name,
                "percent_time": percent_time,
                "calls": int(calls) if calls else None,
            })
        if len(results) >= top_n:
            break

    return results


def read_source(function_name: str) -> dict:
    """
    Searches the known source files for a function definition matching
    function_name and returns its source text.

    Simple substring-based search across Search.cpp/Board.cpp/MoveGen.cpp/
    Zobrist.cpp -- not a real C++ parser, but good enough for a known,
    small codebase like this one. Returns just the matching function body,
    not the whole file, to keep tool output short (see Groq token-budget
    notes in the roadmap).
    """
    for filename in SOURCE_FILES:
        filepath = os.path.join(ENGINE_DIR, filename)
        if not os.path.exists(filepath):
            continue

        with open(filepath, "r") as f:
            content = f.read()

        if "::" in function_name:
            function_name = function_name.split("::")[-1]
            
        # Look for "ReturnType ClassName::function_name(" as the start of a definition.
        pattern = re.compile(
            r"[\w:<>\*&,\s]+::"+ re.escape(function_name) + r"\s*\([^)]*\)[^\{]*\{"
        )
        match = pattern.search(content)
        if not match:
            continue

        start = match.start()
        brace_count = 0
        i = match.end() - 1  # position of the opening '{'
        end = None
        for j in range(i, len(content)):
            if content[j] == '{':
                brace_count += 1
            elif content[j] == '}':
                brace_count -= 1
                if brace_count == 0:
                    end = j + 1
                    break

        if end is None:
            return {"error": f"Found '{function_name}' in {filename} but could not match braces."}

        return {"file": filename, "function": function_name, "source": content[start:end]}

    return {"error": f"Function '{function_name}' not found in any known source file."}


# ---------------------------------------------------------------------------
# Manual test harness -- run this file directly to sanity-check each tool
# BEFORE wiring up the agent loop. This is Phase 3.2 from the roadmap.
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
    MIDGAME_FEN = "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4"

    print("=== Testing run_benchmark ===")
    print(json.dumps(run_benchmark(START_FEN, 6), indent=2))

    print("\n=== Testing run_profiler ===")
    print(json.dumps(run_profiler(MIDGAME_FEN, 7, top_n=5), indent=2))

    print("\n=== Testing read_source ===")
    print(json.dumps(read_source("evaluatePawnStructure"), indent=2))


# ===========================================================================
# Phase 5 tools -- patch proposal, application, and rebuild
# ===========================================================================

# Stores the pending patch between propose_patch() and apply_patch() calls.
# This is a simple in-process state dict -- it exists only while agent.py
# is running, not persisted to disk.
_pending_patch = {}


def propose_patch(function_name: str, new_code: str, reason: str) -> dict:
    """
    Called by the agent to propose a code change before applying it.
    Does NOT modify any file -- it just stores the proposal and returns
    a structured description so the human can review it in the terminal.

    The actual confirmation prompt is handled in agent.py's tool dispatcher,
    not here -- this function is kept pure (no input() calls) so it can be
    tested standalone without blocking.
    """
    global _pending_patch

    # Find the current source so we can show a before/after diff
    current = read_source(function_name)
    if "error" in current:
        return {"error": f"Cannot propose patch: {current['error']}"}

    _pending_patch = {
        "function_name": function_name,
        "file": current["file"],
        "old_code": current["source"],
        "new_code": new_code,
        "reason": reason,
        "status": "pending",
    }

    return {
        "status": "pending_review",
        "function": function_name,
        "file": current["file"],
        "reason": reason,
        "message": "Patch proposed. Awaiting human confirmation before applying.",
    }


def apply_patch() -> dict:
    """
    Applies the most recently proposed patch (stored in _pending_patch).
    Only call this after the human has confirmed via the terminal prompt.

    Safety measures:
    - Makes a .bak backup of the original file before writing anything.
    - Verifies the old_code still exists in the file before replacing
      (guards against the file having changed between propose and apply).
    - Returns a clear error if the backup or replacement fails.
    """
    global _pending_patch

    if not _pending_patch or _pending_patch.get("status") != "approved":
        return {"error": "No approved patch to apply. Call propose_patch first and confirm."}

    filepath = os.path.join(ENGINE_DIR, _pending_patch["file"])
    backup_path = filepath + ".bak"

    # Read current file content
    try:
        with open(filepath, "r") as f:
            content = f.read()
    except FileNotFoundError:
        return {"error": f"Source file not found: {filepath}"}

    # Verify the old code is still there (guard against stale proposals)
    old_code = _pending_patch["old_code"]
    if old_code not in content:
        return {
            "error": "Original function code no longer matches what was proposed. "
                     "File may have changed. Re-run propose_patch."
        }

    # Write backup before touching anything
    try:
        with open(backup_path, "w") as f:
            f.write(content)
    except Exception as e:
        return {"error": f"Failed to write backup: {e}"}

    # Apply the replacement
    new_content = content.replace(old_code, _pending_patch["new_code"], 1)
    try:
        with open(filepath, "w") as f:
            f.write(new_content)
    except Exception as e:
        return {"error": f"Failed to write patched file: {e}"}

    _pending_patch["status"] = "applied"
    return {
        "status": "applied",
        "file": _pending_patch["file"],
        "backup": backup_path,
        "message": "Patch applied. Run rebuild() to recompile.",
    }


def rebuild() -> dict:
    """
    Recompiles both the normal and profiled binaries after a patch.
    Returns success/failure and the compiler output so the agent can
    report build errors accurately rather than guessing.
    """
    source_files = " ".join(
        os.path.join(ENGINE_DIR, f)
        for f in ["headless_main.cpp", "Board.cpp", "MoveGen.cpp", "Search.cpp", "Zobrist.cpp"]
    )

    # Build normal binary
    normal_cmd = (
        f"g++ -O2 -std=c++17 "
        f"-o {HEADLESS_BINARY} "
        f"{source_files}"
    )
    # Build profiled binary
    profiled_cmd = (
        f"g++ -O2 -pg -std=c++17 "
        f"-o {HEADLESS_PROFILED_BINARY} "
        f"{source_files}"
    )

    results = {}
    for label, cmd in [("normal", normal_cmd), ("profiled", profiled_cmd)]:
        try:
            result = subprocess.run(
                cmd,
                shell=True,
                capture_output=True,
                text=True,
                cwd=ENGINE_DIR,
                timeout=60,
            )
            results[label] = {
                "success": result.returncode == 0,
                "stderr": result.stderr.strip()[:500] if result.stderr else "",
            }
        except subprocess.TimeoutExpired:
            results[label] = {"success": False, "stderr": "Compilation timed out."}

    all_ok = all(r["success"] for r in results.values())
    return {
        "status": "success" if all_ok else "failed",
        "builds": results,
        "message": "Both binaries rebuilt successfully." if all_ok
                   else "One or more builds failed -- see stderr for details.",
    }


def revert_patch() -> dict:
    """
    Restores the .bak backup if a patch turned out to be wrong.
    Can be called manually or triggered by the agent after a failed rebuild.
    """
    if not _pending_patch or "file" not in _pending_patch:
        return {"error": "No patch history to revert."}

    filepath = os.path.join(ENGINE_DIR, _pending_patch["file"])
    backup_path = filepath + ".bak"

    if not os.path.exists(backup_path):
        return {"error": f"No backup found at {backup_path}. Cannot revert."}

    try:
        with open(backup_path, "r") as f:
            original = f.read()
        with open(filepath, "w") as f:
            f.write(original)
        os.remove(backup_path)
    except Exception as e:
        return {"error": f"Revert failed: {e}"}

    return {"status": "reverted", "file": _pending_patch["file"],
            "message": "File restored from backup. Remember to rebuild."}