"""
agent.py

The chess engine profiling agent loop.
Ties together:
  - Groq API (llama-3.3-70b-versatile, free tier)
  - Tool schemas (descriptions sent to the model so it knows what it can call)
  - Real tool functions from tools.py (run_benchmark, run_profiler, read_source)

Usage:
  export GROQ_API_KEY=your_key_here
  python3 agent.py

The loop:
  1. Send system prompt + tool schemas + user message to Groq
  2. If model returns tool_calls → run the real Python function, send result back
  3. If model returns plain text → print it, we're done
  4. Repeat until done or max iterations hit
"""

import os
import json
import time
import sys
from openai import OpenAI
from dotenv import load_dotenv
from tools import (
    run_benchmark, run_profiler, read_source,
    propose_patch, apply_patch, rebuild, revert_patch,
    _pending_patch,
)

# Load GROQ_API_KEY from .env file in the same directory
load_dotenv()

# ---------------------------------------------------------------------------
# Tee -- writes every print() to both the terminal AND result.txt
# This way you see output live while it runs, and it's saved automatically.
# ---------------------------------------------------------------------------
class Tee:
    def __init__(self, filepath):
        self.terminal = sys.stdout
        self.file = open(filepath, "w", encoding="utf-8")

    def write(self, message):
        self.terminal.write(message)
        self.file.write(message)

    def flush(self):
        self.terminal.flush()
        self.file.flush()

    def close(self):
        self.file.close()

# Start capturing -- everything printed after this line goes to both places
_tee = Tee("result.txt")
sys.stdout = _tee

# ---------------------------------------------------------------------------
# Groq client setup -- same OpenAI SDK, different base URL
# ---------------------------------------------------------------------------
client = OpenAI(
    base_url="https://api.groq.com/openai/v1",
    api_key=os.environ.get("GROQ_API_KEY"),
)

MODEL = "openai/gpt-oss-120b"
MAX_ITERATIONS = 10  # safety cap -- prevents runaway loops burning your free-tier quota

# ---------------------------------------------------------------------------
# System prompt -- tells the model what its job is and how to behave
# ---------------------------------------------------------------------------
SYSTEM_PROMPT = """You are a performance analysis assistant for a C++ chess engine.
Your job is to diagnose performance bottlenecks and propose concrete, specific optimizations.

You have access to these tools:
- run_benchmark: runs the engine on a position and returns timing data
- run_profiler: runs gprof on the engine and returns the top hottest functions by % time
- read_source: returns the source code of a specific named function from the engine
- propose_patch: proposes a replacement for a function (human must confirm before applying)
- apply_patch: applies the approved patch to the source file (only after human confirms)
- rebuild: recompiles the engine after a patch
- revert_patch: restores the original file from backup if something goes wrong

When asked to investigate and fix performance, follow this order:
1. Run a benchmark to get baseline timing numbers.
2. Run the profiler to find the hottest functions.
3. Read the source of the hottest function(s) to understand WHY they are slow.
4. Propose a specific patch using propose_patch -- explain the reason clearly.
5. Wait for the human to confirm (you will see "approved" or "rejected" in the result).
6. If approved: call apply_patch, then rebuild.
7. If rebuild succeeds: run_benchmark again and compare before/after timing.
8. If rebuild fails: call revert_patch, then rebuild to restore the original.

Important rules:
- Always explain your reasoning before calling a tool.
- Never guess at performance numbers -- only state numbers retrieved from a tool.
- Never call apply_patch without a prior approved propose_patch result.
- If a tool returns an error, report it clearly rather than working around it silently.
- Keep proposed patches minimal -- change only what is needed to fix the bottleneck.
- When proposing a patch, keep new_code under 50 lines. If the fix requires more,
  propose it in stages, one small change at a time.
- For caching fixes, add only the cache lookup/store lines around the existing logic,
  do not rewrite the entire function.
- If read_source returns an error twice in a row, stop searching and work with
  the source you already have. Do not try more than 2 variations of the same
  function name.
- The pawn structure cache key should use board.getHashKey() which already exists --
  do not search for a separate hash function."""
  
# ---------------------------------------------------------------------------
# Tool schemas -- these are NOT the actual functions.
# They are descriptions sent to the model so it knows what it CAN call.
# The model reads these as text and decides when/whether to call each one.
# Your code (below) is what actually executes them.
# ---------------------------------------------------------------------------
TOOL_SCHEMAS = [
    {
        "type": "function",
        "function": {
            "name": "run_benchmark",
            "description": "Runs the chess engine's search on a given FEN position to a fixed depth. Returns the best move found, time taken in milliseconds, and the depth searched.",
            "parameters": {
                "type": "object",
                "properties": {
                    "fen": {
                        "type": "string",
                        "description": "The board position in FEN notation, e.g. 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'"
                    },
                    "depth": {
                        "type": "integer",
                        "description": "Search depth to run. Use 6 for quick benchmarks, 7 for more thorough ones."
                    }
                },
                "required": ["fen", "depth"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "run_profiler",
            "description": "Runs the gprof-instrumented engine on a position and returns the top hottest functions by percentage of CPU time spent. Use this after run_benchmark to find WHERE the time is going.",
            "parameters": {
                "type": "object",
                "properties": {
                    "fen": {
                        "type": "string",
                        "description": "The board position in FEN notation."
                    },
                    "depth": {
                        "type": "integer",
                        "description": "Search depth. Match the depth used in run_benchmark for comparable results."
                    },
                    "top_n": {
                        "type": "integer",
                        "description": "How many top functions to return. Default 5, max 10."
                    }
                },
                "required": ["fen", "depth"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "read_source",
            "description": "Returns the full source code of a named function from the chess engine's C++ source files. Use this after run_profiler to understand WHY a hot function is slow.",
            "parameters": {
                "type": "object",
                "properties": {
                    "function_name": {
                        "type": "string",
                        "description": "The function name to look up, e.g. 'evaluatePawnStructure' or 'scoreMove'. Do not include the class name or return type."
                    }
                },
                "required": ["function_name"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "propose_patch",
            "description": (
                "Proposes a code change to a specific function. Does NOT apply it yet -- "
                "the human must confirm before anything is written to disk. "
                "Always call this before apply_patch. Explain your reason clearly."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "function_name": {
                        "type": "string",
                        "description": "The function to patch, e.g. 'evaluatePawnStructure'."
                    },
                    "new_code": {
                        "type": "string",
                        "description": "The complete replacement function body (including signature and braces)."
                    },
                    "reason": {
                        "type": "string",
                        "description": "A clear explanation of what is being changed and why it will improve performance."
                    }
                },
                "required": ["function_name", "new_code", "reason"]
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "apply_patch",
            "description": (
                "Applies the previously proposed and human-approved patch to the source file. "
                "Only call this after propose_patch has been called and the human confirmed. "
                "A .bak backup is made automatically."
            ),
            "parameters": {
                "type": "object",
                "properties": {},
                "required": []
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "rebuild",
            "description": (
                "Recompiles the engine after a patch. Returns success/failure and "
                "compiler output. Call this after apply_patch, then run_benchmark "
                "again to compare before/after performance."
            ),
            "parameters": {
                "type": "object",
                "properties": {},
                "required": []
            }
        }
    },
    {
        "type": "function",
        "function": {
            "name": "revert_patch",
            "description": (
                "Restores the original source file from the .bak backup if a patch "
                "caused a build failure or made performance worse. Call rebuild() after "
                "reverting to restore the original binaries."
            ),
            "parameters": {
                "type": "object",
                "properties": {},
                "required": []
            }
        }
    },
]

# ---------------------------------------------------------------------------
# Tool dispatcher -- maps tool name strings to actual Python functions.
# propose_patch gets special handling: after the tool returns, we pause
# and ask for human confirmation before the agent can call apply_patch.
# This is what YOUR CODE runs when the model requests a tool call.
# The model itself never executes anything -- it only sends a request.
# ---------------------------------------------------------------------------
import tools as _tools_module

def _dispatch_tool(tool_name: str, tool_args: dict) -> dict:
    """
    Runs the real tool function and, for propose_patch specifically,
    blocks on a human confirmation prompt before marking the patch approved.
    This is where the safety gate lives -- apply_patch checks for
    _pending_patch["status"] == "approved" before writing anything.
    """
    dispatch = {
        "run_benchmark": run_benchmark,
        "run_profiler": run_profiler,
        "read_source": read_source,
        "propose_patch": propose_patch,
        "apply_patch": apply_patch,
        "rebuild": rebuild,
        "revert_patch": revert_patch,
    }

    if tool_name not in dispatch:
        return {"error": f"Unknown tool: {tool_name}"}

    result = dispatch[tool_name](**tool_args)

    # --- Human confirmation gate for propose_patch ---
    if tool_name == "propose_patch" and result.get("status") == "pending_review":
        # Temporarily restore real stdout so input() works even with the Tee active
        real_stdout = sys.stdout
        sys.stdout = sys.stdout.terminal if hasattr(sys.stdout, "terminal") else sys.stdout

        print("\n" + "=" * 60)
        print("PATCH PROPOSAL — HUMAN REVIEW REQUIRED")
        print("=" * 60)
        print(f"Function : {result['function']}")
        print(f"File     : {result['file']}")
        print(f"Reason   : {result['reason']}")
        print("-" * 60)
        print("NEW CODE:")
        print(_tools_module._pending_patch.get("new_code", ""))
        print("=" * 60)

        while True:
            answer = input("Apply this patch? [yes/no]: ").strip().lower()
            if answer in ("yes", "y"):
                _tools_module._pending_patch["status"] = "approved"
                result["status"] = "approved"
                result["message"] = "Human approved. Call apply_patch() to write the change."
                break
            elif answer in ("no", "n"):
                _tools_module._pending_patch["status"] = "rejected"
                result["status"] = "rejected"
                result["message"] = "Human rejected the patch. Do not call apply_patch()."
                break
            else:
                print("Please type 'yes' or 'no'.")

        sys.stdout = real_stdout  # restore Tee

    return result

# ---------------------------------------------------------------------------
# The agent loop
# ---------------------------------------------------------------------------
def run_agent(user_question: str):
    print(f"\nUser: {user_question}\n")
    print("-" * 60)

    # The conversation history -- grows with every round trip.
    # The ENTIRE history is resent on every API call because the model
    # has zero memory between calls. This is why token usage grows over time.
    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": user_question},
    ]

    for iteration in range(MAX_ITERATIONS):
        print(f"[Iteration {iteration + 1}]")

        # --- Step 1: Send current conversation to the model ---
        try:
            response = client.chat.completions.create(
                model=MODEL,
                messages=messages,
                tools=TOOL_SCHEMAS,
                tool_choice="auto",  # model decides whether to call a tool or answer directly
                max_tokens=1000,
            )
        except Exception as e:
            # Handle rate limit (429) by waiting and retrying
            if "429" in str(e) or "rate_limit" in str(e).lower():
                wait_time = 60
                print(f"  Rate limited -- waiting {wait_time}s before retrying...")
                time.sleep(wait_time)
                continue
            print(f"  API error: {e}")
            break

        message = response.choices[0].message

        # --- Step 2: Check what the model returned ---
        if message.tool_calls:
            # Model wants to call one or more tools.
            # Append the model's response to history first (required by the API).
            messages.append(message)

            # Process every tool call in this response (there may be multiple).
            for tool_call in message.tool_calls:
                tool_name = tool_call.function.name
                tool_args = json.loads(tool_call.function.arguments)

                print(f"  Tool call: {tool_name}({tool_args})")

                # --- Step 3: YOUR CODE runs the actual function ---
                # _dispatch_tool handles the special confirmation prompt
                # for propose_patch before marking it approved/rejected.
                result = _dispatch_tool(tool_name, tool_args)

                print(f"  Result: {json.dumps(result)[:200]}...")  # truncate for readability

                # --- Step 4: Send the result back as a tool message ---
                # tool_call_id is the matching label -- without it the model
                # can't know which result answers which request (especially
                # when multiple tools were called in the same response).
                messages.append({
                    "role": "tool",
                    "tool_call_id": tool_call.id,
                    "content": json.dumps(result),
                })

        else:
            # --- Step 2a: Model gave a final text answer -- we're done ---
            print(f"\nAgent: {message.content}")
            return

    print("\n[Max iterations reached without a final answer]")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    run_agent(
        "Analyse the performance of my chess engine on this position: "
        "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4 "
        "at depth 7. Find the main bottleneck, propose a fix, apply it, rebuild, and compare before/after performance."
    )
    _tee.close()
    sys.stdout = _tee.terminal  # restore normal stdout
    print("Output saved to result.txt")